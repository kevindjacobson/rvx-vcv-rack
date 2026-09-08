#include "../src/core/Video.hpp"
#include "../src/io/VideoBackend.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#if defined(__APPLE__)
#include <mach/mach.h>
#else
#include <sys/resource.h>
#endif

using namespace rvx;

namespace {

struct BackendCounts {
    std::atomic<int> constructed{0};
    std::atomic<int> calls{0};
    std::atomic<int> destroyed{0};
    std::atomic<int> wrongThread{0};
};

class CountedBackend : public VideoBackend {
public:
    explicit CountedBackend(std::shared_ptr<BackendCounts> value)
        : counts_(std::move(value)), owner_(std::this_thread::get_id()) {
        counts_->constructed.fetch_add(1, std::memory_order_relaxed);
    }

    ~CountedBackend() override {
        verifyThread();
        counts_->destroyed.fetch_add(1, std::memory_order_release);
    }

    std::vector<VideoSource> sources() override {
        verifyThread();
        return {};
    }

    FramePtr receive(const IoSettings&, const Format&, uint64_t, double) override {
        verifyThread();
        counts_->calls.fetch_add(1, std::memory_order_release);
        return {};
    }

    void publish(const IoSettings&, FramePtr) override { verifyThread(); }

    std::string status() const override {
        verifyThread();
        return "lifecycle test backend";
    }

private:
    void verifyThread() const {
        if (std::this_thread::get_id() != owner_)
            counts_->wrongThread.fetch_add(1, std::memory_order_relaxed);
    }

