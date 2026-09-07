#include "../src/core/Video.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <sys/resource.h>

using namespace rvx;

namespace {
double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(std::ceil(fraction * values.size())) - 1;
    return values[std::min(index, values.size() - 1)];
}

long peakRssBytes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return -1;
#if defined(__APPLE__)
    return usage.ru_maxrss;
#else
    return usage.ru_maxrss * 1024L;
#endif
}

Connection connect(const std::shared_ptr<Node>& source, int output,
                   const std::shared_ptr<Node>& destination, int input) {
    return {source->key, output, destination->key, input};
}
} // namespace

int main(int argc, char** argv) {
    double durationSeconds = 10.0;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--seconds" && i + 1 < argc) {
            durationSeconds = std::strtod(argv[++i], nullptr);
        } else if (std::string(argv[i]) == "--help") {
            std::cout << "usage: rvx-benchmark [--seconds N]\n";
            return 0;
        } else {
            std::cerr << "unknown or incomplete argument: " << argv[i] << '\n';
            return 2;
        }
    }
    if (!(durationSeconds > 0.0) || !std::isfinite(durationSeconds)) {
        std::cerr << "--seconds must be a positive finite number\n";
        return 2;
    }

    const Format format{};
    const double framePeriod = static_cast<double>(format.rateDenominator) / format.rateNumerator;
    auto test = std::make_shared<Node>(Kind::TestImage);
    auto processorA = std::make_shared<Node>(Kind::Processor);
    auto processorB = std::make_shared<Node>(Kind::Processor);
    auto delay = std::make_shared<Node>(Kind::Delay);
    auto monitor = std::make_shared<Node>(Kind::Monitor);
    auto bridge = std::make_shared<Node>(Kind::CvBridge);
    test->params[0].store(3.f);
    test->params[1].store(0.75f);
    processorA->params[0].store(1.15f);
    processorA->params[2].store(-0.075f);
    processorB->params[0].store(0.9f);
    processorB->params[1].store(0.1f);
    bridge->cvVoltage.store(5.f);

    Graph graph;
    graph.nodes = {test, processorA, processorB, delay, monitor, bridge};
    graph.connections = {
        connect(test, 0, processorA, 0), connect(bridge, 0, processorA, 2),
        connect(processorA, 0, processorB, 0), connect(delay, 0, processorB, 1),
        connect(processorB, 0, delay, 0), connect(delay, 0, monitor, 0)
    };
    graph.revision = 1;

    Renderer renderer;
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(durationSeconds / framePeriod) + 2);
    uint64_t missedDeadlines = 0;
    uint64_t errors = 0;
    size_t steadyFrameBytes = 0;
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    uint64_t tick = 0;
    for (;;) {
        const auto target = started + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(tick * framePeriod));
        std::this_thread::sleep_until(target);
        if (std::chrono::duration<double>(Clock::now() - started).count() >= durationSeconds)
            break;
        const RenderReport report = renderer.render(graph, format, tick, tick * framePeriod);
        samples.push_back(report.milliseconds);
        errors += report.errors;
        steadyFrameBytes = report.frameBytes;
        ++tick;
        const auto deadline = started + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(tick * framePeriod));
        if (Clock::now() > deadline) ++missedDeadlines;
    }
    const double elapsed = std::chrono::duration<double>(Clock::now() - started).count();
    const long rss = peakRssBytes();
    std::cout << std::fixed << std::setprecision(3)
              << "{\"requested_seconds\":" << durationSeconds
              << ",\"elapsed_seconds\":" << elapsed
              << ",\"ticks\":" << samples.size()
              << ",\"format\":\"" << format.width << "x" << format.height << "@30000/1001\""
              << ",\"render_ms_p50\":" << percentile(samples, .50)
              << ",\"render_ms_p95\":" << percentile(samples, .95)
              << ",\"render_ms_p99\":" << percentile(samples, .99)
              << ",\"render_ms_max\":" << percentile(samples, 1.0)
              << ",\"deadline_ms\":" << framePeriod * 1000.0
              << ",\"missed_deadlines\":" << missedDeadlines
              << ",\"render_errors\":" << errors
              << ",\"reported_frame_bytes\":" << steadyFrameBytes
              << ",\"peak_rss_bytes\":" << rss << "}\n";
    return errors == 0 ? 0 : 1;
}
