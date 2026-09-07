#include "VideoBackend.hpp"

#import <Foundation/Foundation.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#import <Syphon/SyphonOpenGLClient.h>
#import <Syphon/SyphonOpenGLImage.h>
#import <Syphon/SyphonOpenGLServer.h>
#import <Syphon/SyphonServerDirectory.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace rvx {
namespace {

std::string utf8(NSString* value) {
    if (!value) return {};
    const char* bytes = value.UTF8String;
    return bytes ? bytes : "";
}

NSString* nsString(const std::string& value) {
    return [[NSString alloc] initWithBytes:value.data()
                                    length:value.size()
                                  encoding:NSUTF8StringEncoding];
}

std::string sourceId(NSDictionary<NSString*, id<NSCoding>>* description) {
    return utf8((NSString*)description[SyphonServerDescriptionUUIDKey]);
}

class CurrentContext {
public:
    explicit CurrentContext(CGLContextObj next) : previous_(CGLGetCurrentContext()) {
        if (previous_ != next) CGLSetCurrentContext(next);
    }
    ~CurrentContext() {
        if (CGLGetCurrentContext() != previous_) CGLSetCurrentContext(previous_);
    }
private:
    CGLContextObj previous_ = nullptr;
};

CGLContextObj createContext() {
    CGLPixelFormatAttribute attributes[] = {
        kCGLPFAOpenGLProfile,
        static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),
        static_cast<CGLPixelFormatAttribute>(0)
    };
    CGLPixelFormatObj pixelFormat = nullptr;
    GLint count = 0;
    if (CGLChoosePixelFormat(attributes, &pixelFormat, &count) != kCGLNoError || !pixelFormat)
        return nullptr;
    CGLContextObj context = nullptr;
    CGLError error = CGLCreateContext(pixelFormat, nullptr, &context);
    CGLReleasePixelFormat(pixelFormat);
    return error == kCGLNoError ? context : nullptr;
}

bool selectedByName(const IoSettings& settings,
                    NSDictionary<NSString*, id<NSCoding>>* description) {
    return utf8((NSString*)description[SyphonServerDescriptionAppNameKey]) == settings.sourceApplication &&
           utf8((NSString*)description[SyphonServerDescriptionNameKey]) == settings.sourceName;
}

class SyphonBackend final : public VideoBackend {
public:
    SyphonBackend() : context_(createContext()) {
        static std::atomic<uint64_t> nextPublisher{1};
        defaultPublisher_ = "RVX " + std::to_string(nextPublisher.fetch_add(1));
        status_ = context_ ? "Syphon ready" : "Syphon unavailable: OpenGL context creation failed";
    }

    ~SyphonBackend() override {
        @autoreleasepool {
            if (context_) {
                CurrentContext current(context_);
                closeClient();
                closeServer();
                if (uploadTexture_) glDeleteTextures(1, &uploadTexture_);
                if (readFramebuffer_) glDeleteFramebuffers(1, &readFramebuffer_);
            }
            if (context_) CGLReleaseContext(context_);
        }
    }

    std::vector<VideoSource> sources() override {
        @autoreleasepool {
            std::vector<VideoSource> result;
            NSArray<NSDictionary<NSString*, id<NSCoding>>*>* descriptions =
                SyphonServerDirectory.sharedDirectory.servers;
            result.reserve(descriptions.count);
            for (NSDictionary<NSString*, id<NSCoding>>* description in descriptions) {
                result.push_back({sourceId(description),
                                  utf8((NSString*)description[SyphonServerDescriptionAppNameKey]),
                                  utf8((NSString*)description[SyphonServerDescriptionNameKey])});
            }
            return result;
        }
    }

