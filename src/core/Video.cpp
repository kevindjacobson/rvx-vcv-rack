#include "Video.hpp"
#include "../io/VideoBackend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace rvx {
namespace {

constexpr int kMaxDimension = 4096;
constexpr uint64_t kMaxPixels = 16ull * 1024ull * 1024ull;
constexpr size_t kMaxNodes = 1024;
constexpr uint64_t kFrameBudgetBytes = 512ull * 1024ull * 1024ull;
constexpr size_t kMaxAudioHistory = 65536;
constexpr uint64_t kMaxPendingTriggers = 4096;

std::atomic<uint64_t> nextNodeKey{1};
std::atomic<uint64_t> nextWorkerRun{1};

bool validFormat(const Format& format) {
    if (format.width <= 0 || format.height <= 0 || format.width > kMaxDimension ||
        format.height > kMaxDimension || format.rateNumerator <= 0 ||
        format.rateDenominator <= 0)
        return false;
    const auto pixels = static_cast<uint64_t>(format.width) * static_cast<uint64_t>(format.height);
    return pixels <= kMaxPixels;
}

uint64_t estimatedFrameBytes(const Graph& graph, const Format& format) {
    const uint64_t pixels = static_cast<uint64_t>(format.width) * format.height;
    const uint64_t image = pixels * 4ull * sizeof(float);
    const uint64_t field = pixels * sizeof(float);
    // Black plus unity/zero fields are shared working inputs. Count current-tick allocations here,
    // then add every unique frame retained by the prior NodeDisplays below. I/O includes current
    // receive/export copies plus one conservative backend-retained publication.
    uint64_t result = image + 2 * field;
    for (const auto& node : graph.nodes) {
        if (!node) continue;
        switch (node->kind) {
        case Kind::TestImage: result += image + field; break;
        case Kind::Processor: result += image + field; break;
        case Kind::CvBridge: result += field; break;
        case Kind::Delay: result += image; break;
        case Kind::Monitor: break;
        case Kind::VideoIo: result += 3 * image; break;
        }
        if (result > kFrameBudgetBytes) return result;
    }
    std::unordered_set<const Frame*> retained;
    for (const auto& node : graph.nodes) {
        if (!node) continue;
        const auto display = node->display();
        if (!display) continue;
        for (const auto& frame : display->outputs) {
            if (frame && retained.insert(frame.get()).second)
                result += frame->pixels.size() * sizeof(float);
        }
        if (display->preview && retained.insert(display->preview.get()).second)
            result += display->preview->pixels.size() * sizeof(float);
        if (result > kFrameBudgetBytes) return result;
    }
    return result;
}

size_t sampleCount(int width, int height, int channels) {
    if (width <= 0 || height <= 0 || channels <= 0)
        return 0;
    const uint64_t result = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) *
                            static_cast<uint64_t>(channels);
    if (result > std::numeric_limits<size_t>::max())
        return 0;
    return static_cast<size_t>(result);
}

std::shared_ptr<Frame> makeFrame(const Format& format, int channels, uint64_t tick,
                                 double seconds, float value = 0.f) {
    auto frame = std::make_shared<Frame>();
    frame->width = format.width;
    frame->height = format.height;
    frame->channels = channels;
    frame->sequence = tick;
    frame->seconds = seconds;
    frame->pixels.assign(sampleCount(frame->width, frame->height, channels), value);
    return frame;
}

bool structurallyValid(const Frame& frame) {
    return frame.width > 0 && frame.height > 0 && frame.channels > 0 &&
           frame.pixels.size() == sampleCount(frame.width, frame.height, frame.channels);
}

bool matchesFormat(const Frame& frame, const Format& format, int channels) {
    return structurallyValid(frame) && frame.width == format.width &&
           frame.height == format.height && frame.channels == channels;
}

float finiteOrZero(float value, uint64_t& errors) {
    if (std::isfinite(value))
        return value;
    ++errors;
    return 0.f;
}

FramePtr convertReceived(const FramePtr& source, const Format& format, uint64_t tick,
                         double seconds, uint64_t& errors) {
    if (!source)
        return {};
    if (!structurallyValid(*source) ||
        (source->channels != 1 && source->channels != 3 && source->channels != 4)) {
        ++errors;
        return {};
    }
    auto result = makeFrame(format, 4, tick, seconds);
    const int sw = source->width;
    const int sh = source->height;
    for (int y = 0; y < format.height; ++y) {
        const float sourceY = format.height == 1 ? 0.f
            : static_cast<float>(y) * static_cast<float>(sh - 1) /
                  static_cast<float>(format.height - 1);
        const int y0 = static_cast<int>(std::floor(sourceY));
        const int y1 = std::min(y0 + 1, sh - 1);
        const float fy = sourceY - static_cast<float>(y0);
        for (int x = 0; x < format.width; ++x) {
            const float sourceX = format.width == 1 ? 0.f
                : static_cast<float>(x) * static_cast<float>(sw - 1) /
                      static_cast<float>(format.width - 1);
            const int x0 = static_cast<int>(std::floor(sourceX));
            const int x1 = std::min(x0 + 1, sw - 1);
            const float fx = sourceX - static_cast<float>(x0);
            const size_t out = (static_cast<size_t>(y) * format.width + x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                if (channel == 3 && source->channels != 4) {
                    result->pixels[out + channel] = 1.f;
                    continue;
                }
                const int sourceChannel = source->channels == 1 ? 0 : channel;
                auto at = [&](int sx, int sy) {
                    const size_t offset = (static_cast<size_t>(sy) * sw + sx) * source->channels +
                                          sourceChannel;
                    return finiteOrZero(source->pixels[offset], errors);
                };
                const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
                const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
                result->pixels[out + channel] = top + (bottom - top) * fy;
            }
        }
    }
    return result;
}

