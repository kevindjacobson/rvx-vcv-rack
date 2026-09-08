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
    bool returnFrame = true;
    int receives = 0;
    int publishes = 0;
    bool lastPublishEnabled = false;
    bool lastPublishHadFrame = false;
};

class FakeBackend : public VideoBackend {
public:
    explicit FakeBackend(std::shared_ptr<BackendState> value) : state(std::move(value)) {}
    std::vector<VideoSource> sources() override { return {{"id", "app", "source"}}; }
    FramePtr receive(const IoSettings&, const Format&, uint64_t, double) override {
        ++state->receives;
        return state->returnFrame ? state->incoming : FramePtr{};
    }
    void publish(const IoSettings& settings, FramePtr frame) override {
        ++state->publishes;
        state->lastPublishEnabled = settings.publish;
        state->lastPublishHadFrame = frame != nullptr;
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

struct ThreadBackendState {
    std::atomic<int> created{0}, calls{0}, destroyed{0}, violations{0};
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
    FramePtr receive(const IoSettings&, const Format&, uint64_t, double) override {
        verify(); ++state->calls; return {};
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
    testPhaseSpeedAllPatterns();
    testInvalidEdgesFormatsAndCycles();
    testDelayReadCommitClearAndCleanup();
    testFormatChangeClearsRasterState();
    testCvAudioEpochAndTriggers();
    testCvBypassAndAudioOverflow();
    testCvClockDriftRecovery();
    testImageNodeBypass();
    testBackendBoundaryAndHold();
    testBackendWorkerLifetime();
    testEngineIndependentClock();
    if (failures) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "core tests passed\n";
    return EXIT_SUCCESS;
}
