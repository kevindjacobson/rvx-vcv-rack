#include "../src/core/Video.hpp"
#include "../src/io/VideoBackend.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using namespace rvx;

namespace {
int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)

bool finiteNear(float actual, float expected, float tolerance) {
    return std::isfinite(actual) && std::isfinite(expected) && std::isfinite(tolerance) &&
        tolerance >= 0.f && std::fabs(actual - expected) <= tolerance;
}

void near(float actual, float expected, float tolerance, int line) {
    if (!finiteNear(actual, expected, tolerance)) {
        std::cerr << "FAIL line " << line << ": got " << actual << ", expected "
                  << expected << " +/- " << tolerance << '\n';
        ++failures;
    }
}
#define NEAR(a, b, t) near((a), (b), (t), __LINE__)

Connection connect(const std::shared_ptr<Node>& source, int output,
                   const std::shared_ptr<Node>& destination, int input) {
    return {source->key, output, destination->key, input};
}

float pixel(const FramePtr& frame, int x, int y, int channel = 0) {
    CHECK(frame != nullptr);
    if (!frame) return 0.f;
    return frame->pixels[(static_cast<size_t>(y) * frame->width + x) * frame->channels + channel];
}

class TickValueBackend : public VideoBackend {
public:
    std::vector<VideoSource> sources() override { return {}; }
    FramePtr receive(const IoSettings&, const Format& format, uint64_t tick, double seconds) override {
        auto frame = std::make_shared<Frame>();
        frame->width = format.width;
        frame->height = format.height;
        frame->channels = 4;
        frame->sequence = 1000000 - tick; // Delay age must not depend on source metadata.
        frame->seconds = seconds;
        frame->pixels.resize(static_cast<size_t>(format.width) * format.height * 4);
        for (size_t p = 0; p < frame->pixels.size(); p += 4) {
            frame->pixels[p] = frame->pixels[p + 1] = frame->pixels[p + 2] =
                static_cast<float>(tick + 1);
            frame->pixels[p + 3] = 1.f;
        }
        return frame;
    }
    void publish(const IoSettings&, FramePtr) override {}
    std::string status() const override { return "tick source"; }
};

Graph delayGraph(const std::shared_ptr<Node>& source, const std::shared_ptr<Node>& delay) {
    return {{delay, source}, {connect(source, 0, delay, 0)}, 1};
}

void testTypesDefaultsAndQueue() {
    CHECK(!finiteNear(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f));
    CHECK(!finiteNear(0.f, std::numeric_limits<float>::infinity(), 0.f));
    CHECK(!finiteNear(0.f, 0.f, std::numeric_limits<float>::infinity()));

    CHECK(inputType(Kind::Processor, 0) == PortType::Image);
    CHECK(inputType(Kind::Processor, 2) == PortType::Field);
    CHECK(inputType(Kind::Delay, 1) == PortType::Audio);
    CHECK(inputType(Kind::TestImage, 0) == PortType::None);
    CHECK(outputType(Kind::TestImage, 1) == PortType::Field);
    CHECK(outputType(Kind::Monitor, 0) == PortType::None);

    Node processor(Kind::Processor);
    Node bridge(Kind::CvBridge);
    Node delay(Kind::Delay);
    NEAR(processor.params[0].load(), 1.f, 0.f);
    NEAR(processor.params[1].load(), 0.f, 0.f);
    NEAR(bridge.params[1].load(), 0.1f, 0.f);
    NEAR(delay.params[kDelayFramesParam].load(), static_cast<float>(kDefaultDelayFrames), 0.f);
    CHECK(kDelayClearParam == 0 && kDelayFramesParam == 1);
    CHECK(normalizedDelayFrames(-100.f) == 1);
    CHECK(normalizedDelayFrames(1.49f) == 1);
    CHECK(normalizedDelayFrames(1.5f) == 2);
    CHECK(normalizedDelayFrames(59.6f) == 60);
    CHECK(normalizedDelayFrames(100.f) == 60);
    CHECK(normalizedDelayFrames(std::numeric_limits<float>::infinity()) == 1);
    CHECK(normalizedDelayFrames(std::numeric_limits<float>::quiet_NaN()) == 1);
    CHECK(processor.key != 0 && bridge.key > processor.key);

    SpscQueue<int, 4> queue;
    CHECK(queue.push(1)); CHECK(queue.push(2)); CHECK(queue.push(3));
    CHECK(!queue.push(4)); CHECK(queue.dropped.load() == 1);
    int value = 0;
    CHECK(queue.pop(value) && value == 1);
    CHECK(queue.pop(value) && value == 2);
    CHECK(queue.pop(value) && value == 3);
    CHECK(!queue.pop(value));
}

void testRenderTimingHistogram() {
    RenderTimingHistogram histogram;
    CHECK(histogram.quantile(.50) == 0.0);
    CHECK(histogram.count == 0 && histogram.saturated == 0);

    histogram.observe(0.0);
    histogram.observe(0.01);
    histogram.observe(1.0);
    histogram.observe(10.01);
    histogram.observe(std::numeric_limits<double>::max());
    CHECK(histogram.count == 5);
    CHECK(histogram.saturated == 1);
    CHECK(histogram.maximumMilliseconds == std::numeric_limits<double>::max());
    CHECK(histogram.quantile(-1.0) == 0.0);
    CHECK(histogram.quantile(.50) == 1.0);
    CHECK(histogram.quantile(.95) == RenderTimingHistogram::saturationMilliseconds);
    CHECK(histogram.quantile(2.0) == RenderTimingHistogram::saturationMilliseconds);
    CHECK(histogram.quantile(std::numeric_limits<double>::quiet_NaN()) == 0.0);
}

void testPatternsProcessorAndConversion() {
    Format format{4, 2, 2, 1};
    auto source = std::make_shared<Node>(Kind::TestImage);
    auto processor = std::make_shared<Node>(Kind::Processor);
    source->params[0].store(2.f); // horizontal ramp
    processor->params[0].store(2.f);
    processor->params[2].store(-0.5f);
    Graph graph{{source, processor}, {connect(source, 0, processor, 0),
                                      connect(source, 1, processor, 2)}, 1};
    Renderer renderer;
    RenderReport report = renderer.render(graph, format, 7, 3.5);
    CHECK(report.errors == 0);
    auto sourceDisplay = source->display();
    auto processed = processor->display()->outputs[0];
    CHECK(sourceDisplay->tick == 7 && processed->sequence == 7);
    NEAR(pixel(sourceDisplay->outputs[0], 3, 0, 0), 1.f, 1e-6f);
    // ((1 * 2) - .5) * per-pixel ramp. Values remain unclipped.
    NEAR(pixel(processed, 3, 0, 0), 1.5f, 1e-6f);
    NEAR(pixel(processed, 1, 0, 0), (2.f / 3.f - .5f) / 3.f, 1e-6f);
    NEAR(pixel(processed, 3, 0, 3), 1.f, 0.f); // modulation does not alter alpha

    processor->params[3].store(1.f); // field to gray
    renderer.render(graph, format, 8, 4.0);
    processed = processor->display()->outputs[0];
    NEAR(pixel(processed, 2, 0, 0), 2.f / 3.f, 1e-6f);
    NEAR(pixel(processed, 2, 0, 1), 2.f / 3.f, 1e-6f);
    NEAR(pixel(processed, 2, 0, 3), 1.f, 0.f);

    source->params[0].store(0.f); // bars: second bar is yellow
    processor->params[3].store(3.f); // green component
    renderer.render(graph, format, 9, 4.5);
    NEAR(pixel(processor->display()->outputs[1], 1, 0), 1.f, 0.f);

    processor->params[2].store(std::numeric_limits<float>::infinity());
    report = renderer.render(graph, format, 10, 5.0);
    CHECK(report.errors == 1);
    CHECK(processor->display()->status.find("non-finite parameter") != std::string::npos);
}

void testRasterPhaseOrdering() {
    Format format{4, 2, 2, 1}; // 0.5 second frames, eight ordered samples
    auto source = std::make_shared<Node>(Kind::TestImage);
    source->params[0].store(3.f);
    source->params[1].store(0.25f); // cycles / second
    Renderer renderer;
    Graph graph{{source}, {}, 1};
    renderer.render(graph, format, 0, 0.0);
    const FramePtr first = source->display()->outputs[1];
    const float step = 0.25f * 0.5f / 8.f;
    for (size_t i = 1; i < first->pixels.size(); ++i)
        NEAR(first->pixels[i] - first->pixels[i - 1], step, 1e-6f);
    renderer.render(graph, format, 1, 0.5);
    const FramePtr second = source->display()->outputs[1];
    NEAR(second->pixels.front() - first->pixels.back(), step, 1e-6f);

    auto automated = std::make_shared<Node>(Kind::TestImage);
    automated->params[0].store(3.f);
    automated->params[1].store(1.f);
    Renderer automatedRenderer;
    Graph automatedGraph{{automated}, {}, 1};
    const Format automatedFormat{8, 1, 10, 1};
    automatedRenderer.render(automatedGraph, automatedFormat, 0, 0);
    NEAR(automated->display()->outputs[1]->pixels.back(), .0875f, 1e-6f);
    automated->params[1].store(2.f);
    automatedRenderer.render(automatedGraph, automatedFormat, 1, .1);
    NEAR(automated->display()->outputs[1]->pixels.front(), .1f, 1e-6f);
    NEAR(automated->display()->outputs[1]->pixels[1], .125f, 1e-6f);
}

void testPhaseSpeedAllPatterns() {
    const Format format{64, 2, 2, 1};
    for (int pattern = 0; pattern < 4; ++pattern) {
        auto source = std::make_shared<Node>(Kind::TestImage);
        source->params[0].store(static_cast<float>(pattern));
        source->params[1].store(1.f);
        Renderer renderer;
        Graph graph{{source}, {}, 1};
        renderer.render(graph, format, 0, 0);
        const auto initial = source->display()->outputs[0]->pixels;
        renderer.render(graph, format, 1, .5);
        CHECK(source->display()->outputs[0]->pixels != initial);

        source->resets.fetch_add(1);
        renderer.render(graph, format, 2, .75);
        const auto reset = source->display()->outputs[0]->pixels;
        CHECK(reset == initial);
        source->params[1].store(0.f);
        renderer.render(graph, format, 3, 1.0);
        const auto stopped = source->display()->outputs[0]->pixels;
        renderer.render(graph, format, 4, 1.25);
        CHECK(source->display()->outputs[0]->pixels == stopped);
        source->params[1].store(-1.f);
        renderer.render(graph, format, 5, 1.5);
        renderer.render(graph, format, 6, 1.75);
        CHECK(source->display()->outputs[0]->pixels != stopped);
    }
}