FramePtr exportFrame(const FramePtr& source, const Format& format, uint64_t& errors) {
    if (!source)
        return {};
    if (!matchesFormat(*source, format, 4)) {
        ++errors;
        return {};
    }
    auto result = std::make_shared<Frame>(*source);
    for (float& value : result->pixels) {
        value = std::max(0.f, std::min(1.f, finiteOrZero(value, errors)));
    }
    return result;
}

float param(const Node& node, size_t index) {
    const float value = node.params[index].load(std::memory_order_relaxed);
    return std::isfinite(value) ? value : 0.f;
}

int modeParam(const Node& node, size_t index, int low, int high) {
    const float value = param(node, index);
    if (value <= static_cast<float>(low)) return low;
    if (value >= static_cast<float>(high)) return high;
    return static_cast<int>(std::lround(value));
}

void appendStatus(std::string& status, const std::string& message) {
    if (!status.empty())
        status += "; ";
    status += message;
}

} // namespace

void RenderTimingHistogram::observe(double milliseconds) noexcept {
    if (!(milliseconds >= 0.0))
        milliseconds = 0.0;
    ++count;
    maximumMilliseconds = std::max(maximumMilliseconds, milliseconds);
    size_t bucket = bucketCount - 1;
    if (milliseconds < saturationMilliseconds) {
        bucket = static_cast<size_t>(std::ceil(milliseconds / resolutionMilliseconds));
    } else {
        ++saturated;
    }
    ++buckets[bucket];
}

double RenderTimingHistogram::quantile(double probability) const noexcept {
    if (count == 0)
        return 0.0;
    if (!(probability >= 0.0))
        probability = 0.0;
    probability = std::min(1.0, probability);
    uint64_t rank = static_cast<uint64_t>(
        std::ceil(static_cast<long double>(count) * probability));
    rank = std::max<uint64_t>(1, rank);
    uint64_t cumulative = 0;
    for (size_t bucket = 0; bucket < buckets.size(); ++bucket) {
        cumulative += buckets[bucket];
        if (cumulative >= rank)
            return static_cast<double>(bucket) * resolutionMilliseconds;
    }
    return saturationMilliseconds;
}

PortType inputType(Kind kind, int port) {
    switch (kind) {
    case Kind::Processor:
        if (port == 0 || port == 1) return PortType::Image;
        if (port == 2) return PortType::Field;
        break;
    case Kind::CvBridge:
        if (port == 0) return PortType::Audio;
        if (port == 1) return PortType::Audio;
        if (port == 2) return PortType::Audio;
        break;
    case Kind::Delay:
        if (port == 0) return PortType::Image;
        if (port == 1) return PortType::Audio;
        break;
    case Kind::Monitor:
        if (port == 0) return PortType::Image;
        break;
    case Kind::VideoIo:
        if (port == 0) return PortType::Image;
        break;
    case Kind::TestImage:
        break;
    }
    return PortType::None;
}

PortType outputType(Kind kind, int port) {
    switch (kind) {
    case Kind::TestImage:
        if (port == 0) return PortType::Image;
        if (port == 1) return PortType::Field;
        break;
    case Kind::Processor:
        if (port == 0) return PortType::Image;
        if (port == 1) return PortType::Field;
        break;
    case Kind::CvBridge:
        if (port == 0) return PortType::Field;
        break;
    case Kind::Delay:
        if (port == 0) return PortType::Image;
        break;
    case Kind::VideoIo:
        if (port == 0) return PortType::Image;
        break;
    case Kind::Monitor:
        break;
    }
    return PortType::None;
}

Node::Node(Kind nodeKind) : key(nextNodeKey.fetch_add(1, std::memory_order_relaxed)), kind(nodeKind) {
    for (auto& value : params)
        value.store(0.f, std::memory_order_relaxed);
    if (kind == Kind::Processor)
        params[0].store(1.f, std::memory_order_relaxed);
    if (kind == Kind::CvBridge)
        params[1].store(0.1f, std::memory_order_relaxed);
}

void Node::setIoSettings(IoSettings settings) {
    std::lock_guard<std::mutex> lock(ioMutex_);
    io_ = std::move(settings);
}

IoSettings Node::ioSettings() const {
    std::lock_guard<std::mutex> lock(ioMutex_);
    return io_;
}

std::shared_ptr<const NodeDisplay> Node::display() const {
    return std::atomic_load_explicit(&display_, std::memory_order_acquire);
}

void Node::publishDisplay(std::shared_ptr<const NodeDisplay> value) {
    std::atomic_store_explicit(&display_, std::move(value), std::memory_order_release);
}

struct Renderer::Impl {
    struct Binding { size_t source = 0; int output = 0; bool connected = false; };
    struct DelayState { FramePtr history; bool clearPressed = false; };
    struct TestState {
        double phase = 0;
        double lastSeconds = 0;
        float priorSpeed = 0;
        bool haveTime = false;
    };
    struct CvState {
        std::deque<AudioSample> samples;
        uint64_t epoch = 0;
        bool haveEpoch = false;
        bool haveTimeMapping = false;
        double anchorRenderSeconds = 0;
        double anchorAudioEndSeconds = 0;
        double lastRenderedAudioEndSeconds = 0;
        double recoveryAfterAudioSeconds = 0;
        bool haveRenderedAudioEnd = false;
        bool recoveringFromStall = false;
        uint64_t pendingTriggers = 0;
        uint64_t discardedTriggers = 0;
        uint64_t historyEvictions = 0;
        uint64_t clockReanchors = 0;
    };
    struct IoState {
        std::unique_ptr<VideoBackend> backend;
        FramePtr lastReceived;
        IoSettings selection;
        bool haveSelection = false;
        bool publisherMayBeActive = false;
    };