    std::shared_ptr<BackendCounts> counts_;
    std::thread::id owner_;
};

Connection connect(const std::shared_ptr<Node>& source, int output,
                   const std::shared_ptr<Node>& destination, int input) {
    return {source->key, output, destination->key, input};
}

bool waitUntil(const std::function<bool()>& condition,
               std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

uint64_t residentBytes() {
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0;
    return static_cast<uint64_t>(info.resident_size);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    // This is peak RSS on non-Mac test hosts, not current RSS.
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024;
#endif
}

int fail(const std::string& message) {
    std::cerr << "lifecycle test failed: " << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main() {
    constexpr int cycles = 50;
    constexpr int cyclesPerWorkerRun = 10;
    const Format format{8, 4, 200, 1};
    auto counts = std::make_shared<BackendCounts>();
    Engine engine(format, [counts] { return std::make_unique<CountedBackend>(counts); });
    const uint64_t rssBefore = residentBytes();
    const auto started = std::chrono::steady_clock::now();

    engine.start();
    for (int cycle = 0; cycle < cycles; ++cycle) {
        auto source = std::make_shared<Node>(Kind::TestImage);
        auto processor = std::make_shared<Node>(Kind::Processor);
        auto delay = std::make_shared<Node>(Kind::Delay);
        delay->params[kDelayFramesParam].store(static_cast<float>(1 + cycle % kMaxDelayFrames));
        auto monitor = std::make_shared<Node>(Kind::Monitor);
        auto io = std::make_shared<Node>(Kind::VideoIo);
        IoSettings ioSettings;
        ioSettings.publish = true;
        ioSettings.publisherName = "RVX lifecycle";
        io->setIoSettings(ioSettings);

        Graph graph;
        graph.nodes = {source, processor, delay, monitor, io};
        graph.connections = {
            connect(source, 0, processor, 0),
            connect(processor, 0, delay, 0),
            connect(delay, 0, monitor, 0),
            connect(processor, 0, io, 0)
        };
        graph.revision = static_cast<uint64_t>(cycle + 1);

        std::weak_ptr<Node> retiredNode = delay;
        engine.submit(std::move(graph));
        if (!waitUntil([&] { return monitor->display() && io->display(); }))
            return fail("worker did not render submitted graph " + std::to_string(cycle));
        const std::weak_ptr<const Frame> retiredFrame = monitor->display()->preview;
        const int expectedBackends = cycle + 1;
        if (!waitUntil([&] {
                return counts->constructed.load(std::memory_order_acquire) >= expectedBackends &&
                       counts->calls.load(std::memory_order_acquire) >= expectedBackends;
            }))
            return fail("backend was not created and used for graph " + std::to_string(cycle));

        const uint64_t ticksBeforeRetirement = engine.stats().ticks;
        engine.submit(Graph{});
        source.reset();
        processor.reset();
        delay.reset();
        monitor.reset();
        io.reset();
        if (!waitUntil([&] { return engine.stats().ticks >= ticksBeforeRetirement + 2; }))
            return fail("worker did not cross graph retirement boundary " + std::to_string(cycle));
        if (!waitUntil([&] { return retiredNode.expired() && retiredFrame.expired(); }))
            return fail("retired node or frame remained owned after graph " + std::to_string(cycle));
        if (!waitUntil([&] {
                return counts->destroyed.load(std::memory_order_acquire) >= expectedBackends;
            }))
            return fail("backend remained owned after graph " + std::to_string(cycle));

        if ((cycle + 1) % cyclesPerWorkerRun == 0 && cycle + 1 < cycles) {
            engine.stop();
            engine.start();
        }
    }

    // Capture a source frame, let every visible display move on, then prove it remains alive only
    // while the maximum delay's hidden history owns it and expires when that state is retired.
    auto historySource = std::make_shared<Node>(Kind::TestImage);
    auto historyDelay = std::make_shared<Node>(Kind::Delay);
    auto historyMonitor = std::make_shared<Node>(Kind::Monitor);
    historyDelay->params[kDelayFramesParam].store(static_cast<float>(kMaxDelayFrames));
    engine.submit(Graph{{historySource, historyDelay, historyMonitor},
                        {connect(historySource, 0, historyDelay, 0),
                         connect(historyDelay, 0, historyMonitor, 0)}, cycles + 1});
    if (!waitUntil([&] {
            const auto display = historySource->display();
            return display && display->outputs[0];
        }))
        return fail("maximum delay source did not render");
    auto capturedDisplay = historySource->display();
    auto capturedFrame = capturedDisplay->outputs[0];
    const uint64_t capturedTick = capturedDisplay->tick;
    const Frame* capturedAddress = capturedFrame.get();
    std::weak_ptr<const Frame> retiredHistoryFrame = capturedFrame;
    capturedFrame.reset();
    capturedDisplay.reset();
    if (!waitUntil([&] {
            const auto sourceDisplay = historySource->display();
            const auto delayDisplay = historyDelay->display();
            const auto monitorDisplay = historyMonitor->display();
            return sourceDisplay && delayDisplay && monitorDisplay &&
                   sourceDisplay->tick >= capturedTick + 3 &&
                   delayDisplay->tick >= capturedTick + 3 &&
                   monitorDisplay->tick >= capturedTick + 3 &&
                   sourceDisplay->outputs[0].get() != capturedAddress &&
                   delayDisplay->outputs[0].get() != capturedAddress &&
                   monitorDisplay->preview.get() != capturedAddress;
        }))
        return fail("visible displays did not advance beyond retained history frame");
    if (retiredHistoryFrame.expired())
        return fail("maximum delay did not retain a hidden source frame");
    engine.submit(Graph{});
    const uint64_t historyRetirement = engine.stats().ticks;
    historySource.reset();
    historyDelay.reset();
    historyMonitor.reset();
    if (!waitUntil([&] { return engine.stats().ticks >= historyRetirement + 2; }))
        return fail("worker did not cross maximum-history retirement boundary");
    if (!waitUntil([&] { return retiredHistoryFrame.expired(); }))
        return fail("maximum delay history remained owned after graph retirement");

    // Leave one backend live so stop(), rather than graph replacement, must release it.
    auto finalIo = std::make_shared<Node>(Kind::VideoIo);
    engine.submit(Graph{{finalIo}, {}, cycles + 2});
    if (!waitUntil([&] {
            return counts->constructed.load(std::memory_order_acquire) == cycles + 1;
        }))
        return fail("final backend was not constructed");
    engine.stop();
    if (counts->destroyed.load(std::memory_order_acquire) != cycles + 1)
        return fail("stop did not destroy the final backend");
    if (counts->wrongThread.load(std::memory_order_acquire) != 0)
        return fail("a backend operation or destructor ran off its owner worker");

    // Restart once more with an empty graph to cover reuse after renderer destruction.
    engine.submit(Graph{});
    finalIo.reset();
    const uint64_t runBeforeRestart = engine.stats().workerRun;
    engine.start();
    if (!waitUntil([&] {
            const EngineStats stats = engine.stats();
            return stats.workerRun > runBeforeRestart && stats.ticks > 0;
        }))
        return fail("engine did not render after final restart");
    engine.stop();

    const uint64_t rssAfter = residentBytes();
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "lifecycle test passed: cycles=" << cycles
              << " backends=" << counts->constructed.load()
              << " destroyed=" << counts->destroyed.load()
              << " worker_thread_violations=" << counts->wrongThread.load()
              << " elapsed_seconds=" << elapsed
              << " rss_before_bytes=" << rssBefore
              << " rss_after_bytes=" << rssAfter
#if defined(__APPLE__)
              << " rss_kind=current"
#else
              << " rss_kind=peak"
#endif
              << " (RSS is observational; allocator caches may retain released pages)\n";
    return EXIT_SUCCESS;
}