void testInvalidEdgesFormatsAndCycles() {
    Format format{2, 2, 30, 1};
    auto processor = std::make_shared<Node>(Kind::Processor);
    Renderer renderer;
    Graph malformed{{processor}, {{0, -1, processor->key, 0}}, 1};
    auto report = renderer.render(malformed, format, 0, 0);
    CHECK(report.errors == 1);
    CHECK(processor->display()->status.find("invalid connection") != std::string::npos);
    NEAR(pixel(processor->display()->outputs[0], 0, 0, 0), 0.f, 0.f);

    auto a = std::make_shared<Node>(Kind::Processor);
    auto b = std::make_shared<Node>(Kind::Processor);
    Graph cycle{{a, b}, {connect(a, 0, b, 0), connect(b, 0, a, 0)}, 2};
    report = renderer.render(cycle, format, 1, 1.0 / 30.0);
    CHECK(report.errors == 2);
    CHECK(a->display()->status.find("zero-delay cycle") != std::string::npos);
    CHECK(!a->display()->outputs[0]);

    auto delay = std::make_shared<Node>(Kind::Delay);
    Graph bypassCycle{{a, delay}, {connect(a, 0, delay, 0), connect(delay, 0, a, 0)}, 3};
    delay->bypass.store(true);
    report = renderer.render(bypassCycle, format, 2, 2.0 / 30.0);
    CHECK(report.errors == 2);
    CHECK(delay->display()->status.find("zero-delay cycle") != std::string::npos);

    report = renderer.render(malformed, Format{0, 480, 30000, 1001}, 3, 0.1);
    CHECK(report.errors == 1);
    CHECK(processor->display()->status == "invalid video format");

    report = renderer.render(Graph{{processor}, {}, 4}, Format{4096, 4096, 30, 1}, 4, 0.2);
    CHECK(report.errors == 1);
    CHECK(processor->display()->status.find("512 MiB") != std::string::npos);

    auto retainedMonitor = std::make_shared<Node>(Kind::Monitor);
    Renderer retainedRenderer;
    report = retainedRenderer.render(Graph{{retainedMonitor}, {}, 6},
                                     Format{1024, 2048, 30, 1}, 6, 0.4);
    CHECK(report.errors == 0);
    auto largeSource = std::make_shared<Node>(Kind::TestImage);
    std::vector<std::shared_ptr<Node>> nearBudget{retainedMonitor, largeSource};
    for (int i = 0; i < 10; ++i)
        nearBudget.push_back(std::make_shared<Node>(Kind::Processor));
    report = retainedRenderer.render(Graph{nearBudget, {}, 7},
                                     Format{2048, 1024, 30, 1}, 7, 0.5);
    CHECK(report.errors == 1);
    CHECK(retainedMonitor->display()->status.find("512 MiB") != std::string::npos);
}

void testFormatChangeClearsRasterState() {
    auto source = std::make_shared<Node>(Kind::TestImage);
    auto delay = std::make_shared<Node>(Kind::Delay);
    auto processor = std::make_shared<Node>(Kind::Processor);
    source->params[0].store(2.f);
    Graph graph{{source, delay, processor},
                {connect(source, 0, delay, 0), connect(delay, 0, processor, 0)}, 1};
    Renderer renderer;
    renderer.render(graph, Format{2, 1, 30, 1}, 0, 0);
    renderer.render(graph, Format{2, 1, 30, 1}, 1, 1.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 1, 0, 0), 1.f, 0.f);

    auto report = renderer.render(graph, Format{4, 1, 30, 1}, 2, 2.0 / 30.0);
    CHECK(report.errors == 0);
    CHECK(delay->display()->outputs[0]->width == 4);
    CHECK(processor->display()->outputs[0]->width == 4);
    for (float value : delay->display()->outputs[0]->pixels) NEAR(value, 0.f, 0.f);
    renderer.render(graph, Format{4, 1, 30, 1}, 3, 3.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 3, 0, 0), 1.f, 0.f);
    renderer.render(graph, Format{4, 1, 60, 1}, 4, 4.0 / 60.0);
    for (float value : delay->display()->outputs[0]->pixels) NEAR(value, 0.f, 0.f);
    renderer.render(graph, Format{4, 1, 60, 1}, 5, 5.0 / 60.0);
    NEAR(pixel(delay->display()->outputs[0], 3, 0, 0), 1.f, 0.f);
}

void testDelayReadCommitClearAndCleanup() {
    Format format{2, 1, 1, 1};
    auto source = std::make_shared<Node>(Kind::TestImage);
    auto delay = std::make_shared<Node>(Kind::Delay);
    auto processor = std::make_shared<Node>(Kind::Processor);
    source->params[0].store(2.f);
    processor->params[0].store(1.f);
    processor->params[2].store(0.25f);
    // Explicit delay cuts this feedback cycle. Delay must read old state before Processor commits new.
    Graph graph{{delay, processor, source},
                {connect(delay, 0, processor, 0), connect(source, 0, processor, 1),
                 connect(processor, 0, delay, 0)}, 1};
    Renderer renderer;
    auto report = renderer.render(graph, format, 0, 0);
    CHECK(report.errors == 0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0, 0), 0.f, 0.f);
    renderer.render(graph, format, 1, 1);
    NEAR(pixel(delay->display()->outputs[0], 0, 0, 0), 0.25f, 1e-6f);
    renderer.render(graph, format, 2, 2);
    NEAR(pixel(delay->display()->outputs[0], 0, 0, 0), 0.5f, 1e-6f);

    delay->resets.fetch_add(1);
    renderer.render(graph, format, 3, 3);
    NEAR(pixel(delay->display()->outputs[0], 0, 0, 0), 0.f, 0.f);
    renderer.render(graph, format, 4, 4);
    NEAR(pixel(delay->display()->outputs[0], 0, 0, 0), 0.25f, 1e-6f);

    // Removing a node causes its keyed temporal state to be released.
    renderer.render(Graph{}, format, 5, 5);
    renderer.render(graph, format, 6, 6);
    NEAR(pixel(delay->display()->outputs[0], 0, 0, 0), 0.f, 0.f);

    for (int cycle = 0; cycle < 50; ++cycle) {
        renderer.render(Graph{}, format, 7 + cycle * 2, 7 + cycle * 2);
        renderer.render(graph, format, 8 + cycle * 2, 8 + cycle * 2);
        NEAR(pixel(delay->display()->outputs[0], 0, 0, 0), 0.f, 0.f);
    }
}

void testVariableDelayExactAgesAndChanges() {
    const Format format{1, 1, 30, 1};
    for (int age : {1, 2, 30, 60}) {
        auto source = std::make_shared<Node>(Kind::VideoIo);
        auto delay = std::make_shared<Node>(Kind::Delay);
        delay->params[kDelayFramesParam].store(static_cast<float>(age));
        Renderer renderer([] { return std::make_unique<TickValueBackend>(); });
        const Graph graph = delayGraph(source, delay);
        for (int tick = 0; tick < age; ++tick) {
            const auto report = renderer.render(graph, format, tick, tick / 30.0);
            CHECK(report.errors == 0);
            NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f);
            NEAR(pixel(delay->display()->outputs[0], 0, 0, 3), 0.f, 0.f);
        }
        renderer.render(graph, format, age, age / 30.0);
        NEAR(pixel(delay->display()->outputs[0], 0, 0), 1.f, 0.f);
        CHECK(delay->display()->outputs[0]->sequence == 0);
        renderer.render(graph, format, age + 1, (age + 1) / 30.0);
        NEAR(pixel(delay->display()->outputs[0], 0, 0), 2.f, 0.f);
        CHECK(delay->display()->outputs[0]->sequence == 1);
    }

    auto source = std::make_shared<Node>(Kind::VideoIo);
    auto delay = std::make_shared<Node>(Kind::Delay);
    delay->params[kDelayFramesParam].store(2.f);
    Renderer renderer([] { return std::make_unique<TickValueBackend>(); });
    const Graph graph = delayGraph(source, delay);
    for (uint64_t tick = 0; tick <= 2; ++tick)
        renderer.render(graph, format, tick, tick / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 1.f, 0.f);

    delay->params[kDelayFramesParam].store(4.f);
    renderer.render(graph, format, 3, .1);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f);
    CHECK(delay->display()->status.find("waiting for history") != std::string::npos);
    renderer.render(graph, format, 4, 4.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f);
    renderer.render(graph, format, 5, 5.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 2.f, 0.f); // tick 1 was still retained

    delay->params[kDelayFramesParam].store(1.f);
    renderer.render(graph, format, 6, .2);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 6.f, 0.f);
    delay->params[kDelayFramesParam].store(4.f);
    renderer.render(graph, format, 7, 7.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f); // shrink released tick 3
}