    explicit Impl(VideoBackendFactory value) : factory(std::move(value)) {}

    VideoBackendFactory factory;
    std::unordered_map<uint64_t, DelayState> delays;
    std::unordered_map<uint64_t, TestState> tests;
    std::unordered_map<uint64_t, CvState> bridges;
    std::unordered_map<uint64_t, IoState> io;
    Format activeFormat;
    bool haveFormat = false;

    void prepareFormat(const Format& format) {
        if (haveFormat && activeFormat.width == format.width && activeFormat.height == format.height &&
            activeFormat.rateNumerator == format.rateNumerator &&
            activeFormat.rateDenominator == format.rateDenominator)
            return;
        delays.clear();
        tests.clear();
        for (auto& item : bridges) {
            auto& state = item.second;
            state.samples.clear();
            state.haveEpoch = false;
            state.haveTimeMapping = false;
            state.haveRenderedAudioEnd = false;
            state.recoveringFromStall = false;
            state.pendingTriggers = 0;
        }
        for (auto& item : io) item.second.lastReceived.reset();
        activeFormat = format;
        haveFormat = true;
    }

    void cleanup(const std::unordered_set<uint64_t>& live) {
        for (auto it = delays.begin(); it != delays.end();)
            it = live.count(it->first) ? std::next(it) : delays.erase(it);
        for (auto it = tests.begin(); it != tests.end();)
            it = live.count(it->first) ? std::next(it) : tests.erase(it);
        for (auto it = bridges.begin(); it != bridges.end();)
            it = live.count(it->first) ? std::next(it) : bridges.erase(it);
        for (auto it = io.begin(); it != io.end();)
            it = live.count(it->first) ? std::next(it) : io.erase(it);
    }

    void stopPublisher(uint64_t key, IoSettings settings) {
        auto found = io.find(key);
        if (found == io.end() || !found->second.backend ||
            !found->second.publisherMayBeActive)
            return;
        settings.publish = false;
        found->second.backend->publish(settings, {});
        found->second.publisherMayBeActive = false;
    }
};

Renderer::Renderer(VideoBackendFactory factory) : impl_(new Impl(std::move(factory))) {}
Renderer::~Renderer() = default;

