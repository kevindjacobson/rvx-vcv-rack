#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rvx {
// CPU float storage is the prototype reference backend. Values are unclipped,
// top-left origin, row-major; images are straight-alpha encoded RGB, fields one channel.
struct Format { int width = 720; int height = 480; int rateNumerator = 30000; int rateDenominator = 1001; };
struct Frame {
    int width = 0, height = 0, channels = 4;
    uint64_t sequence = 0;
    double seconds = 0;
    std::vector<float> pixels;
};
using FramePtr = std::shared_ptr<const Frame>;
enum class PortType { None, Audio, Field, Image };
enum class Kind { TestImage, Processor, CvBridge, Delay, Monitor, VideoIo };
// Fixed port indices are part of the prototype patch schema.
// TestImage: out 0 image, 1 field. Processor: in 0/1 image, 2 field; out 0 image, 1 field.
// CvBridge: in 0 CV, 1 audio, 2 trigger; out 0 field. Delay: in 0 image, 1 clear gate; out 0 image.
// Monitor: in 0 image. VideoIo: in 0 image (publish); out 0 image (receive).
PortType inputType(Kind kind, int port);
PortType outputType(Kind kind, int port);

// An epoch identifies a continuous capture run. The producer starts a new one
// on input reconnect, device/frame/rate discontinuity or queue sample loss;
// timestamps alone cannot identify uncaptured gaps for the interpolator.
struct AudioSample { double seconds = 0; float voltage = 0; uint64_t epoch = 0; };
// Exactly one audio producer and one renderer consumer. Drop newest on overflow.
template <class T, size_t Capacity> class SpscQueue {
    std::array<T, Capacity> data_{};
    std::atomic<size_t> write_{0}, read_{0};
public:
    std::atomic<uint64_t> dropped{0};
    bool push(const T& value) noexcept {
        auto w = write_.load(std::memory_order_relaxed);
        auto next = (w + 1) % Capacity;
        if (next == read_.load(std::memory_order_acquire)) { dropped.fetch_add(1, std::memory_order_relaxed); return false; }
        data_[w] = value; write_.store(next, std::memory_order_release); return true;
    }
    bool pop(T& value) noexcept {
        auto r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        value = data_[r]; read_.store((r + 1) % Capacity, std::memory_order_release); return true;
    }
};
struct VideoSource { std::string id, application, name; };
struct IoSettings { std::string sourceId, sourceApplication, sourceName, publisherName; bool publish = false; bool holdLast = false; };
struct NodeDisplay {
    std::array<FramePtr, 2> outputs{};
    FramePtr preview;
    std::string status;
    std::vector<VideoSource> sources;
    uint64_t tick = 0;
};
struct Node {
    const uint64_t key; // Never reused within this plugin process; not a Rack pointer/patch ID.
    const Kind kind;
    std::array<std::atomic<float>, 8> params{};
    std::atomic<float> cvVoltage{0};
    std::atomic<bool> bypass{false};
    std::atomic<uint64_t> triggers{0}, resets{0};
    SpscQueue<AudioSample, 32768> audio;
    explicit Node(Kind kind);
    void setIoSettings(IoSettings settings); // UI thread only, never audio.
    IoSettings ioSettings() const; // Worker/UI, never audio.
    std::shared_ptr<const NodeDisplay> display() const;
    void publishDisplay(std::shared_ptr<const NodeDisplay> display); // Renderer only.
private:
    mutable std::mutex ioMutex_;
    IoSettings io_;
    std::shared_ptr<const NodeDisplay> display_;
};
struct Connection { uint64_t source = 0; int output = 0; uint64_t destination = 0; int input = 0; };
struct Graph { std::vector<std::shared_ptr<Node>> nodes; std::vector<Connection> connections; uint64_t revision = 0; };
struct RenderReport { uint64_t tick = 0, errors = 0; double milliseconds = 0; size_t frameBytes = 0; };
struct EngineStats { uint64_t ticks = 0, lateFrames = 0; double lastMilliseconds = 0, maxMilliseconds = 0; size_t frameBytes = 0; };
class VideoBackend;
using VideoBackendFactory = std::function<std::unique_ptr<VideoBackend>()>;
// Deterministic, synchronous renderer used by both worker and independent tests.
class Renderer {
public:
    explicit Renderer(VideoBackendFactory factory = {});
    ~Renderer();
    RenderReport render(const Graph&, const Format&, uint64_t tick, double seconds);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
// Topology is submitted on UI thread. No Rack objects are accessed by this service.
class Engine {
public:
    explicit Engine(Format format = {}, VideoBackendFactory factory = {});
    ~Engine();
    void submit(Graph graph);
    void start();
    void stop();
    EngineStats stats() const;
    Format format() const;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
} // namespace rvx