void testVariableDelayClearBypassAndDiscontinuities() {
    const Format format{1, 1, 30, 1};
    auto source = std::make_shared<Node>(Kind::VideoIo);
    auto delay = std::make_shared<Node>(Kind::Delay);
    delay->params[kDelayFramesParam].store(2.f);
    Renderer renderer([] { return std::make_unique<TickValueBackend>(); });
    const Graph graph = delayGraph(source, delay);
    for (uint64_t tick = 0; tick <= 2; ++tick)
        renderer.render(graph, format, tick, tick / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 1.f, 0.f);

    delay->params[kDelayClearParam].store(1.f);
    renderer.render(graph, format, 3, .1);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f);
    renderer.render(graph, format, 4, 4.0 / 30.0); // held button does not clear again
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f);
    renderer.render(graph, format, 5, 5.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 4.f, 0.f);
    delay->params[kDelayClearParam].store(0.f);
    renderer.render(graph, format, 6, .2);
    delay->params[kDelayClearParam].store(1.f);
    renderer.render(graph, format, 7, 7.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f);
    delay->params[kDelayClearParam].store(0.f);
    delay->resets.fetch_add(1);
    renderer.render(graph, format, 8, 8.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f);

    delay->params[kDelayFramesParam].store(1.f);
    delay->bypass.store(true);
    renderer.render(graph, format, 9, .3);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 10.f, 0.f);
    CHECK(delay->display()->outputs[0]->sequence == 9);
    delay->bypass.store(false);
    renderer.render(graph, format, 10, 10.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f);
    renderer.render(graph, format, 11, 11.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 11.f, 0.f);

    // A forward scheduling gap is represented as a gap, never a stale relabeled frame.
    renderer.render(graph, format, 13, 13.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f);
    renderer.render(graph, format, 14, 14.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 14.f, 0.f);

    // Repeated or backward ticks invalidate the capture epoch before reading it.
    renderer.render(graph, format, 14, 14.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f);
    renderer.render(graph, format, 12, .4);
    NEAR(pixel(delay->display()->outputs[0], 0, 0), 0.f, 0.f);

    auto missing = std::make_shared<Node>(Kind::Delay);
    missing->params[kDelayFramesParam].store(2.f);
    Renderer missingRenderer;
    Graph missingGraph{{missing}, {}, 2};
    for (uint64_t tick = 0; tick < 4; ++tick) {
        missingRenderer.render(missingGraph, format, tick, tick / 30.0);
        NEAR(pixel(missing->display()->outputs[0], 0, 0), 0.f, 0.f);
        NEAR(pixel(missing->display()->outputs[0], 0, 0, 3), 0.f, 0.f);
    }

    auto nonFiniteSource = std::make_shared<Node>(Kind::VideoIo);
    auto nonFiniteDelay = std::make_shared<Node>(Kind::Delay);
    nonFiniteDelay->params[kDelayFramesParam].store(
        std::numeric_limits<float>::quiet_NaN());
    Renderer nonFiniteRenderer([] { return std::make_unique<TickValueBackend>(); });
    const Graph nonFiniteGraph = delayGraph(nonFiniteSource, nonFiniteDelay);
    auto report = nonFiniteRenderer.render(nonFiniteGraph, format, 0, 0);
    CHECK(report.errors == 1);
    CHECK(nonFiniteDelay->display()->status.find("non-finite parameter") != std::string::npos);
    report = nonFiniteRenderer.render(nonFiniteGraph, format, 1, 1.0 / 30.0);
    CHECK(report.errors == 1);
    NEAR(pixel(nonFiniteDelay->display()->outputs[0], 0, 0), 1.f, 0.f);
}

void testVariableDelayReadBeforeCommitChain() {
    const Format format{1, 1, 30, 1};
    auto source = std::make_shared<Node>(Kind::VideoIo);
    auto first = std::make_shared<Node>(Kind::Delay);
    auto second = std::make_shared<Node>(Kind::Delay);
    Renderer renderer([] { return std::make_unique<TickValueBackend>(); });
    // Reverse node order ensures correctness does not depend on one delay being visited first.
    Graph graph{{second, first, source},
                {connect(source, 0, first, 0), connect(first, 0, second, 0)}, 1};
    renderer.render(graph, format, 0, 0);
    renderer.render(graph, format, 1, 1.0 / 30.0);
    NEAR(pixel(first->display()->outputs[0], 0, 0), 1.f, 0.f);
    NEAR(pixel(second->display()->outputs[0], 0, 0), 0.f, 0.f);
    renderer.render(graph, format, 2, 2.0 / 30.0);
    NEAR(pixel(first->display()->outputs[0], 0, 0), 2.f, 0.f);
    NEAR(pixel(second->display()->outputs[0], 0, 0), 1.f, 0.f);
    CHECK(second->display()->outputs[0]->sequence == 0);
}

void testVariableDelayMemoryBudgetAndUniqueReporting() {
    const Format tiny{1, 1, 30, 1};
    auto source = std::make_shared<Node>(Kind::TestImage);
    auto first = std::make_shared<Node>(Kind::Delay);
    auto second = std::make_shared<Node>(Kind::Delay);
    first->params[kDelayFramesParam].store(2.f);
    second->params[kDelayFramesParam].store(2.f);
    Renderer oneRenderer;
    auto one = oneRenderer.render(
        Graph{{source, first}, {connect(source, 0, first, 0)}, 1}, tiny, 0, 0);
    Renderer twoRenderer;
    auto two = twoRenderer.render(
        Graph{{source, first, second},
              {connect(source, 0, first, 0), connect(source, 0, second, 0)}, 2}, tiny, 0, 0);
    CHECK(one.errors == 0 && two.errors == 0);
    CHECK(two.frameBytes == one.frameBytes); // shared output/history pointers are charged once

    const Format boundary{1024, 1024, 30, 1};
    auto largeSource = std::make_shared<Node>(Kind::TestImage);
    auto largeDelay = std::make_shared<Node>(Kind::Delay);
    Graph largeGraph{{largeSource, largeDelay}, {connect(largeSource, 0, largeDelay, 0)}, 3};
    Renderer boundaryRenderer;
    largeDelay->params[kDelayFramesParam].store(26.f);
    auto report = boundaryRenderer.render(largeGraph, boundary, 0, 0);
    CHECK(report.errors == 0);
    largeDelay->params[kDelayFramesParam].store(27.f);
    report = boundaryRenderer.render(largeGraph, boundary, 1, 1.0 / 30.0);
    CHECK(report.errors == 1);
    CHECK(report.frameBytes == 16ull * 1024ull * 1024ull); // actual retained tick-0 image
    CHECK(largeDelay->display()->status.find("512 MiB") != std::string::npos);
    largeDelay->params[kDelayFramesParam].store(1.f);
    report = boundaryRenderer.render(largeGraph, boundary, 2, 2.0 / 30.0);
    CHECK(report.errors == 0);
    CHECK(largeDelay->display()->status.find("512 MiB") == std::string::npos);

    auto otherDelay = std::make_shared<Node>(Kind::Delay);
    largeDelay->params[kDelayFramesParam].store(13.f);
    otherDelay->params[kDelayFramesParam].store(13.f);
    Graph multiple{{largeSource, largeDelay, otherDelay},
                   {connect(largeSource, 0, largeDelay, 0),
                    connect(largeSource, 0, otherDelay, 0)}, 4};
    report = boundaryRenderer.render(multiple, boundary, 3, .1);
    CHECK(report.errors == 0);
    largeDelay->params[kDelayFramesParam].store(14.f);
    otherDelay->params[kDelayFramesParam].store(14.f);
    report = boundaryRenderer.render(multiple, boundary, 4, 4.0 / 30.0);
    CHECK(report.errors == 1);
    boundaryRenderer.render(Graph{{largeSource}, {}, 5}, boundary, 5, 5.0 / 30.0);
    report = boundaryRenderer.render(largeGraph, boundary, 6, .2);
    CHECK(report.errors == 0); // removal reclaimed the other delay's reservation and history

    std::vector<std::shared_ptr<Node>> bridges;
    for (int i = 0; i < 400; ++i) bridges.push_back(std::make_shared<Node>(Kind::CvBridge));
    Renderer bridgeBudgetRenderer;
    report = bridgeBudgetRenderer.render(Graph{bridges, {}, 6}, tiny, 0, 0);
    CHECK(report.errors == 1);
    CHECK(bridges.front()->display()->status.find("512 MiB") != std::string::npos);
}

void testCvAudioEpochAndTriggers() {
    Format format{4, 1, 10, 1};
    auto bridge = std::make_shared<Node>(Kind::CvBridge);
    Renderer renderer;
    Graph graph{{bridge}, {}, 1};

    bridge->cvVoltage.store(10.f);
    bridge->params[2].store(-0.25f);
    renderer.render(graph, format, 0, 0);
    for (float value : bridge->display()->outputs[0]->pixels) NEAR(value, .75f, 1e-6f);

    bridge->params[0].store(1.f);
    bridge->params[2].store(0.f);
    CHECK(bridge->audio.push({0.8, 8.f, 1}));
    CHECK(bridge->audio.push({0.9, 9.f, 1}));
    renderer.render(graph, format, 10, 1.0); // renders completed [0.8, 0.9]
    const FramePtr audio = bridge->display()->outputs[0];
    NEAR(audio->pixels[0], .8f, 1e-5f);
    NEAR(audio->pixels[1], .8333333f, 1e-5f);
    NEAR(audio->pixels[3], .9f, 1e-5f);

    CHECK(bridge->audio.push({0.8, 2.f, 2})); // new epoch clears prior history
    CHECK(bridge->audio.push({0.9, 4.f, 2}));
    renderer.render(graph, format, 11, 1.0);
    NEAR(bridge->display()->outputs[0]->pixels[0], .2f, 1e-5f);
    NEAR(bridge->display()->outputs[0]->pixels[3], .4f, 1e-5f);

    CHECK(bridge->audio.push({0.0, 5.f, 3}));
    CHECK(bridge->audio.push({0.1, 10.f, 3}));
    renderer.render(graph, format, 12, 100.0); // late-created bridge maps its local epoch to video time
    NEAR(bridge->display()->outputs[0]->pixels[0], .5f, 1e-5f);
    NEAR(bridge->display()->outputs[0]->pixels[3], 1.f, 1e-5f);

    CHECK(bridge->audio.push({0.0, 10.f, 4})); // reset/restart establishes a fresh mapping
    CHECK(bridge->audio.push({0.1, 0.f, 4}));
    renderer.render(graph, format, 13, 200.0);
    NEAR(bridge->display()->outputs[0]->pixels[0], 1.f, 1e-5f);
    NEAR(bridge->display()->outputs[0]->pixels[3], 0.f, 1e-5f);

    CHECK(bridge->audio.push({0.0, 2.f, 5}));
    CHECK(bridge->audio.push({0.05, 6.f, 5}));
    renderer.render(graph, Format{4, 1, 20, 1}, 14, 300.0); // rate change remaps raster history
    NEAR(bridge->display()->outputs[0]->pixels[0], .2f, 1e-5f);
    NEAR(bridge->display()->outputs[0]->pixels[3], .6f, 1e-5f);

    bridge->params[0].store(2.f);
    bridge->triggers.fetch_add(3);
    for (int tick = 0; tick < 4; ++tick) {
        renderer.render(graph, format, 20 + tick, 2.0 + tick * .1);
        NEAR(bridge->display()->outputs[0]->pixels[0], tick < 3 ? 1.f : 0.f, 0.f);
    }
    bridge->triggers.fetch_add(5000);
    renderer.render(graph, format, 30, 3.0);
    CHECK(bridge->display()->status.find("trigger overflow") != std::string::npos);

    bridge->triggers.store(3);
    renderer.render(graph, format, 31, 3.1);
    NEAR(bridge->display()->outputs[0]->pixels[0], 1.f, 0.f);
    bridge->resets.store(1);
    renderer.render(graph, format, 32, 3.2);
    NEAR(bridge->display()->outputs[0]->pixels[0], 0.f, 0.f);
    CHECK(bridge->resets.load() == 0);
    renderer.render(graph, format, 33, 3.3);
    NEAR(bridge->display()->outputs[0]->pixels[0], 0.f, 0.f);

    bridge->triggers.store(3);
    renderer.render(graph, format, 34, 3.4); // leaves two pending events
    bridge->triggers.fetch_add(2);
    CHECK(bridge->audio.push({0.0, 1.f, 6}));
    renderer.render(graph, format, 35, 3.5); // new audio epoch clears stale triggers
    NEAR(bridge->display()->outputs[0]->pixels[0], 0.f, 0.f);
    renderer.render(graph, format, 36, 3.6);
    NEAR(bridge->display()->outputs[0]->pixels[0], 0.f, 0.f);
}