RenderReport Renderer::render(const Graph& graph, const Format& format, uint64_t tick,
                              double seconds) {
    const auto started = std::chrono::steady_clock::now();
    RenderReport report;
    report.tick = tick;

    std::unordered_set<uint64_t> live;
    live.reserve(graph.nodes.size());
    for (const auto& node : graph.nodes)
        if (node) live.insert(node->key);
    impl_->cleanup(live);

    const bool tooManyNodes = graph.nodes.size() > kMaxNodes;
    const bool formatIsValid = validFormat(format);
    const uint64_t estimatedBytes = formatIsValid ? estimatedFrameBytes(graph, format) : 0;
    if (!formatIsValid || tooManyNodes || estimatedBytes > kFrameBudgetBytes) {
        ++report.errors;
        std::unordered_set<uint64_t> checkedPublishers;
        std::unordered_set<uint64_t> publisherStopFailures;
        for (const auto& node : graph.nodes) {
            if (!node || node->kind != Kind::VideoIo ||
                !checkedPublishers.insert(node->key).second)
                continue;
            const IoSettings settings = node->ioSettings();
            const bool bypassed = node->bypass.load(std::memory_order_relaxed);
            if (settings.publish && !bypassed) continue;
            try { impl_->stopPublisher(node->key, settings); }
            catch (...) {
                ++report.errors;
                publisherStopFailures.insert(node->key);
            }
        }
        for (const auto& node : graph.nodes) {
            if (!node) continue;
            auto display = std::make_shared<NodeDisplay>();
            display->tick = tick;
            if (!formatIsValid)
                display->status = "invalid video format";
            else if (tooManyNodes)
                display->status = "video graph exceeds node limit";
            else
                display->status = "video graph exceeds 512 MiB frame budget";
            if (publisherStopFailures.count(node->key))
                appendStatus(display->status, "backend stop failed");
            node->publishDisplay(display);
        }
        report.milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        return report;
    }
    impl_->prepareFormat(format);

    const size_t count = graph.nodes.size();
    std::unordered_map<uint64_t, size_t> indices;
    indices.reserve(count);
    std::vector<bool> duplicate(count, false);
    std::vector<std::string> statuses(count);
    for (size_t i = 0; i < count; ++i) {
        if (!graph.nodes[i]) {
            ++report.errors;
            continue;
        }
        auto inserted = indices.emplace(graph.nodes[i]->key, i);
        if (!inserted.second) {
            duplicate[i] = true;
            duplicate[inserted.first->second] = true;
            ++report.errors;
        }
    }
    std::vector<bool> bypassed(count, false);
    for (size_t i = 0; i < count; ++i)
        if (graph.nodes[i]) bypassed[i] = graph.nodes[i]->bypass.load(std::memory_order_relaxed);

    std::vector<std::array<Impl::Binding, 8>> inputs(count);
    std::vector<std::array<bool, 8>> inputInvalid(count);
    std::vector<std::vector<size_t>> dependents(count);
    std::vector<size_t> indegree(count, 0);
    for (const auto& connection : graph.connections) {
        auto sourceIt = indices.find(connection.source);
        auto destinationIt = indices.find(connection.destination);
        if (sourceIt == indices.end() || destinationIt == indices.end()) {
            ++report.errors;
            if (destinationIt != indices.end() && connection.input >= 0 && connection.input < 8) {
                const size_t destination = destinationIt->second;
                inputInvalid[destination][connection.input] = true;
                inputs[destination][connection.input] = {};
                appendStatus(statuses[destination], "invalid connection");
            }
            continue;
        }
        const size_t source = sourceIt->second;
        const size_t destination = destinationIt->second;
        const auto& sourceNode = graph.nodes[source];
        const auto& destinationNode = graph.nodes[destination];
        const PortType outType = outputType(sourceNode->kind, connection.output);
        const PortType inType = inputType(destinationNode->kind, connection.input);
        if (duplicate[source] || duplicate[destination] || connection.input < 0 ||
            connection.input >= 8 || outType == PortType::None || inType == PortType::None ||
            outType != inType) {
            ++report.errors;
            if (connection.input >= 0 && connection.input < 8) {
                inputInvalid[destination][connection.input] = true;
                inputs[destination][connection.input] = {};
            }
            appendStatus(statuses[destination], "invalid connection");
            continue;
        }
        auto& binding = inputs[destination][connection.input];
        if (binding.connected || inputInvalid[destination][connection.input]) {
            ++report.errors;
            inputInvalid[destination][connection.input] = true;
            binding = {};
            appendStatus(statuses[destination], "multiple connections to input");
            continue;
        }
        binding = {source, connection.output, true};
    }

    for (size_t destination = 0; destination < count; ++destination) {
        if (!graph.nodes[destination] || duplicate[destination]) continue;
        for (const auto& binding : inputs[destination]) {
            if (!binding.connected || duplicate[binding.source]) continue;
            const bool activeDelay = graph.nodes[destination]->kind == Kind::Delay &&
                !bypassed[destination];
            const bool deferredVideoPublication = graph.nodes[destination]->kind == Kind::VideoIo;
            if (activeDelay || deferredVideoPublication) continue;
            dependents[binding.source].push_back(destination);
            ++indegree[destination];
        }
    }

    std::deque<size_t> ready;
    for (size_t i = 0; i < count; ++i)
        if (graph.nodes[i] && !duplicate[i] && indegree[i] == 0) ready.push_back(i);
    std::vector<size_t> order;
    order.reserve(count);
    while (!ready.empty()) {
        const size_t current = ready.front();
        ready.pop_front();
        order.push_back(current);
        for (size_t dependent : dependents[current])
            if (--indegree[dependent] == 0) ready.push_back(dependent);
    }
    std::vector<bool> schedulable(count, false);
    for (size_t index : order) schedulable[index] = true;
    for (size_t i = 0; i < count; ++i) {
        if (graph.nodes[i] && !duplicate[i] && !schedulable[i]) {
            ++report.errors;
            appendStatus(statuses[i], "zero-delay cycle");
        }
        if (duplicate[i]) appendStatus(statuses[i], "duplicate node key");
    }

    std::vector<std::array<FramePtr, 2>> outputs(count);
    const FramePtr black = makeFrame(format, 4, tick, seconds, 0.f);
    const FramePtr unity = makeFrame(format, 1, tick, seconds, 1.f);
    const FramePtr zero = makeFrame(format, 1, tick, seconds, 0.f);
    auto input = [&](size_t node, int port) -> FramePtr {
        if (port < 0 || port >= 8 || inputInvalid[node][port]) return {};
        const auto& binding = inputs[node][port];
        if (!binding.connected || !schedulable[binding.source]) return {};
        return outputs[binding.source][binding.output];
    };

    // Video I/O has a source half and a sink half. Receive every current source before operators
    // execute; publishing is deferred until every current-tick operator has completed.
    std::vector<IoSettings> ioSettings(count);
    for (size_t index = 0; index < count; ++index) {
        if (!graph.nodes[index] || !schedulable[index] || graph.nodes[index]->kind != Kind::VideoIo)
            continue;
        const auto& node = *graph.nodes[index];
        auto& state = impl_->io[node.key];
        ioSettings[index] = node.ioSettings();
        const bool selectionChanged = !state.haveSelection ||
            state.selection.sourceId != ioSettings[index].sourceId ||
            state.selection.sourceApplication != ioSettings[index].sourceApplication ||
            state.selection.sourceName != ioSettings[index].sourceName;
        if (selectionChanged) {
            state.lastReceived.reset();
            state.selection = ioSettings[index];
            state.haveSelection = true;
        }
        if (bypassed[index]) {
            outputs[index][0] = black;
            continue;
        }
        if (!state.backend && impl_->factory) {
            try { state.backend = impl_->factory(); }
            catch (...) {
                ++report.errors;
                appendStatus(statuses[index], "backend creation failed");
            }
        }
        FramePtr received;
        if (state.backend) {
            try {
                received = convertReceived(
                    state.backend->receive(ioSettings[index], format, tick, seconds),
                    format, tick, seconds, report.errors);
            } catch (...) {
                ++report.errors;
                appendStatus(statuses[index], "backend receive failed");
            }
        }
        if (received) {
            state.lastReceived = received;
            outputs[index][0] = received;
        } else if (ioSettings[index].holdLast && state.lastReceived) {
            outputs[index][0] = state.lastReceived;
        } else {
            outputs[index][0] = black;
        }
    }

    for (size_t index : order) {
        const auto& node = *graph.nodes[index];
        bool nonFiniteParameter = false;
        for (const auto& parameter : node.params)
            nonFiniteParameter |= !std::isfinite(parameter.load(std::memory_order_relaxed));
        if (nonFiniteParameter) {
            ++report.errors;
            appendStatus(statuses[index], "non-finite parameter");
        }
        if (bypassed[index]) appendStatus(statuses[index], "bypassed");
        switch (node.kind) {
        case Kind::TestImage: {
            const float speed = param(node, 1);
            auto& state = impl_->tests[node.key];
            const bool reset = graph.nodes[index]->resets.exchange(0, std::memory_order_acq_rel) > 0;
            if (reset || !state.haveTime || !std::isfinite(seconds) || seconds < state.lastSeconds) {
                if (reset || !state.haveTime) state.phase = 0;
                state.lastSeconds = std::isfinite(seconds) ? seconds : 0;
                state.haveTime = true;
            } else {
                // The elapsed interval belongs to the speed latched on the prior tick. The newly
                // latched speed controls this frame's ordered raster slope and the next interval.
                state.phase += (seconds - state.lastSeconds) * static_cast<double>(state.priorSpeed);
                state.phase -= std::floor(state.phase);
                state.lastSeconds = seconds;
            }
            state.priorSpeed = speed;
            if (bypassed[index]) {
                outputs[index][0] = black;
                outputs[index][1] = zero;
                break;
            }
            auto image = makeFrame(format, 4, tick, seconds);
            auto field = makeFrame(format, 1, tick, seconds);
            const int pattern = modeParam(node, 0, 0, 3);
            const double phase = state.phase;
            static const float bars[7][3] = {
                {1, 1, 1}, {1, 1, 0}, {0, 1, 1}, {0, 1, 0},
                {1, 0, 1}, {1, 0, 0}, {0, 0, 1}
            };
            const size_t pixels = static_cast<size_t>(format.width) * format.height;
            for (int y = 0; y < format.height; ++y) {
                for (int x = 0; x < format.width; ++x) {
                    const size_t p = static_cast<size_t>(y) * format.width + x;
                    float r = 0.f, g = 0.f, b = 0.f, f = 0.f;
                    if (pattern == 0) {
                        double position = static_cast<double>(x) / format.width + phase;
                        position -= std::floor(position);
                        const int bar = std::min(6, static_cast<int>(position * 7));
                        r = bars[bar][0]; g = bars[bar][1]; b = bars[bar][2]; f = r;
                    } else if (pattern == 1) {
                        const int scrolledX = x + static_cast<int>(std::floor(phase * 64.0));
                        f = ((scrolledX / 32 + y / 32) & 1) ? 1.f : 0.f;
                        r = g = b = f;
                    } else if (pattern == 2) {
                        const double base = format.width == 1 ? 0.0 :
                            static_cast<double>(x) / (format.width - 1);
                        double advanced = base + phase;
                        advanced -= std::floor(advanced);
                        if (phase == 0.0 && x == format.width - 1) advanced = 1.0;
                        f = static_cast<float>(advanced);
                        r = g = b = f;
                    } else {
                        const double frameDuration = static_cast<double>(format.rateDenominator) /
                                                     static_cast<double>(format.rateNumerator);
                        double ordered = phase + static_cast<double>(p) * frameDuration *
                                                   static_cast<double>(speed) /
                                                   static_cast<double>(pixels);
                        ordered -= std::floor(ordered);
                        f = static_cast<float>(ordered);
                        r = g = b = f;
                    }
                    const size_t q = p * 4;
                    image->pixels[q] = r; image->pixels[q + 1] = g;
                    image->pixels[q + 2] = b; image->pixels[q + 3] = 1.f;
                    field->pixels[p] = f;
                }
            }
            outputs[index][0] = image;
            outputs[index][1] = field;
            break;
        }
        case Kind::Processor: {
            const FramePtr a = input(index, 0) ? input(index, 0) : black;
            const int mode = modeParam(node, 3, 0, 4);
            if (bypassed[index]) {
                if (!matchesFormat(*a, format, 4)) {
                    ++report.errors;
                    appendStatus(statuses[index], "invalid input frame");
                    outputs[index][0] = black;
                    outputs[index][1] = zero;
                    break;
                }
                auto field = makeFrame(format, 1, tick, seconds);
                const int component = mode >= 2 ? mode - 2 : 0;
                for (size_t p = 0; p < field->pixels.size(); ++p)
                    field->pixels[p] = finiteOrZero(a->pixels[p * 4 + component], report.errors);
                outputs[index][0] = a;
                outputs[index][1] = field;
                break;
            }
            const FramePtr b = input(index, 1) ? input(index, 1) : black;
            const FramePtr modulation = inputInvalid[index][2] ? zero :
                (input(index, 2) ? input(index, 2) : unity);
            auto image = makeFrame(format, 4, tick, seconds);
            auto field = makeFrame(format, 1, tick, seconds);
            if (!matchesFormat(*a, format, 4) || !matchesFormat(*b, format, 4) ||
                !matchesFormat(*modulation, format, 1)) {
                ++report.errors;
                appendStatus(statuses[index], "invalid input frame");
                outputs[index][0] = black;
                outputs[index][1] = zero;
                break;
            }
            const float gainA = param(node, 0), gainB = param(node, 1), offset = param(node, 2);
            const size_t pixels = static_cast<size_t>(format.width) * format.height;
            for (size_t p = 0; p < pixels; ++p) {
                const size_t q = p * 4;
                const float mod = modulation->pixels[p];
                if (mode == 1) {
                    const float value = finiteOrZero(mod, report.errors);
                    image->pixels[q] = image->pixels[q + 1] = image->pixels[q + 2] = value;
                    image->pixels[q + 3] = 1.f;
                    field->pixels[p] = value;
                    continue;
                }
                for (int channel = 0; channel < 3; ++channel) {
                    const float mixed = a->pixels[q + channel] * gainA +
                                        b->pixels[q + channel] * gainB + offset;
                    image->pixels[q + channel] = finiteOrZero(mixed * mod, report.errors);
                }
                image->pixels[q + 3] = finiteOrZero(a->pixels[q + 3], report.errors);
                const int component = mode >= 2 ? mode - 2 : 0;
                field->pixels[p] = finiteOrZero(a->pixels[q + component], report.errors);
            }
            outputs[index][0] = image;
            outputs[index][1] = field;
            break;
        }
        case Kind::CvBridge: {
            auto& state = impl_->bridges[node.key];
            const double duration = static_cast<double>(format.rateDenominator) /
                                    static_cast<double>(format.rateNumerator);
            AudioSample sample;
            bool epochChanged = false;
            size_t acceptedSamples = 0;
            auto resetCapturedState = [&] {
                state.samples.clear();
                state.haveEpoch = false;
                state.haveTimeMapping = false;
                state.haveRenderedAudioEnd = false;
                state.recoveringFromStall = false;
                state.pendingTriggers = 0;
            };
            const bool reset = graph.nodes[index]->resets.exchange(0, std::memory_order_acq_rel) > 0;
            if (bypassed[index] || reset) {
                resetCapturedState();
                // Reset/bypass wins over all audio and trigger captures observed by this boundary.
                // Producer events arriving after these drains remain queued for the following tick.
                while (graph.nodes[index]->audio.pop(sample)) {}
                graph.nodes[index]->triggers.exchange(0, std::memory_order_acq_rel);
            } else {
                while (graph.nodes[index]->audio.pop(sample)) {
                    if (!state.haveEpoch || sample.epoch != state.epoch) {
                        state.samples.clear();
                        state.epoch = sample.epoch;
                        state.haveEpoch = true;
                        state.haveTimeMapping = false;
                        state.haveRenderedAudioEnd = false;
                        state.recoveringFromStall = false;
                        state.pendingTriggers = 0;
                        epochChanged = true;
                    }
                    if (!std::isfinite(sample.seconds) || !std::isfinite(sample.voltage)) {
                        ++report.errors;
                        appendStatus(statuses[index], "non-finite audio sample");
                        continue;
                    }
                    if (!state.samples.empty() && sample.seconds < state.samples.back().seconds) {
                        ++report.errors;
                        appendStatus(statuses[index], "non-monotonic audio timestamp");
                        continue;
                    }
                    state.samples.push_back(sample);
                    ++acceptedSamples;
                    if (state.samples.size() > kMaxAudioHistory) {
                        state.samples.pop_front();
                        ++state.historyEvictions;
                    }
                }
            }
            if (!state.haveTimeMapping && !state.samples.empty()) {
                // Producer timestamps are local to each Rack module/epoch. Anchor the newest
                // completed producer sample at the end of the one-tick-buffered raster interval.
                state.anchorRenderSeconds = seconds;
                state.anchorAudioEndSeconds = state.samples.back().seconds;
                state.haveTimeMapping = true;
            }
            if (epochChanged) {
                // Trigger counters have no epoch tag. An observed audio epoch boundary therefore
                // discards the complete atomic trigger batch visible at this frame boundary.
                graph.nodes[index]->triggers.exchange(0, std::memory_order_acq_rel);
            } else if (!bypassed[index] && !reset) {
                const uint64_t arrived = graph.nodes[index]->triggers.exchange(0, std::memory_order_acq_rel);
                if (arrived > kMaxPendingTriggers - state.pendingTriggers) {
                    state.discardedTriggers += arrived -
                        (kMaxPendingTriggers - state.pendingTriggers);
                    state.pendingTriggers = kMaxPendingTriggers;
                } else {
                    state.pendingTriggers += arrived;
                }
            }
            auto field = makeFrame(format, 1, tick, seconds);
            const int mode = modeParam(node, 0, 0, 2);
            const float scale = param(node, 1), offset = param(node, 2);
            if (bypassed[index]) {
                // The pre-zeroed field is the complete bypass output.
            } else if (mode == 0) {
                const float value = finiteOrZero(node.cvVoltage.load(std::memory_order_relaxed) * scale +
                                                 offset, report.errors);
                std::fill(field->pixels.begin(), field->pixels.end(), value);
            } else if (mode == 2) {
                const float value = state.pendingTriggers ? 1.f : 0.f;
                std::fill(field->pixels.begin(), field->pixels.end(), value);
                if (state.pendingTriggers) --state.pendingTriggers;
            } else {
                double end = state.anchorAudioEndSeconds + (seconds - state.anchorRenderSeconds);
                double begin = end - duration;
                constexpr double timestampTolerance = 1e-9;
                bool windowAvailable = !state.samples.empty() &&
                    begin + timestampTolerance >= state.samples.front().seconds &&
                    end - timestampTolerance <= state.samples.back().seconds;
                if (!acceptedSamples && !windowAvailable && state.haveRenderedAudioEnd &&
                    !state.recoveringFromStall) {
                    state.recoveringFromStall = true;
                    state.recoveryAfterAudioSeconds = state.lastRenderedAudioEndSeconds;
                }
                const bool completeRecoveryWindow = !state.recoveringFromStall ||
                    (!state.samples.empty() && state.samples.back().seconds -
                        state.recoveryAfterAudioSeconds + timestampTolerance >= duration);
                if (state.recoveringFromStall) windowAvailable = false;
                const bool producerAhead = !state.samples.empty() &&
                    state.samples.back().seconds - end > duration;
                const bool fullNewestWindow = !state.samples.empty() &&
                    state.samples.back().seconds - state.samples.front().seconds +
                        timestampTolerance >= duration;
                if (acceptedSamples && fullNewestWindow && completeRecoveryWindow &&
                    (!windowAvailable || producerAhead)) {
                    end = state.samples.back().seconds;
                    begin = end - duration;
                    state.anchorRenderSeconds = seconds;
                    state.anchorAudioEndSeconds = end;
                    ++state.clockReanchors;
                    windowAvailable = begin + timestampTolerance >= state.samples.front().seconds;
                    state.recoveringFromStall = false;
                }
                // An unavailable window may be only a fraction of one audio block short. Preserve
                // it so the next capture can complete a full raster interval; the fixed history cap
                // remains the backstop. Once usable, retain one interpolation sample before begin.
                if (windowAvailable) {
                    while (state.samples.size() > 2 && state.samples[1].seconds < begin)
                        state.samples.pop_front();
                }
                windowAvailable = windowAvailable && !state.samples.empty() &&
                    begin + timestampTolerance >= state.samples.front().seconds &&
                    end - timestampTolerance <= state.samples.back().seconds;
                if (!windowAvailable)
                    appendStatus(statuses[index], "audio history underrun");
                auto interpolate = [&](double at) {
                    if (state.samples.empty()) return 0.f;
                    if (at + timestampTolerance < state.samples.front().seconds ||
                        at - timestampTolerance > state.samples.back().seconds)
                        return 0.f;
                    at = std::max(state.samples.front().seconds,
                                  std::min(state.samples.back().seconds, at));
                    auto upper = std::lower_bound(state.samples.begin(), state.samples.end(), at,
                        [](const AudioSample& value, double time) { return value.seconds < time; });
                    if (upper == state.samples.begin()) return upper->voltage;
                    if (upper == state.samples.end()) return state.samples.back().voltage;
                    const auto& right = *upper;
                    const auto& left = *std::prev(upper);
                    const double span = right.seconds - left.seconds;
                    if (span <= 0.0) return right.voltage;
                    const float amount = static_cast<float>((at - left.seconds) / span);
                    return left.voltage + (right.voltage - left.voltage) * amount;
                };
                if (windowAvailable) {
                    const size_t pixels = field->pixels.size();
                    for (size_t p = 0; p < pixels; ++p) {
                        const double amount = pixels == 1 ? 0.0 :
                            static_cast<double>(p) / static_cast<double>(pixels - 1);
                        field->pixels[p] = finiteOrZero(interpolate(begin + (end - begin) * amount) *
                                                        scale + offset, report.errors);
                    }
                    state.lastRenderedAudioEndSeconds = end;
                    state.haveRenderedAudioEnd = true;
                }
            }
            if (state.discardedTriggers)
                appendStatus(statuses[index], "trigger overflow " + std::to_string(state.discardedTriggers));
            if (state.historyEvictions)
                appendStatus(statuses[index], "audio history evicted " +
                             std::to_string(state.historyEvictions));
            if (state.clockReanchors)
                appendStatus(statuses[index], "audio clock reanchored " +
                             std::to_string(state.clockReanchors));
            const uint64_t droppedAudio = graph.nodes[index]->audio.dropped.load(std::memory_order_relaxed);
            if (droppedAudio)
                appendStatus(statuses[index], "audio queue dropped " + std::to_string(droppedAudio));
            outputs[index][0] = field;
            break;
        }
        case Kind::Delay: {
            auto& state = impl_->delays[node.key];
            const bool pressed = param(node, 0) >= 0.5f;
            const bool clear = graph.nodes[index]->resets.exchange(0, std::memory_order_acq_rel) > 0 ||
                               (pressed && !state.clearPressed);
            state.clearPressed = pressed;
            if (clear) state.history.reset();
            if (bypassed[index])
                outputs[index][0] = input(index, 0) ? input(index, 0) : black;
            else
                outputs[index][0] = state.history ? state.history : black;
            break;
        }
        case Kind::Monitor:
            break;
        case Kind::VideoIo:
            break;
        }
    }

    // All active delays observed the old histories above. Only now may new histories be committed.
    for (size_t index : order) {
        const auto& node = *graph.nodes[index];
        if (node.kind != Kind::Delay || bypassed[index]) continue;
        auto& state = impl_->delays[node.key];
        const FramePtr next = input(index, 0);
        state.history = next && matchesFormat(*next, format, 4) ? next : black;
    }

    for (size_t index = 0; index < count; ++index) {
        if (!graph.nodes[index] || !schedulable[index] || graph.nodes[index]->kind != Kind::VideoIo)
            continue;
        auto found = impl_->io.find(graph.nodes[index]->key);
        if (bypassed[index]) {
            if (found != impl_->io.end() && found->second.backend) {
                try { impl_->stopPublisher(graph.nodes[index]->key, ioSettings[index]); }
                catch (...) {
                    ++report.errors;
                    appendStatus(statuses[index], "backend stop failed");
                }
            }
            continue;
        }
        if (found == impl_->io.end() || !found->second.backend) {
            appendStatus(statuses[index], "backend unavailable");
            continue;
        }
        try {
            if (ioSettings[index].publish) {
                const FramePtr publishInput = input(index, 0) ? input(index, 0) : black;
                found->second.publisherMayBeActive = true;
                found->second.backend->publish(
                    ioSettings[index], exportFrame(publishInput, format, report.errors));
            } else {
                impl_->stopPublisher(graph.nodes[index]->key, ioSettings[index]);
            }
        } catch (...) {
            ++report.errors;
            appendStatus(statuses[index], "backend publish failed");
        }
        try { appendStatus(statuses[index], found->second.backend->status()); }
        catch (...) {
            ++report.errors;
            appendStatus(statuses[index], "backend status failed");
        }
    }

    std::unordered_set<const Frame*> frames;
    auto countFrame = [&](const FramePtr& frame) {
        if (frame && frames.insert(frame.get()).second)
            report.frameBytes += frame->pixels.size() * sizeof(float);
    };
    for (size_t i = 0; i < count; ++i) {
        if (!graph.nodes[i]) continue;
        auto display = std::make_shared<NodeDisplay>();
        display->outputs = outputs[i];
        if (graph.nodes[i]->kind == Kind::Monitor)
            display->preview = bypassed[i] ? black : (input(i, 0) ? input(i, 0) : black);
        else if (graph.nodes[i]->kind == Kind::VideoIo)
            display->preview = outputs[i][0];
        display->status = statuses[i];
        display->tick = tick;
        if (graph.nodes[i]->kind == Kind::VideoIo && !bypassed[i]) {
            auto found = impl_->io.find(graph.nodes[i]->key);
            if (found != impl_->io.end() && found->second.backend) {
                try { display->sources = found->second.backend->sources(); }
                catch (...) { ++report.errors; appendStatus(display->status, "source discovery failed"); }
            }
        }
        graph.nodes[i]->publishDisplay(display);
        countFrame(display->outputs[0]);
        countFrame(display->outputs[1]);
        countFrame(display->preview);
    }
    for (const auto& item : impl_->delays) countFrame(item.second.history);
    for (const auto& item : impl_->bridges)
        report.frameBytes += item.second.samples.size() * sizeof(AudioSample);

    report.milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    return report;
}