    FramePtr receive(const IoSettings& settings, const Format& format,
                     uint64_t tick, double seconds) override {
        @autoreleasepool {
            if (!context_ || format.width <= 0 || format.height <= 0)
                return missing(settings, "Syphon input unavailable");
            CurrentContext current(context_);
            NSDictionary<NSString*, id<NSCoding>>* description = resolve(settings);
            if (!description)
                return missing(settings, resolutionStatus(settings));

            const std::string resolvedId = sourceId(description);
            if (!client_ || selectedId_ != resolvedId || !client_.isValid) {
                closeClient();
                client_ = [[SyphonOpenGLClient alloc]
                    initWithServerDescription:description context:context_
                                      options:nil newFrameHandler:nil];
                selectedId_ = resolvedId;
                if (!client_ || !client_.isValid) {
                    closeClient();
                    return missing(settings, "Syphon input could not connect");
                }
            }

            if (!client_.hasNewFrame && held_) {
                inputStatus_ = "receiving " + selectedLabel_;
                updateStatus();
                return held_;
            }

            SyphonOpenGLImage* image = [client_ newFrameImage];
            if (!image || image.textureSize.width < 1 || image.textureSize.height < 1)
                return missing(settings, "Syphon input waiting for frame");

            const int sourceWidth = static_cast<int>(image.textureSize.width);
            const int sourceHeight = static_cast<int>(image.textureSize.height);
            if (sourceWidth > std::numeric_limits<int>::max() / sourceHeight / 4)
                return missing(settings, "Syphon input dimensions are unsupported");

            std::vector<float> source(static_cast<size_t>(sourceWidth) * sourceHeight * 4);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_SKIP_ROWS, 0);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
            if (!readFramebuffer_) glGenFramebuffers(1, &readFramebuffer_);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer_);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_RECTANGLE, image.textureName, 0);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                return missing(settings, "Syphon input framebuffer is incomplete");
            }
            glReadPixels(0, 0, sourceWidth, sourceHeight, GL_RGBA, GL_FLOAT, source.data());
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_RECTANGLE, 0, 0);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            if (glGetError() != GL_NO_ERROR)
                return missing(settings, "Syphon input GPU readback failed");

            auto frame = std::make_shared<Frame>();
            frame->width = format.width;
            frame->height = format.height;
            frame->channels = 4;
            frame->sequence = tick;
            frame->seconds = seconds;
            frame->pixels.resize(static_cast<size_t>(format.width) * format.height * 4);
            for (int y = 0; y < format.height; ++y) {
                // Syphon's OpenGL image has a bottom-left origin; RVX is top-left.
                int sourceY = sourceHeight - 1 - (y * sourceHeight / format.height);
                for (int x = 0; x < format.width; ++x) {
                    int sourceX = x * sourceWidth / format.width;
                    size_t from = (static_cast<size_t>(sourceY) * sourceWidth + sourceX) * 4;
                    size_t to = (static_cast<size_t>(y) * format.width + x) * 4;
                    std::copy_n(source.data() + from, 4, frame->pixels.data() + to);
                }
            }
            held_ = frame;
            inputStatus_ = "receiving " + selectedLabel_;
            updateStatus();
            return frame;
        }
    }

    void publish(const IoSettings& settings, FramePtr frame) override {
        @autoreleasepool {
            if (!context_) return;
            CurrentContext current(context_);
            if (!settings.publish) {
                closeServer();
                outputStatus_ = "publishing off";
                updateStatus();
                return;
            }

            std::string requestedName = settings.publisherName.empty()
                ? defaultPublisher_ : settings.publisherName;
            if (!server_ || publishedName_ != requestedName) {
                closeServer();
                server_ = [[SyphonOpenGLServer alloc]
                    initWithName:nsString(requestedName) context:context_ options:nil];
                publishedName_ = requestedName;
                if (!server_) {
                    outputStatus_ = "publisher creation failed";
                    updateStatus();
                    return;
                }
            }
            if (!frame || frame->width <= 0 || frame->height <= 0 || frame->channels < 3) {
                outputStatus_ = "publisher waiting for frame";
                updateStatus();
                return;
            }
            if (frame->width > std::numeric_limits<int>::max() / frame->height / 4) {
                outputStatus_ = "publisher rejected unsupported dimensions";
                updateStatus();
                return;
            }

            const size_t pixelCount = static_cast<size_t>(frame->width) * frame->height;
            if (frame->pixels.size() < pixelCount * static_cast<size_t>(frame->channels)) {
                outputStatus_ = "publisher rejected malformed frame";
                updateStatus();
                return;
            }
            upload_.resize(pixelCount * 4);
            for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
                size_t from = pixel * static_cast<size_t>(frame->channels);
                size_t to = pixel * 4;
                for (size_t channel = 0; channel < 4; ++channel) {
                    float value = channel < static_cast<size_t>(frame->channels)
                        ? frame->pixels[from + channel] : 1.f;
                    if (!std::isfinite(value)) value = 0.f;
                    value = std::clamp(value, 0.f, 1.f);
                    upload_[to + channel] = static_cast<uint8_t>(std::lround(value * 255.f));
                }
            }

            if (!uploadTexture_) glGenTextures(1, &uploadTexture_);
            glBindTexture(GL_TEXTURE_2D, uploadTexture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame->width, frame->height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, upload_.data());
            [server_ publishFrameTexture:uploadTexture_ textureTarget:GL_TEXTURE_2D
                             imageRegion:NSMakeRect(0, 0, frame->width, frame->height)
                       textureDimensions:NSMakeSize(frame->width, frame->height)
                                 flipped:YES];
            glBindTexture(GL_TEXTURE_2D, 0);
            glFlush();
            outputStatus_ = "publishing " + publishedName_;
            updateStatus();
        }
    }

    std::string status() const override { return status_; }