void testCvBypassAndAudioOverflow() {
    Format format{2, 1, 30, 1};
    auto bridge = std::make_shared<Node>(Kind::CvBridge);
    bridge->params[0].store(2.f);
    Renderer renderer;
    Graph graph{{bridge}, {}, 1};
    bridge->triggers.store(3);
    renderer.render(graph, format, 0, 0);
    NEAR(bridge->display()->outputs[0]->pixels[0], 1.f, 0.f);
    bridge->bypass.store(true);
    bridge->triggers.fetch_add(2);
    CHECK(bridge->audio.push({0.0, 5.f, 1}));
    renderer.render(graph, format, 1, 1.0 / 30.0);
    NEAR(bridge->display()->outputs[0]->pixels[0], 0.f, 0.f);
    CHECK(bridge->display()->status.find("bypassed") != std::string::npos);
    bridge->bypass.store(false);
    renderer.render(graph, format, 2, 2.0 / 30.0);
    NEAR(bridge->display()->outputs[0]->pixels[0], 0.f, 0.f);

    auto overflow = std::make_shared<Node>(Kind::CvBridge);
    overflow->params[0].store(1.f);
    for (int i = 0; i < 32770; ++i)
        overflow->audio.push({i / 48000.0, 1.f, 1});
    renderer.render(Graph{{overflow}, {}, 2}, format, 3, 0.1);
    CHECK(overflow->audio.dropped.load() == 3);
    CHECK(overflow->display()->status.find("audio queue dropped 3") != std::string::npos);
    renderer.render(Graph{{overflow}, {}, 2}, format, 4, 0.2);
    CHECK(overflow->display()->status.find("audio queue dropped 3") != std::string::npos);

    auto retained = std::make_shared<Node>(Kind::CvBridge);
    Graph retainedGraph{{retained}, {}, 3};
    for (int batch = 0; batch < 3; ++batch) {
        for (int i = 0; i < 32767; ++i)
            CHECK(retained->audio.push({(batch * 32767 + i) / 48000.0, 1.f, 1}));
        renderer.render(retainedGraph, format, 5 + batch, 0.3 + batch / 30.0);
    }
    CHECK(retained->audio.dropped.load() == 0);
    CHECK(retained->display()->status.find("audio history evicted 32765") != std::string::npos);
}

void testCvClockDriftRecovery() {
    const Format format{4, 1, 10, 1};
    auto pushInterval = [](const std::shared_ptr<Node>& bridge, double begin, double end,
                           uint64_t epoch, int samples = 10) {
        for (int sample = 1; sample <= samples; ++sample) {
            const double amount = static_cast<double>(sample) / samples;
            CHECK(bridge->audio.push({begin + (end - begin) * amount, 1.f, epoch}));
        }
    };

    {
        auto warm = std::make_shared<Node>(Kind::CvBridge);
        warm->params[0].store(1.f);
        warm->params[1].store(1.f);
        Renderer warmRenderer;
        Graph warmGraph{{warm}, {}, 1};
        int nextSample = 0;
        for (; nextSample < 2000; ++nextSample)
            CHECK(warm->audio.push({nextSample / 48000.0, 1.f, 20}));
        warmRenderer.render(warmGraph, Format{4, 1, 30000, 1001}, 0, 0);
        for (float value : warm->display()->outputs[0]->pixels) NEAR(value, 1.f, 1e-5f);
        warmRenderer.render(warmGraph, Format{4, 1, 30000, 1001}, 1,
                            1001.0 / 30000.0);
        warmRenderer.render(warmGraph, Format{4, 1, 30000, 1001}, 2,
                            2.0 * 1001.0 / 30000.0);
        CHECK(warm->audio.push({nextSample++ / 48000.0, 20.f, 20}));
        warmRenderer.render(warmGraph, Format{4, 1, 30000, 1001}, 3,
                            3.0 * 1001.0 / 30000.0);
        for (float value : warm->display()->outputs[0]->pixels) NEAR(value, 0.f, 0.f);
        for (int added = 0; added < 1602; ++added, ++nextSample)
            CHECK(warm->audio.push({nextSample / 48000.0, 20.f, 20}));
        warmRenderer.render(warmGraph, Format{4, 1, 30000, 1001}, 4,
                            4.0 * 1001.0 / 30000.0);
        for (float value : warm->display()->outputs[0]->pixels) NEAR(value, 20.f, 1e-4f);
        warmRenderer.render(warmGraph, Format{4, 1, 30000, 1001}, 5,
                            5.0 * 1001.0 / 30000.0);
        for (float value : warm->display()->outputs[0]->pixels) NEAR(value, 0.f, 0.f);
    }

    for (int initialBatch : {800, 1600}) {
        auto cold = std::make_shared<Node>(Kind::CvBridge);
        cold->params[0].store(1.f);
        cold->params[1].store(1.f);
        Renderer coldRenderer;
        Graph coldGraph{{cold}, {}, 1};
        int sampleIndex = 0;
        for (; sampleIndex < initialBatch; ++sampleIndex)
            CHECK(cold->audio.push({sampleIndex / 48000.0, 1.f, 10}));
        coldRenderer.render(coldGraph, Format{4, 1, 30000, 1001}, 0, 0);
        for (float value : cold->display()->outputs[0]->pixels) NEAR(value, 0.f, 0.f);
        coldRenderer.render(coldGraph, Format{4, 1, 30000, 1001}, 1,
                            1001.0 / 30000.0); // empty capture tick preserves the short batch
        for (float value : cold->display()->outputs[0]->pixels) NEAR(value, 0.f, 0.f);
        for (int added = 0; added < 1600; ++added, ++sampleIndex)
            CHECK(cold->audio.push({sampleIndex / 48000.0, 1.f, 10}));
        coldRenderer.render(coldGraph, Format{4, 1, 30000, 1001}, 2,
                            2.0 * 1001.0 / 30000.0);
        for (float value : cold->display()->outputs[0]->pixels) NEAR(value, 1.f, 1e-5f);
        coldRenderer.render(coldGraph, Format{4, 1, 30000, 1001}, 3,
                            3.0 * 1001.0 / 30000.0); // no new capture: never replay stale data
        for (float value : cold->display()->outputs[0]->pixels) NEAR(value, 0.f, 0.f);
    }

    auto slower = std::make_shared<Node>(Kind::CvBridge);
    slower->params[0].store(1.f);
    slower->params[1].store(1.f);
    Renderer slowerRenderer;
    Graph slowerGraph{{slower}, {}, 1};
    CHECK(slower->audio.push({0.0, 1.f, 1}));
    pushInterval(slower, 0.0, 0.1, 1);
    slowerRenderer.render(slowerGraph, format, 0, 10.0);
    double producerTime = 0.1;
    for (int tick = 1; tick <= 30; ++tick) {
        const double next = producerTime + .099; // producer clock is 1% slower
        pushInterval(slower, producerTime, next, 1, 7 + (tick % 3) * 3);
        slowerRenderer.render(slowerGraph, format, tick, 10.0 + tick * .1);
        NEAR(slower->display()->outputs[0]->pixels[0], 1.f, 1e-5f);
        producerTime = next;
    }
    CHECK(slower->display()->status.find("audio clock reanchored") != std::string::npos);
    slowerRenderer.render(slowerGraph, format, 31, 13.1); // producer stopped: do not replay
    for (float value : slower->display()->outputs[0]->pixels) NEAR(value, 0.f, 0.f);

    auto faster = std::make_shared<Node>(Kind::CvBridge);
    faster->params[0].store(1.f);
    faster->params[1].store(1.f);
    Renderer fasterRenderer;
    Graph fasterGraph{{faster}, {}, 1};
    CHECK(faster->audio.push({0.0, 1.f, 2}));
    pushInterval(faster, 0.0, 0.1, 2);
    fasterRenderer.render(fasterGraph, format, 0, 20.0);
    producerTime = 0.1;
    for (int tick = 1; tick <= 130; ++tick) {
        const double next = producerTime + .101; // producer clock is 1% faster
        pushInterval(faster, producerTime, next, 2, 6 + (tick % 4) * 2);
        fasterRenderer.render(fasterGraph, format, tick, 20.0 + tick * .1);
        NEAR(faster->display()->outputs[0]->pixels[0], 1.f, 1e-5f);
        producerTime = next;
    }
    CHECK(faster->display()->status.find("audio clock reanchored") != std::string::npos);
    CHECK(faster->display()->status.find("audio history evicted") == std::string::npos);
}