struct Engine::Impl {
    Impl(Format value, VideoBackendFactory valueFactory)
        : videoFormat(value), factory(std::move(valueFactory)) {}
    Format videoFormat;
    VideoBackendFactory factory;
    mutable std::mutex mutex;
    std::condition_variable wake;
    Graph graph;
    EngineStats statistics;
    bool running = false;
    bool stopRequested = false;
    std::thread worker;

    void run() {
        using Clock = std::chrono::steady_clock;
        // Renderer owns every backend. This scope guarantees backend creation, use, and destruction
        // all happen on the video worker, including across repeated start/stop cycles.
        Renderer renderer(factory);
        const auto origin = Clock::now();
        const bool validRate = videoFormat.rateNumerator > 0 && videoFormat.rateDenominator > 0;
        const long double period = validRate
            ? static_cast<long double>(videoFormat.rateDenominator) /
                  static_cast<long double>(videoFormat.rateNumerator)
            : 1.0L / 30.0L;
        uint64_t tick = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            statistics = {};
            statistics.workerRun = nextWorkerRun.fetch_add(1, std::memory_order_relaxed);
        }
        while (true) {
            const auto target = origin + std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<long double>(static_cast<long double>(tick) * period));
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_until(lock, target, [&] { return stopRequested; });
                if (stopRequested) break;
            }
            const auto before = Clock::now();
            Graph snapshot;
            {
                std::lock_guard<std::mutex> lock(mutex);
                snapshot = graph;
            }
            const double seconds = validRate
                ? static_cast<double>(tick) * static_cast<double>(videoFormat.rateDenominator) /
                      static_cast<double>(videoFormat.rateNumerator)
                : static_cast<double>(tick) / 30.0;
            const RenderReport report = renderer.render(snapshot, videoFormat, tick, seconds);
            const auto after = Clock::now();
            const auto nextTarget = origin + std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<long double>(static_cast<long double>(tick + 1) * period));
            std::lock_guard<std::mutex> lock(mutex);
            ++statistics.ticks;
            statistics.lastMilliseconds = report.milliseconds;
            statistics.maxMilliseconds = std::max(statistics.maxMilliseconds, report.milliseconds);
            statistics.frameBytes = report.frameBytes;
            statistics.renderMilliseconds.observe(report.milliseconds);
            statistics.renderErrors += report.errors;
            if (report.errors != 0)
                ++statistics.renderErrorFrames;
            const bool late = after > nextTarget || before > target + std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<long double>(period));
            if (late) {
                ++statistics.renderDeadlineMisses;
                ++statistics.lateFrames;
            }
            ++tick;
            // Absolute targets prevent cumulative drift; overload skips stale simulation ticks.
            const auto now = Clock::now();
            const auto due = origin + std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<long double>(static_cast<long double>(tick) * period));
            if (now > due + std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<long double>(period))) {
                const long double elapsed = std::chrono::duration<long double>(now - origin).count();
                const uint64_t resumedTick = static_cast<uint64_t>(elapsed / period) + 1;
                if (resumedTick > tick) {
                    const uint64_t skipped = resumedTick - tick;
                    statistics.skippedTicks += skipped;
                    statistics.lateFrames += skipped;
                    tick = resumedTick;
                }
            }
        }
    }
};

Engine::Engine(Format format, VideoBackendFactory factory)
    : impl_(new Impl(format, std::move(factory))) {}

Engine::~Engine() { stop(); }

void Engine::submit(Graph graph) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->graph = std::move(graph);
}

void Engine::start() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->running) return;
    impl_->stopRequested = false;
    impl_->running = true;
    impl_->worker = std::thread([this] { impl_->run(); });
}

void Engine::stop() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->running) return;
        impl_->stopRequested = true;
    }
    impl_->wake.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->running = false;
}

EngineStats Engine::stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->statistics;
}

Format Engine::format() const { return impl_->videoFormat; }

} // namespace rvx