private:
    NSDictionary<NSString*, id<NSCoding>>* resolve(const IoSettings& settings) {
        selectedLabel_.clear();
        ambiguous_ = false;
        NSArray<NSDictionary<NSString*, id<NSCoding>>*>* descriptions =
            SyphonServerDirectory.sharedDirectory.servers;
        if (!settings.sourceId.empty()) {
            for (NSDictionary<NSString*, id<NSCoding>>* description in descriptions) {
                if (sourceId(description) == settings.sourceId) {
                    selectedLabel_ = label(description);
                    return description;
                }
            }
        }
        NSDictionary<NSString*, id<NSCoding>>* match = nil;
        size_t matches = 0;
        if (!settings.sourceApplication.empty() || !settings.sourceName.empty()) {
            for (NSDictionary<NSString*, id<NSCoding>>* description in descriptions) {
                if (selectedByName(settings, description)) {
                    match = description;
                    ++matches;
                }
            }
        }
        if (matches == 1) {
            selectedLabel_ = label(match);
            return match;
        }
        ambiguous_ = matches > 1;
        return nil;
    }

    std::string resolutionStatus(const IoSettings& settings) const {
        if (settings.sourceId.empty() && settings.sourceApplication.empty() && settings.sourceName.empty())
            return "Syphon input: no source selected";
        return ambiguous_ ? "Syphon input selection is ambiguous" : "Syphon input source is missing";
    }

    static std::string label(NSDictionary<NSString*, id<NSCoding>>* description) {
        std::string app = utf8((NSString*)description[SyphonServerDescriptionAppNameKey]);
        std::string name = utf8((NSString*)description[SyphonServerDescriptionNameKey]);
        if (app.empty()) return name;
        if (name.empty()) return app;
        return app + " / " + name;
    }

    FramePtr missing(const IoSettings& settings, std::string message) {
        inputStatus_ = std::move(message);
        updateStatus();
        return settings.holdLast ? held_ : FramePtr{};
    }

    void closeClient() {
        [client_ stop];
        client_ = nil;
        selectedId_.clear();
    }

    void closeServer() {
        [server_ stop];
        server_ = nil;
        publishedName_.clear();
    }

    void updateStatus() {
        status_ = inputStatus_;
        if (!outputStatus_.empty()) {
            if (!status_.empty()) status_ += "; ";
            status_ += outputStatus_;
        }
    }

    CGLContextObj context_ = nullptr;
    SyphonOpenGLClient* client_ = nil;
    SyphonOpenGLServer* server_ = nil;
    GLuint uploadTexture_ = 0;
    GLuint readFramebuffer_ = 0;
    std::vector<uint8_t> upload_;
    FramePtr held_;
    std::string selectedId_;
    std::string selectedLabel_;
    std::string defaultPublisher_;
    std::string publishedName_;
    std::string inputStatus_ = "input idle";
    std::string outputStatus_ = "publishing off";
    std::string status_;
    bool ambiguous_ = false;
};

} // namespace

std::unique_ptr<VideoBackend> makeSyphonBackend() {
    return std::make_unique<SyphonBackend>();
}

} // namespace rvx