void testImageNodeBypass() {
    Format format{2, 1, 30, 1};
    auto source = std::make_shared<Node>(Kind::TestImage);
    auto processor = std::make_shared<Node>(Kind::Processor);
    auto delay = std::make_shared<Node>(Kind::Delay);
    auto monitor = std::make_shared<Node>(Kind::Monitor);
    source->params[0].store(2.f);
    processor->params[0].store(9.f);
    processor->params[2].store(4.f);
    processor->params[3].store(2.f);
    processor->bypass.store(true);
    monitor->bypass.store(true);
    Graph graph{{source, processor, delay, monitor},
                {connect(source, 0, processor, 0), connect(source, 0, delay, 0),
                 connect(processor, 0, monitor, 0)}, 1};
    Renderer renderer;
    auto report = renderer.render(graph, format, 0, 0);
    CHECK(report.errors == 0);
    NEAR(pixel(processor->display()->outputs[0], 1, 0, 0), 1.f, 0.f);
    NEAR(pixel(processor->display()->outputs[1], 1, 0), 1.f, 0.f);
    CHECK(processor->display()->status.find("bypassed") != std::string::npos);
    for (float value : monitor->display()->preview->pixels) NEAR(value, 0.f, 0.f);
    CHECK(monitor->display()->status.find("bypassed") != std::string::npos);

    delay->bypass.store(true);
    renderer.render(graph, format, 1, 1.0 / 30.0);
    NEAR(pixel(delay->display()->outputs[0], 1, 0, 0), 1.f, 0.f);
    CHECK(delay->display()->status.find("bypassed") != std::string::npos);

    source->bypass.store(true);
    renderer.render(graph, format, 2, 2.0 / 30.0);
    for (float value : source->display()->outputs[0]->pixels) NEAR(value, 0.f, 0.f);
    for (float value : source->display()->outputs[1]->pixels) NEAR(value, 0.f, 0.f);
    CHECK(source->display()->status.find("bypassed") != std::string::npos);
}

struct BackendState {
    FramePtr incoming;
    FramePtr published;
    std::string availableSourceId;
    std::string availableSourceApplication;
    std::string availableSourceName;
    std::string lastPublisherName;
    bool returnFrame = true;
    bool serverActive = false;
    int receives = 0;
    int publishes = 0;
    bool lastPublishEnabled = false;
    bool lastPublishHadFrame = false;
};

class FakeBackend : public VideoBackend {
public:
    explicit FakeBackend(std::shared_ptr<BackendState> value) : state(std::move(value)) {}
    std::vector<VideoSource> sources() override { return {{"id", "app", "source"}}; }
    FramePtr receive(const IoSettings& settings, const Format&, uint64_t, double) override {
        ++state->receives;
        return state->returnFrame && settings.sourceId == state->availableSourceId &&
            settings.sourceApplication == state->availableSourceApplication &&
            settings.sourceName == state->availableSourceName
            ? state->incoming : FramePtr{};
    }
    void publish(const IoSettings& settings, FramePtr frame) override {
        ++state->publishes;
        state->serverActive = settings.publish;
        state->lastPublishEnabled = settings.publish;
        state->lastPublishHadFrame = frame != nullptr;
        state->lastPublisherName = settings.publisherName;
        state->published = std::move(frame);
    }
    std::string status() const override { return "ready"; }
private:
    std::shared_ptr<BackendState> state;
};

void testBackendBoundaryAndHold() {
    Format format{2, 2, 30, 1};
    auto state = std::make_shared<BackendState>();
    auto incoming = std::make_shared<Frame>();
    incoming->width = incoming->height = 1;
    incoming->channels = 3;
    incoming->pixels = {-.5f, .5f, 1.5f};
    state->incoming = incoming;
    Renderer renderer([state] { return std::make_unique<FakeBackend>(state); });
    auto io = std::make_shared<Node>(Kind::VideoIo);
    auto processor = std::make_shared<Node>(Kind::Processor);
    IoSettings settings;
    settings.publish = true;
    settings.holdLast = true;
    io->setIoSettings(settings);
    Graph graph{{io, processor},
                {connect(io, 0, processor, 0), connect(processor, 0, io, 0)}, 1};
    auto report = renderer.render(graph, format, 0, 0);
    CHECK(report.errors == 0 && state->receives == 1 && state->publishes == 1);
    auto output = io->display()->outputs[0];
    NEAR(pixel(output, 1, 1, 0), -.5f, 0.f); // receive conversion preserves signed values
    NEAR(pixel(output, 1, 1, 2), 1.5f, 0.f);
    NEAR(pixel(output, 1, 1, 3), 1.f, 0.f); // RGB receives explicit straight alpha
    for (float value : state->published->pixels) CHECK(value >= 0.f && value <= 1.f);
    NEAR(pixel(state->published, 0, 0, 0), 0.f, 0.f);
    NEAR(pixel(state->published, 0, 0, 1), .5f, 0.f);
    NEAR(pixel(state->published, 0, 0, 2), 1.f, 0.f);
    CHECK(io->display()->sources.size() == 1 && io->display()->status == "ready");

    const int receivesBeforeBypass = state->receives;
    const int publishesBeforeBypass = state->publishes;
    io->bypass.store(true);
    renderer.render(graph, format, 1, 1.0 / 30.0);
    CHECK(state->receives == receivesBeforeBypass);
    CHECK(state->publishes == publishesBeforeBypass + 1);
    CHECK(!state->lastPublishEnabled && !state->lastPublishHadFrame);
    for (float value : io->display()->outputs[0]->pixels) NEAR(value, 0.f, 0.f);
    CHECK(io->display()->status.find("bypassed") != std::string::npos);
    io->bypass.store(false);

    state->returnFrame = false;
    renderer.render(graph, format, 2, 2.0 / 30.0);
    NEAR(pixel(io->display()->outputs[0], 0, 0, 0), -.5f, 0.f);
    renderer.render(graph, Format{4, 1, 30, 1}, 3, 3.0 / 30.0);
    CHECK(io->display()->outputs[0]->width == 4);
    for (float value : io->display()->outputs[0]->pixels) NEAR(value, 0.f, 0.f);
    settings.holdLast = false;
    io->setIoSettings(settings);
    renderer.render(graph, format, 4, 4.0 / 30.0);
    NEAR(pixel(io->display()->outputs[0], 0, 0, 0), 0.f, 0.f);
}

void testIoSettingTransitions() {
    const Format format{1, 1, 30, 1};
    auto state = std::make_shared<BackendState>();
    auto incoming = std::make_shared<Frame>();
    incoming->width = incoming->height = 1;
    incoming->channels = 4;
    incoming->pixels = {.75f, .75f, .75f, 1.f};
    state->incoming = incoming;
    state->availableSourceId = "id-A";
    state->availableSourceApplication = "app";
    state->availableSourceName = "A";

    Renderer renderer([state] { return std::make_unique<FakeBackend>(state); });
    auto io = std::make_shared<Node>(Kind::VideoIo);
    Graph graph{{io}, {}, 1};
    IoSettings settings;
    settings.sourceId = "id-A";
    settings.sourceApplication = "app";
    settings.sourceName = "A";
    settings.publisherName = "first";
    settings.publish = true;
    settings.holdLast = true;
    io->setIoSettings(settings);

    renderer.render(graph, format, 0, 0.0);
    CHECK(state->publishes == 1 && state->lastPublishEnabled && state->lastPublishHadFrame);
    NEAR(pixel(io->display()->outputs[0], 0, 0), .75f, 0.f);

    // Publisher-only settings do not invalidate a held frame from the same source.
    state->returnFrame = false;
    settings.publisherName = "second";
    settings.publish = false;
    io->setIoSettings(settings);
    renderer.render(graph, format, 1, 1.0 / 30.0);
    CHECK(state->publishes == 2 && !state->lastPublishEnabled && !state->lastPublishHadFrame);
    CHECK(state->lastPublisherName == "second");
    NEAR(pixel(io->display()->outputs[0], 0, 0), .75f, 0.f);

    settings.publish = true;
    io->setIoSettings(settings);
    renderer.render(graph, format, 2, 2.0 / 30.0);
    CHECK(state->publishes == 3 && state->lastPublishEnabled && state->lastPublishHadFrame);
    CHECK(state->lastPublisherName == "second");
    NEAR(pixel(io->display()->outputs[0], 0, 0), .75f, 0.f);

    // A UUID change identifies a new selection even when its labels match.
    settings.sourceId = "id-B";
    io->setIoSettings(settings);
    renderer.render(graph, format, 3, 3.0 / 30.0);
    NEAR(pixel(io->display()->outputs[0], 0, 0), 0.f, 0.f);

    state->availableSourceId = "id-B";
    state->returnFrame = true;
    incoming->pixels[0] = .25f;
    renderer.render(graph, format, 4, 4.0 / 30.0);
    NEAR(pixel(io->display()->outputs[0], 0, 0), .25f, 0.f);

    // Missing input may hold the last frame only while the selection stays the same.
    state->returnFrame = false;
    renderer.render(graph, format, 5, 5.0 / 30.0);
    NEAR(pixel(io->display()->outputs[0], 0, 0), .25f, 0.f);
    settings.sourceApplication = "other app";
    io->setIoSettings(settings);
    renderer.render(graph, format, 6, 6.0 / 30.0);
    NEAR(pixel(io->display()->outputs[0], 0, 0), 0.f, 0.f);

    state->availableSourceApplication = "other app";
    state->returnFrame = true;
    incoming->pixels[0] = .5f;
    renderer.render(graph, format, 7, 7.0 / 30.0);
    NEAR(pixel(io->display()->outputs[0], 0, 0), .5f, 0.f);

    state->returnFrame = false;
    renderer.render(graph, format, 8, 8.0 / 30.0);
    NEAR(pixel(io->display()->outputs[0], 0, 0), .5f, 0.f);
    settings.sourceName = "B";
    io->setIoSettings(settings);
    renderer.render(graph, format, 9, 9.0 / 30.0);
    NEAR(pixel(io->display()->outputs[0], 0, 0), 0.f, 0.f);
}

