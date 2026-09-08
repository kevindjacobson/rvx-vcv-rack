#pragma once
#include "../core/Video.hpp"
namespace rvx {
// Each instance belongs to one Video I/O node and is created, used and destroyed
// on the video worker. The implementation owns its graphics/device resources.
class VideoBackend {
public:
    virtual ~VideoBackend() = default;
    virtual std::vector<VideoSource> sources() = 0;
    virtual FramePtr receive(const IoSettings&, const Format&, uint64_t tick, double seconds) = 0;
    virtual void publish(const IoSettings&, FramePtr) = 0;
    virtual std::string status() const = 0;
};
std::unique_ptr<VideoBackend> makeSyphonBackend();
} // namespace rvx
