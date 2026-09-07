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

void near(float actual, float expected, float tolerance, int line) {
    if (std::fabs(actual - expected) > tolerance) {
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

void testTypesDefaultsAndQueue() {
    CHECK(inputType(Kind::Processor, 0) == PortType::Image);
    CHECK(inputType(Kind::Processor, 2) == PortType::Field);
    CHECK(inputType(Kind::Delay, 1) == PortType::Audio);
    CHECK(inputType(Kind::TestImage, 0) == PortType::None);
    CHECK(outputType(Kind::TestImage, 1) == PortType::Field);
    CHECK(outputType(Kind::Monitor, 0) == PortType::None);

    Node processor(Kind::Processor);
    Node bridge(Kind::CvBridge);
    NEAR(processor.params[0].load(), 1.f, 0.f);
    NEAR(processor.params[1].load(), 0.f, 0.f);
    NEAR(bridge.params[1].load(), 0.1f, 0.f);
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

    bridge->params[0].store(2.f);
    bridge->triggers.fetch_add(3);
    for (int tick = 0; tick < 4; ++tick) {
        renderer.render(graph, format, 20 + tick, 2.0 + tick * .1);
        NEAR(bridge->display()->outputs[0]->pixels[0], tick < 3 ? 1.f : 0.f, 0.f);
    }
    bridge->triggers.fetch_add(5000);
    renderer.render(graph, format, 30, 3.0);
    CHECK(bridge->display()->status.find("trigger overflow") != std::string::npos);
}

struct BackendState {
    FramePtr incoming;
    FramePtr published;
    bool returnFrame = true;
    int receives = 0;
    int publishes = 0;
};

class FakeBackend : public VideoBackend {
public:
    explicit FakeBackend(std::shared_ptr<BackendState> value) : state(std::move(value)) {}
    std::vector<VideoSource> sources() override { return {{"id", "app", "source"}}; }
    FramePtr receive(const IoSettings&, const Format&, uint64_t, double) override {
        ++state->receives;
        return state->returnFrame ? state->incoming : FramePtr{};
    }
    void publish(const IoSettings&, FramePtr frame) override {
        ++state->publishes;
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
    auto source = std::make_shared<Node>(Kind::TestImage);
    auto io = std::make_shared<Node>(Kind::VideoIo);
    source->params[0].store(2.f);
    IoSettings settings;
    settings.publish = true;
    settings.holdLast = true;
    io->setIoSettings(settings);
    Graph graph{{source, io}, {connect(source, 0, io, 0)}, 1};
    auto report = renderer.render(graph, format, 0, 0);
    CHECK(report.errors == 0 && state->receives == 1 && state->publishes == 1);
    auto output = io->display()->outputs[0];
    NEAR(pixel(output, 1, 1, 0), -.5f, 0.f); // receive conversion preserves signed values
    NEAR(pixel(output, 1, 1, 2), 1.5f, 0.f);
    NEAR(pixel(output, 1, 1, 3), 1.f, 0.f); // RGB receives explicit straight alpha
    for (float value : state->published->pixels) CHECK(value >= 0.f && value <= 1.f);
    CHECK(io->display()->sources.size() == 1 && io->display()->status == "ready");

    state->returnFrame = false;
    renderer.render(graph, format, 1, 1.0 / 30.0);
    NEAR(pixel(io->display()->outputs[0], 0, 0, 0), -.5f, 0.f);
    settings.holdLast = false;
    io->setIoSettings(settings);
    renderer.render(graph, format, 2, 2.0 / 30.0);
    NEAR(pixel(io->display()->outputs[0], 0, 0, 0), 0.f, 0.f);
}

void testEngineIndependentClock() {
    Format format{4, 2, 50, 1};
    auto source = std::make_shared<Node>(Kind::TestImage);
    Engine engine(format);
    engine.submit(Graph{{source}, {}, 1});
    engine.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (engine.stats().ticks < 7 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    engine.stop();
    const EngineStats stats = engine.stats();
    CHECK(stats.ticks >= 7);
    CHECK(source->display() && source->display()->tick + 1 == stats.ticks);
    CHECK(stats.frameBytes > 0 && stats.maxMilliseconds >= stats.lastMilliseconds);
    CHECK(engine.format().rateNumerator == 50);

    engine.start();
    const auto restartDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (engine.stats().ticks == stats.ticks && std::chrono::steady_clock::now() < restartDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    engine.stop();
    CHECK(engine.stats().ticks > stats.ticks);
}
} // namespace

int main() {
    testTypesDefaultsAndQueue();
    testPatternsProcessorAndConversion();
    testRasterPhaseOrdering();
    testInvalidEdgesFormatsAndCycles();
    testDelayReadCommitClearAndCleanup();
    testCvAudioEpochAndTriggers();
    testBackendBoundaryAndHold();
    testEngineIndependentClock();
    if (failures) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "core tests passed\n";
    return EXIT_SUCCESS;
}