void testRejectedGraphPublisherTeardown() {
    const Format format{720, 480, 30000, 1001};
    auto state = std::make_shared<BackendState>();
    Renderer renderer([state] { return std::make_unique<FakeBackend>(state); });
    auto io = std::make_shared<Node>(Kind::VideoIo);
    IoSettings settings;
    settings.publish = true;
    io->setIoSettings(settings);
    Graph valid{{io}, {}, 1};

    CHECK(renderer.render(valid, format, 0, 0.0).errors == 0);
    CHECK(state->serverActive && state->publishes == 1 && state->receives == 1);

    Graph rejected = valid;
    for (int i = 0; i < 80; ++i)
        rejected.nodes.push_back(std::make_shared<Node>(Kind::TestImage));
    const int receivesBeforeRejection = state->receives;
    const int publishesBeforeRejection = state->publishes;
    CHECK(renderer.render(rejected, format, 1, 1.0 / 30.0).errors == 1);
    CHECK(state->serverActive && state->receives == receivesBeforeRejection);
    CHECK(state->publishes == publishesBeforeRejection);
    CHECK(!rejected.nodes.back()->display()->outputs[0]);

    settings.publish = false;
    io->setIoSettings(settings);
    CHECK(renderer.render(rejected, format, 2, 2.0 / 30.0).errors == 1);
    CHECK(!state->serverActive && state->receives == receivesBeforeRejection);
    CHECK(state->publishes == publishesBeforeRejection + 1);
    CHECK(!state->lastPublishEnabled && !state->lastPublishHadFrame);

    // Repeating a rejected disabled graph does not issue redundant stop calls.
    CHECK(renderer.render(rejected, format, 3, 3.0 / 30.0).errors == 1);
    CHECK(state->publishes == publishesBeforeRejection + 1);
    CHECK(state->receives == receivesBeforeRejection);

    settings.publish = true;
    io->setIoSettings(settings);
    CHECK(renderer.render(valid, format, 4, 4.0 / 30.0).errors == 0);
    CHECK(state->serverActive && state->receives == receivesBeforeRejection + 1);
    CHECK(state->publishes == publishesBeforeRejection + 2);

    io->bypass.store(true);
    const int receivesBeforeBypass = state->receives;
    CHECK(renderer.render(rejected, format, 5, 5.0 / 30.0).errors == 1);
    CHECK(!state->serverActive && state->receives == receivesBeforeBypass);
    CHECK(state->publishes == publishesBeforeRejection + 3);
    CHECK(renderer.render(rejected, format, 6, 6.0 / 30.0).errors == 1);
    CHECK(state->publishes == publishesBeforeRejection + 3);

    io->bypass.store(false);
    CHECK(renderer.render(valid, format, 7, 7.0 / 30.0).errors == 0);
    CHECK(state->serverActive && state->receives == receivesBeforeBypass + 1);
    CHECK(state->publishes == publishesBeforeRejection + 4);
}

struct ThreadBackendState {
    std::atomic<int> created{0}, calls{0}, destroyed{0}, violations{0};
    int initialStallMilliseconds = 0; // Set before worker start.
    std::vector<uint64_t> receivedTicks; // Worker writes; inspect only after stop/join.
};

class ThreadBackend : public VideoBackend {
public:
    explicit ThreadBackend(std::shared_ptr<ThreadBackendState> value)
        : state(std::move(value)), owner(std::this_thread::get_id()) { ++state->created; }
    ~ThreadBackend() override {
        if (std::this_thread::get_id() != owner) ++state->violations;
        ++state->destroyed;
    }
    std::vector<VideoSource> sources() override { verify(); return {}; }
    FramePtr receive(const IoSettings&, const Format&, uint64_t tick, double) override {
        verify();
        if (state->receivedTicks.empty() && state->initialStallMilliseconds > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(state->initialStallMilliseconds));
        state->receivedTicks.push_back(tick);
        ++state->calls;
        return {};
    }
    void publish(const IoSettings&, FramePtr) override { verify(); }
    std::string status() const override {
        if (std::this_thread::get_id() != owner) ++state->violations;
        return "ready";
    }
private:
    void verify() { if (std::this_thread::get_id() != owner) ++state->violations; }
    std::shared_ptr<ThreadBackendState> state;
    std::thread::id owner;
};

void waitForCalls(const std::shared_ptr<ThreadBackendState>& state, int expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (state->calls.load() < expected && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

bool waitForWorkerReady(Engine& engine, uint64_t previousRun = 0) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline) {
        const EngineStats stats = engine.stats();
        if (stats.workerRun != 0 && stats.workerRun != previousRun && stats.ticks > 0)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void testBackendWorkerLifetime() {
    auto state = std::make_shared<ThreadBackendState>();
    auto io = std::make_shared<Node>(Kind::VideoIo);
    Engine engine(Format{2, 1, 100, 1},
                  [state] { return std::make_unique<ThreadBackend>(state); });
    engine.submit(Graph{{io}, {}, 1});
    engine.start();
    waitForCalls(state, 1);
    engine.stop();
    CHECK(state->created.load() == 1 && state->destroyed.load() == 1);
    CHECK(state->violations.load() == 0);

    const int previousCalls = state->calls.load();
    engine.start();
    waitForCalls(state, previousCalls + 1);
    engine.stop();
    CHECK(state->created.load() == 2 && state->destroyed.load() == 2);
    CHECK(state->violations.load() == 0);
}

void testEngineIndependentClock(int initialStallMilliseconds) {
    Format format{4, 2, 50, 1};
    auto source = std::make_shared<Node>(Kind::TestImage);
    auto io = std::make_shared<Node>(Kind::VideoIo);
    auto state = std::make_shared<ThreadBackendState>();
    state->initialStallMilliseconds = initialStallMilliseconds;
    Engine engine(format, [state] { return std::make_unique<ThreadBackend>(state); });
    engine.submit(Graph{{source, io}, {}, 1});
    engine.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (engine.stats().ticks < 7 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    engine.stop();
    const EngineStats stats = engine.stats();
    CHECK(stats.ticks >= 7);
    CHECK(stats.workerRun != 0);
    CHECK(stats.renderMilliseconds.count == stats.ticks);
    CHECK(stats.renderErrors == 0 && stats.renderErrorFrames == 0);
    CHECK(state->receivedTicks.size() == stats.ticks);
    CHECK(!state->receivedTicks.empty());
    if (!state->receivedTicks.empty()) {
        CHECK(state->receivedTicks.front() == 0);
        for (size_t i = 1; i < state->receivedTicks.size(); ++i)
            CHECK(state->receivedTicks[i] > state->receivedTicks[i - 1]);
        const auto displayed = source->display();
        CHECK(displayed && displayed->tick == state->receivedTicks.back());
        // Completed renders exclude skipped scheduled ticks. The worker can also
        // record skips after its final render, before stop() joins it.
        const uint64_t scheduledThroughDisplay = state->receivedTicks.back() + 1;
        CHECK(scheduledThroughDisplay >= stats.ticks);
        CHECK(scheduledThroughDisplay <= stats.ticks + stats.skippedTicks);
        if (initialStallMilliseconds > 0) {
            CHECK(stats.skippedTicks > 0);
            CHECK(scheduledThroughDisplay > stats.ticks);
        }
    }
    CHECK(stats.frameBytes > 0 && stats.maxMilliseconds >= stats.lastMilliseconds);
    CHECK(stats.maxMilliseconds == stats.renderMilliseconds.maximumMilliseconds);
    CHECK(stats.renderMilliseconds.quantile(.50) <= stats.renderMilliseconds.quantile(.95));
    CHECK(stats.renderMilliseconds.quantile(.95) <= stats.renderMilliseconds.quantile(.99));
    CHECK(stats.lateFrames == stats.renderDeadlineMisses + stats.skippedTicks);
    CHECK(engine.format().rateNumerator == 50);

    engine.start();
    CHECK(waitForWorkerReady(engine, stats.workerRun));
    engine.stop();
    const EngineStats restarted = engine.stats();
    CHECK(restarted.workerRun != stats.workerRun);
    CHECK(restarted.ticks > 0 && restarted.renderMilliseconds.count == restarted.ticks);
}

void testWorkerRunIdentifiers() {
    Engine first(Format{1, 1, 100, 1});
    Engine second(Format{1, 1, 100, 1});
    first.start();
    second.start();
    CHECK(waitForWorkerReady(first));
    CHECK(waitForWorkerReady(second));
    first.stop();
    second.stop();
    const EngineStats firstRun = first.stats();
    const EngineStats secondRun = second.stats();
    CHECK(firstRun.workerRun != 0 && secondRun.workerRun != 0);
    CHECK(firstRun.workerRun != secondRun.workerRun);

    first.start();
    CHECK(waitForWorkerReady(first, firstRun.workerRun));
    first.stop();
    const EngineStats restarted = first.stats();
    CHECK(restarted.workerRun != firstRun.workerRun);
    CHECK(restarted.workerRun != secondRun.workerRun);
    CHECK(restarted.ticks > 0);
}

void testEngineReportAggregation() {
    Engine engine(Format{0, 1, 200, 1});
    engine.submit(Graph{{std::make_shared<Node>(Kind::TestImage)}, {}, 1});
    engine.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (engine.stats().ticks < 5 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const EngineStats beforeTopologyChange = engine.stats();
    engine.submit(Graph{});
    while (engine.stats().ticks < beforeTopologyChange.ticks + 3 &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    engine.stop();
    const EngineStats stats = engine.stats();
    CHECK(stats.workerRun == beforeTopologyChange.workerRun);
    CHECK(stats.ticks >= beforeTopologyChange.ticks + 3);
    CHECK(stats.renderMilliseconds.count == stats.ticks);
    CHECK(stats.renderErrors == stats.ticks);
    CHECK(stats.renderErrorFrames == stats.ticks);
    CHECK(stats.maxMilliseconds == stats.renderMilliseconds.maximumMilliseconds);

    engine.start();
    const auto restartDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while ((engine.stats().workerRun == stats.workerRun || engine.stats().ticks < 3) &&
           std::chrono::steady_clock::now() < restartDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    engine.stop();
    const EngineStats restarted = engine.stats();
    CHECK(restarted.workerRun != stats.workerRun);
    CHECK(restarted.ticks >= 3);
    CHECK(restarted.renderErrors == restarted.ticks);
    CHECK(restarted.renderErrorFrames == restarted.ticks);
}
struct RecorderFixture {
    Format format{4, 2, 10, 1};
    Renderer renderer;
    std::shared_ptr<Node> source = std::make_shared<Node>(Kind::TestImage);
    std::shared_ptr<Node> recorder = std::make_shared<Node>(Kind::Recorder);
    Graph graph{{recorder, source}, {connect(source, 0, recorder, 0)}, 1};
    uint64_t tick = 0;
    double seconds = 0;
    RenderReport step(double delta = .1) {
        const auto result = renderer.render(graph, format, tick++, seconds);
        seconds += delta;
        return result;
    }
    FramePtr output() const { return recorder->display()->outputs[0]; }
    std::vector<FramePtr> record(int frames) {
        recorder->params[kRecorderCapacityParam] = static_cast<float>(frames);
        recorder->params[kRecorderRecordParam] = 1;
        std::vector<FramePtr> result;
        for (int i = 0; i < frames; ++i) {
            CHECK(step().errors == 0);
            if (i) CHECK(output() == result.back());
            result.push_back(source->display()->outputs[0]);
        }
        CHECK(recorder->display()->recorderFrames == static_cast<size_t>(frames));
        CHECK(!recorder->display()->recorderRecording);
        recorder->params[kRecorderRecordParam] = 0;
        CHECK(step().errors == 0);
        CHECK(output() == result.front());
        return result;
    }
};

void testRecorderCaptureAndSeek() {
    CHECK(inputType(Kind::Recorder, 0) == PortType::Image);
    CHECK(inputType(Kind::Recorder, 1) == PortType::Audio);
    CHECK(inputType(Kind::Recorder, 2) == PortType::None);
    CHECK(outputType(Kind::Recorder, 0) == PortType::Image);
    CHECK(outputType(Kind::Recorder, 1) == PortType::None);
    CHECK(normalizedRecorderCapacity(-1) == 1);
    CHECK(normalizedRecorderCapacity(1.5f) == 2);
    CHECK(normalizedRecorderCapacity(90) == 60);
    CHECK(normalizedRecorderCapacity(std::numeric_limits<float>::quiet_NaN()) == 60);
    RecorderFixture f;
    CHECK(f.recorder->params[kRecorderCapacityParam] == 60);
    CHECK(f.recorder->params[kRecorderSpeedParam] == 1);
    CHECK(f.recorder->params[kRecorderLoopParam] == 1);
    CHECK(!f.recorder->recorderPositionConnected);
    auto clip = f.record(5);
    // Nearest-image ties and values on either side have an independent explicit oracle.
    for (const auto& sample : std::vector<std::pair<float, size_t>>{
            {.12499f, 0}, {.125f, 1}, {.12501f, 1}}) {
        f.recorder->params[kRecorderPositionParam] = sample.first;
        f.step(); CHECK(f.output() == clip[sample.second]);
    }
    const double originalSeconds = clip[4]->seconds;
    f.recorder->params[kRecorderPositionParam] = 1;
    f.step(); CHECK(f.output() == clip[4]);
    CHECK(f.output()->seconds == originalSeconds);
    f.recorder->params[kRecorderPositionParam] = .5f;
    f.step(); CHECK(f.output() == clip[2]);
    CHECK(f.recorder->display()->recorderIndex == 2);
    f.recorder->recorderPositionConnected = true;
    for (const auto& sample : std::vector<std::pair<float, size_t>>{
            {-10, 0}, {0, 0}, {5, 2}, {10, 4}, {20, 4}}) {
        f.recorder->cvVoltage = sample.first;
        f.step(); CHECK(f.output() == clip[sample.second]);
    }
    f.recorder->cvVoltage = std::numeric_limits<float>::infinity();
    CHECK(f.step().errors == 1); CHECK(f.output() == clip[0]);
    f.recorder->recorderPositionConnected = false;
    f.step(); CHECK(f.output() == clip[2]);
    // Capacity changes affect future capture, not the retained clip.
    f.recorder->params[kRecorderCapacityParam] = 1;
    f.recorder->params[kRecorderPositionParam] = 1;
    f.step(); CHECK(f.output() == clip[4]);
    CHECK(f.recorder->display()->recorderFrames == 5);
    f.recorder->params[kRecorderRecordParam] = 1;
    f.step();
    const auto replacement = f.source->display()->outputs[0];
    CHECK(f.recorder->display()->recorderFrames == 1);
    f.step(); CHECK(f.output() == replacement);
    f.step(); CHECK(f.output() == replacement);
    CHECK(f.recorder->display()->recorderFrames == 1);
    // Releasing Record early retains exactly the completed captures.
    f.recorder->params[kRecorderRecordParam] = 0;
    f.step();
    f.recorder->params[kRecorderCapacityParam] = 60;
    f.recorder->params[kRecorderRecordParam] = 1;
    f.step(); const auto early = f.source->display()->outputs[0];
    f.recorder->params[kRecorderRecordParam] = 0;
    f.step(); CHECK(f.output() == early);
    CHECK(f.recorder->display()->recorderFrames == 1);
}

void testRecorderTransport() {
    RecorderFixture f;
    auto clip = f.record(5);
    f.recorder->params[kRecorderPlayParam] = 1;
    f.recorder->params[kRecorderSpeedParam] = .5f;
    f.step(); CHECK(f.output() == clip[0]); // First Play tick seeks exactly.
    const size_t forward[] = {1, 1, 2, 2, 3, 3, 4, 4, 0, 0};
    for (auto expected : forward) { f.step(); CHECK(f.output() == clip[expected]); }
    f.recorder->params[kRecorderSpeedParam] = 0;
    f.step(); CHECK(f.output() == clip[0]);
    f.step(2); CHECK(f.output() == clip[0]);
    f.recorder->params[kRecorderPositionParam] = 1;
    f.recorder->params[kRecorderSpeedParam] = -1;
    f.step(); CHECK(f.output() == clip[4]); // Seek wins over elapsed advance.
    const size_t reverse[] = {3, 2, 1, 0, 4, 3};
    for (auto expected : reverse) { f.step(); CHECK(f.output() == clip[expected]); }
    f.recorder->params[kRecorderLoopParam] = 0;
    for (int i = 0; i < 8; ++i) f.step();
    CHECK(f.output() == clip[0]);
    f.recorder->params[kRecorderSpeedParam] = 4;
    f.step(); CHECK(f.output() == clip[4]);
    f.step(); CHECK(f.output() == clip[4]);
    f.recorder->recorderPositionConnected = true;
    f.recorder->cvVoltage = 5;
    f.step(); CHECK(f.output() == clip[2]);
    f.step(); CHECK(f.output() == clip[2]); // Held CV overrides transport every tick.
    f.recorder->recorderPositionConnected = false;
    f.recorder->params[kRecorderPositionParam] = 0;
    f.step(); CHECK(f.output() == clip[0]);
    f.recorder->params[kRecorderSpeedParam] = 1;
    f.tick += 2; f.seconds += .2;
    f.step(); CHECK(f.output() == clip[3]); // Scheduled skips advance by elapsed time.
    f.recorder->params[kRecorderPlayParam] = 0;
    f.step(); CHECK(f.output() == clip[3]);
    f.step(); CHECK(f.output() == clip[3]);
}

void testRecorderLifecycleAndFeedback() {
    RecorderFixture f;
    auto clip = f.record(3);
    f.recorder->params[kRecorderPositionParam] = 1;
    f.step(); CHECK(f.output() == clip[2]);
    f.recorder->bypass = true;
    f.step(); CHECK(f.output() == f.source->display()->outputs[0]);
    CHECK(f.recorder->display()->recorderFrames == 3);
    f.recorder->bypass = false;
    f.step(); CHECK(f.output() == clip[2]);
    f.recorder->params[kRecorderPlayParam] = 1;
    f.recorder->params[kRecorderPositionParam] = 0;
    f.step(); CHECK(f.output() == clip[0]);
    f.step(); CHECK(f.output() == clip[1]);
    f.recorder->bypass = true;
    f.step(5); f.step();
    f.recorder->bypass = false;
    f.step(); CHECK(f.output() == clip[1]);
    f.step(); CHECK(f.output() == clip[2]);
    f.recorder->params[kRecorderRecordParam] = 1;
    f.recorder->params[kRecorderCapacityParam] = 60;
    f.step();
    f.recorder->resets++;
    f.step(); CHECK(f.recorder->display()->recorderFrames == 0);
    f.step(); CHECK(f.recorder->display()->recorderFrames == 0);
    f.recorder->params[kRecorderRecordParam] = 0;
    f.step();
    f.recorder->params[kRecorderRecordParam] = 1;
    f.step(); CHECK(f.recorder->display()->recorderFrames == 1);
    f.recorder->params[kRecorderClearParam] = 1;
    f.step(); CHECK(f.recorder->display()->recorderFrames == 0);
    f.step(); CHECK(f.recorder->display()->recorderFrames == 0);
    f.recorder->params[kRecorderClearParam] = 0;
    f.recorder->params[kRecorderRecordParam] = 0;
    f.step();
    f.recorder->params[kRecorderRecordParam] = 1;
    f.step();
    f.format.width = 3;
    f.step(); CHECK(f.recorder->display()->recorderFrames == 0);
    CHECK(f.recorder->display()->status.find("format change") != std::string::npos);
    f.step(); CHECK(f.recorder->display()->recorderFrames == 0);
    f.recorder->params[kRecorderRecordParam] = 0; f.step();
    f.recorder->params[kRecorderRecordParam] = 1; f.step();
    f.seconds = 0;
    f.step(); CHECK(f.recorder->display()->recorderFrames == 0);
    CHECK(f.recorder->display()->status.find("time discontinuity") != std::string::npos);

    RecorderFixture missing;
    missing.recorder->params[kRecorderRecordParam] = 1;
    missing.graph.connections.clear();
    missing.step(); CHECK(missing.recorder->display()->recorderFrames == 0);
    CHECK(missing.recorder->display()->status.find("missing valid") != std::string::npos);
    missing.tick += 9; missing.seconds += .9;
    missing.graph.connections.push_back(connect(missing.source, 0, missing.recorder, 0));
    missing.step(); CHECK(missing.recorder->display()->recorderFrames == 1);
    const auto captured = missing.source->display()->outputs[0];
    missing.graph.connections.clear();
    missing.step(); CHECK(missing.output() == captured);
    CHECK(missing.recorder->display()->recorderFrames == 1);
    missing.graph.connections.push_back(connect(missing.source, 1, missing.recorder, 0));
    CHECK(missing.step().errors > 0);
    CHECK(missing.recorder->display()->recorderFrames == 1);

    RecorderFixture feedback;
    auto seed = feedback.record(2);
    feedback.graph.connections = {connect(feedback.recorder, 0, feedback.recorder, 0)};
    feedback.recorder->params[kRecorderRecordParam] = 1;
    feedback.recorder->params[kRecorderCapacityParam] = 3;
    CHECK(feedback.step().errors == 0);
    const auto causalBlack = feedback.output();
    CHECK(feedback.step().errors == 0); CHECK(feedback.output() == causalBlack);
    CHECK(feedback.recorder->display()->recorderFrames == 2);
    feedback.recorder->bypass = true;
    CHECK(feedback.step().errors > 0);
    CHECK(feedback.recorder->display()->status.find("zero-delay cycle") != std::string::npos);
    CHECK(feedback.recorder->display()->recorderFrames == 2);
    feedback.recorder->bypass = false;
    CHECK(feedback.step().errors == 0);
    CHECK(feedback.recorder->display()->recorderFrames == 2); // No stale capture/restart.
}

void testRecorderRetentionAndBudget() {
    RecorderFixture f;
    f.format = {720, 480, 30, 1};
    auto clip = f.record(2);
    const size_t image = 720 * 480 * 4 * sizeof(float);
    const size_t field = image / 4;
    // Source current image/field plus two retained unique captures.
    CHECK(f.step().frameBytes == 3 * image + field);
    auto other = std::make_shared<Node>(Kind::Recorder);
    f.graph.nodes.push_back(other);
    f.graph.connections.push_back(connect(f.source, 0, other, 0));
    other->params[kRecorderCapacityParam] = 60;
    other->params[kRecorderRecordParam] = 1;
    f.recorder->params[kRecorderCapacityParam] = 60;
    f.recorder->params[kRecorderRecordParam] = 1;
    const auto rejected = f.step();
    CHECK(rejected.errors == 1);
    CHECK(rejected.frameBytes == 2 * image);
    CHECK(f.recorder->display()->recorderFrames == 2); // Failed replacement preserves old clip.
    CHECK(other->display()->recorderFrames == 0);
    other->bypass = true;
    CHECK(f.step().errors == 0); // Rejected rising edge requires rearming.
    CHECK(f.recorder->display()->recorderFrames == 2);
    f.recorder->params[kRecorderRecordParam] = 0; f.step();
    f.recorder->params[kRecorderRecordParam] = 1;
    CHECK(f.step().errors == 0);
    CHECK(f.recorder->display()->recorderFrames == 1);
    f.recorder->params[kRecorderRecordParam] = 0;
    f.step();
    // Duplicate keys reject scheduling without double appends or accounting.
    f.graph.nodes.push_back(f.recorder);
    CHECK(f.step().errors > 0);
    CHECK(f.recorder->display()->recorderFrames == 1);
    f.graph.nodes.pop_back();
    f.recorder->params[kRecorderClearParam] = 1;
    CHECK(f.step().errors == 0);
    CHECK(f.recorder->display()->recorderFrames == 0);
    CHECK(f.step().frameBytes == 2 * image + field);
    // A fresh renderer is patch reload: runtime clip storage cannot leak across it.
    Renderer reloaded;
    reloaded.render(f.graph, f.format, 0, 0);
    CHECK(f.recorder->display()->recorderFrames == 0);
}

void testRecorderLoopOffAndSourceProvenance() {
    for (bool stopPlay : {false, true}) {
        RecorderFixture f;
        const auto clip = f.record(3);
        f.recorder->params[kRecorderPositionParam] = 1;
        f.recorder->params[kRecorderPlayParam] = 1;
        f.recorder->params[kRecorderSpeedParam] = .25f;
        f.step(); CHECK(f.output() == clip[2]);
        f.step(); f.step(); // Fractional phase 2.5 wraps its nearest image to zero.
        CHECK(f.output() == clip[0]);
        f.recorder->params[kRecorderLoopParam] = 0;
        if (stopPlay) f.recorder->params[kRecorderPlayParam] = 0;
        else f.recorder->params[kRecorderSpeedParam] = 0;
        f.step(); CHECK(f.output() == clip[2]);
        f.step(); CHECK(f.output() == clip[2]);
    }
    {
        RecorderFixture f;
        auto delay = std::make_shared<Node>(Kind::Delay);
        delay->params[kDelayFramesParam] = 2;
        f.graph.nodes.push_back(delay);
        f.graph.connections = {connect(f.source, 0, delay, 0),
                               connect(delay, 0, f.recorder, 0)};
        for (int i = 0; i < 5; ++i) CHECK(f.step().errors == 0);
        f.recorder->params[kRecorderCapacityParam] = 3;
        f.recorder->params[kRecorderRecordParam] = 1;
        std::vector<FramePtr> clip;
        for (int i = 0; i < 3; ++i) {
            CHECK(f.step().errors == 0);
            clip.push_back(delay->display()->outputs[0]);
            CHECK(clip.back()->sequence == static_cast<uint64_t>(3 + i));
            CHECK(std::isfinite(clip.back()->seconds) &&
                  std::abs(clip.back()->seconds - (.3 + .1 * i)) < 1e-9);
        }
        f.recorder->params[kRecorderRecordParam] = 0;
        for (int i = 0; i < 3; ++i) {
            f.recorder->params[kRecorderPositionParam] = i / 2.f;
            CHECK(f.step().errors == 0);
            CHECK(f.output() == clip[i]);
            CHECK(f.output()->sequence == static_cast<uint64_t>(3 + i));
            CHECK(std::isfinite(f.output()->seconds) &&
                  std::abs(f.output()->seconds - (.3 + .1 * i)) < 1e-9);
        }
    }
    {
        RecorderFixture f;
        auto io = std::make_shared<Node>(Kind::VideoIo);
        f.graph.nodes = {f.recorder, io};
        f.graph.connections = {connect(io, 0, f.recorder, 0)};
        f.recorder->params[kRecorderCapacityParam] = 1;
        f.recorder->params[kRecorderRecordParam] = 1;
        f.step();
        const auto black = io->display()->outputs[0];
        CHECK(black && std::all_of(black->pixels.begin(), black->pixels.end(),
                                  [](float value) { return value == 0.f; }));
        CHECK(f.recorder->display()->recorderFrames == 1);
        CHECK(f.recorder->display()->status.find("missing valid image") == std::string::npos);
        f.recorder->params[kRecorderRecordParam] = 0;
        f.step(); CHECK(f.output() == black);
    }
}

void testRecorderSharedStorageAndRejectedGraphs() {
    RecorderFixture f;
    auto second = std::make_shared<Node>(Kind::Recorder);
    f.graph.nodes.push_back(second);
    f.graph.connections.push_back(connect(f.source, 0, second, 0));
    second->params[kRecorderRecordParam] = 1;
    second->params[kRecorderCapacityParam] = 2;
    auto clip = f.record(2);
    second->params[kRecorderRecordParam] = 0;
    second->params[kRecorderPositionParam] = 1;
    const size_t image = 4 * 2 * 4 * sizeof(float);
    const size_t field = image / 4;
    CHECK(f.step().frameBytes == 3 * image + field);
    CHECK(second->display()->outputs[0] == clip[1]);
    CHECK(f.output() == clip[0]);
    // Every rejection exit includes hidden clips once, even shared across recorders.
    const auto valid = f.format;
    f.format.width = 0;
    CHECK(f.step().frameBytes == 2 * image);
    CHECK(f.recorder->display()->recorderFrames == 2);
    CHECK(second->display()->recorderFrames == 2);
    f.format = valid;
    CHECK(f.step().errors == 0);
    f.graph.nodes.resize(1025, second);
    const auto tooMany = f.step();
    CHECK(tooMany.errors == 1);
    CHECK(tooMany.frameBytes == 2 * image);
    CHECK(second->display()->recorderFrames == 2);
    f.graph.nodes.resize(3);
    CHECK(f.step().errors == 0);
    // Clear one owner; the other still retains exactly the same shared two captures.
    f.recorder->params[kRecorderClearParam] = 1;
    f.step();
    CHECK(second->display()->recorderFrames == 2);
    f.graph.nodes.erase(f.graph.nodes.begin());
    f.graph.connections.erase(f.graph.connections.begin());
    CHECK(f.step().frameBytes == 3 * image + field);

    RecorderFixture large;
    large.format = {1024, 1024, 30, 1};
    auto largeClip = large.record(8);
    largeClip.clear(); // The oracle must not keep the storage alive during release checks.
    auto newcomer = std::make_shared<Node>(Kind::Recorder);
    large.graph.nodes.push_back(newcomer);
    large.graph.connections.push_back(connect(large.source, 0, newcomer, 0));
    newcomer->params[kRecorderCapacityParam] = 24;
    newcomer->params[kRecorderRecordParam] = 1;
    large.recorder->bypass = true;
    const auto refused = large.step();
    CHECK(refused.errors == 1); // Hidden bypass clip plus requested capacity exceeds budget.
    CHECK(refused.frameBytes == 8 * 1024ull * 1024 * 4 * sizeof(float));
    CHECK(large.recorder->display()->recorderFrames == 8);
    CHECK(newcomer->display()->recorderFrames == 0);
    newcomer->params[kRecorderRecordParam] = 0;
    CHECK(large.step().errors == 0);
    newcomer->params[kRecorderRecordParam] = 1;
    large.recorder->params[kRecorderClearParam] = 1;
    CHECK(large.step().errors == 0); // Clear frees hidden ownership before admission.
    CHECK(large.recorder->display()->recorderFrames == 0);
    CHECK(newcomer->display()->recorderFrames == 1);
    newcomer->bypass = true;
    CHECK(large.step().errors == 0);
    newcomer->bypass = false;
    CHECK(large.step().errors == 0);
    CHECK(newcomer->display()->recorderFrames == 1); // Bypass cannot silently restart capture.
}

} // namespace

int main() {
    testRecorderLoopOffAndSourceProvenance();
    testRecorderSharedStorageAndRejectedGraphs();
    testRecorderCaptureAndSeek();
    testRecorderTransport();
    testRecorderLifecycleAndFeedback();
    testRecorderRetentionAndBudget();
    testTypesDefaultsAndQueue();
    testRenderTimingHistogram();
    testPatternsProcessorAndConversion();
    testRasterPhaseOrdering();
    testPhaseSpeedAllPatterns();
    testInvalidEdgesFormatsAndCycles();
    testDelayReadCommitClearAndCleanup();
    testVariableDelayExactAgesAndChanges();
    testVariableDelayClearBypassAndDiscontinuities();
    testVariableDelayReadBeforeCommitChain();
    testVariableDelayMemoryBudgetAndUniqueReporting();
    testFormatChangeClearsRasterState();
    testCvAudioEpochAndTriggers();
    testCvBypassAndAudioOverflow();
    testCvClockDriftRecovery();
    testImageNodeBypass();
    testBackendBoundaryAndHold();
    testIoSettingTransitions();
    testRejectedGraphPublisherTeardown();
    testBackendWorkerLifetime();
    testEngineIndependentClock(0);
    testEngineIndependentClock(100);
    testWorkerRunIdentifiers();
    testEngineReportAggregation();
    if (failures) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "core tests passed\n";
    return EXIT_SUCCESS;
}
