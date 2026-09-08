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
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace rvx {
namespace {

constexpr size_t kMaxCpuFrameBytes = 128u * 1024u * 1024u;

void clearGlErrors() {
    while (glGetError() != GL_NO_ERROR) {}
}

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
        if (context_) {
            CurrentContext current(context_);
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize_);
        }
        status_ = context_ && maxTextureSize_ > 0
            ? "Syphon ready" : "Syphon unavailable: OpenGL context creation failed";
    }

    ~SyphonBackend() override {
        @autoreleasepool {
            if (context_) {
                CurrentContext current(context_);
                closeClient();
                closeServer();
                if (uploadTexture_) glDeleteTextures(1, &uploadTexture_);
                if (readFramebuffer_) glDeleteFramebuffers(1, &readFramebuffer_);
                if (scaledFramebuffer_) glDeleteFramebuffers(1, &scaledFramebuffer_);
                if (scaledTexture_) glDeleteTextures(1, &scaledTexture_);
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
            if (!context_ || !validWorkingFormat(format))
                return missing(settings, format, "Syphon input working format is unsupported");
            CurrentContext current(context_);
            updateRequestedSelection(settings);
            NSDictionary<NSString*, id<NSCoding>>* description = resolve(settings);
            if (!description) {
                closeClient();
                return missing(settings, format, resolutionStatus(settings));
            }

            const std::string resolvedId = sourceId(description);
            if (!client_ || selectedId_ != resolvedId || !client_.isValid) {
                closeClient();
                ++clientGeneration_;
                client_ = [[SyphonOpenGLClient alloc]
                    initWithServerDescription:description context:context_
                                      options:nil newFrameHandler:nil];
                selectedId_ = resolvedId;
                if (!client_ || !client_.isValid) {
                    closeClient();
                    return missing(settings, format, "Syphon input could not connect");
                }
                adoptedId_ = resolvedId;
            }

            const bool formatMatches = held_ && held_->width == format.width &&
                                       held_->height == format.height;
            if (!client_.hasNewFrame && heldGeneration_ == clientGeneration_ && formatMatches) {
                inputStatus_ = "receiving " + selectedLabel_;
                updateStatus();
                return held_;
            }

            SyphonOpenGLImage* image = [client_ newFrameImage];
            if (!image || image.textureSize.width < 1 || image.textureSize.height < 1)
                return missing(settings, format, "Syphon input waiting for frame");

            const double sourceWidthValue = image.textureSize.width;
            const double sourceHeightValue = image.textureSize.height;
            if (!std::isfinite(sourceWidthValue) || !std::isfinite(sourceHeightValue) ||
                sourceWidthValue > maxTextureSize_ || sourceHeightValue > maxTextureSize_ ||
                sourceWidthValue > std::numeric_limits<int>::max() ||
                sourceHeightValue > std::numeric_limits<int>::max())
                return missing(settings, format, "Syphon input dimensions are unsupported");
            const int sourceWidth = static_cast<int>(sourceWidthValue);
            const int sourceHeight = static_cast<int>(sourceHeightValue);

            if (!ensureScaledTarget(format.width, format.height))
                return missing(settings, format, "Syphon input conversion target failed");

            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_SKIP_ROWS, 0);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
            clearGlErrors();
            if (!readFramebuffer_) glGenFramebuffers(1, &readFramebuffer_);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer_);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_RECTANGLE, image.textureName, 0);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                return missing(settings, format, "Syphon input framebuffer is incomplete");
            }

            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scaledFramebuffer_);
            glBlitFramebuffer(0, 0, sourceWidth, sourceHeight,
                              0, 0, format.width, format.height,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            if (glGetError() != GL_NO_ERROR) {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                return missing(settings, format, "Syphon input GPU scale failed");
            }

            std::shared_ptr<Frame> frame;
            try {
                frame = std::make_shared<Frame>();
                frame->pixels.resize(static_cast<size_t>(format.width) * format.height * 4);
            }
            catch (const std::bad_alloc&) {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                return missing(settings, format, "Syphon input CPU frame allocation failed");
            }
            frame->width = format.width;
            frame->height = format.height;
            frame->channels = 4;
            frame->sequence = tick;
            frame->seconds = seconds;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, scaledFramebuffer_);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(0, 0, format.width, format.height, GL_RGBA, GL_FLOAT,
                         frame->pixels.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if (glGetError() != GL_NO_ERROR)
                return missing(settings, format, "Syphon input GPU conversion failed");

            // OpenGL readback is bottom-left; RVX storage is top-left.
            const size_t rowFloats = static_cast<size_t>(format.width) * 4;
            for (int y = 0; y < format.height / 2; ++y) {
                float* top = frame->pixels.data() + static_cast<size_t>(y) * rowFloats;
                float* bottom = frame->pixels.data() +
                    static_cast<size_t>(format.height - 1 - y) * rowFloats;
                std::swap_ranges(top, top + rowFloats, bottom);
            }
            held_ = frame;
            heldGeneration_ = clientGeneration_;
            inputStatus_ = "receiving " + selectedLabel_;
            updateStatus();
            return frame;
        }
    }

    void publish(const IoSettings& settings, FramePtr frame) override {
        @autoreleasepool {
            if (!context_ || maxTextureSize_ <= 0) return;
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
            if (frame->width > maxTextureSize_ || frame->height > maxTextureSize_ ||
                static_cast<size_t>(frame->width) * frame->height > kMaxCpuFrameBytes / 4) {
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
            try {
                upload_.resize(pixelCount * 4);
            }
            catch (const std::bad_alloc&) {
                outputStatus_ = "publisher staging allocation failed";
                updateStatus();
                return;
            }
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
    bool validWorkingFormat(const Format& format) const {
        if (format.width <= 0 || format.height <= 0 ||
            format.width > maxTextureSize_ || format.height > maxTextureSize_)
            return false;
        const size_t pixels = static_cast<size_t>(format.width) * format.height;
        return pixels <= kMaxCpuFrameBytes / (4 * sizeof(float));
    }

    void updateRequestedSelection(const IoSettings& settings) {
        if (settings.sourceId == requestedId_ &&
            settings.sourceApplication == requestedApplication_ &&
            settings.sourceName == requestedName_)
            return;
        closeClient();
        held_.reset();
        heldGeneration_ = 0;
        adoptedId_.clear();
        requestedId_ = settings.sourceId;
        requestedApplication_ = settings.sourceApplication;
        requestedName_ = settings.sourceName;
    }

    bool ensureScaledTarget(int width, int height) {
        if (scaledTexture_ && scaledWidth_ == width && scaledHeight_ == height)
            return true;
        if (scaledTexture_) glDeleteTextures(1, &scaledTexture_);
        scaledTexture_ = 0;
        scaledWidth_ = 0;
        scaledHeight_ = 0;
        clearGlErrors();
        if (!scaledFramebuffer_) glGenFramebuffers(1, &scaledFramebuffer_);
        glGenTextures(1, &scaledTexture_);
        glBindTexture(GL_TEXTURE_2D, scaledTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scaledFramebuffer_);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, scaledTexture_, 0);
        bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) ==
                        GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (!complete) {
            glDeleteTextures(1, &scaledTexture_);
            scaledTexture_ = 0;
            return false;
        }
        scaledWidth_ = width;
        scaledHeight_ = height;
        return true;
    }

    NSDictionary<NSString*, id<NSCoding>>* resolve(const IoSettings& settings) {
        selectedLabel_.clear();
        ambiguous_ = false;
        NSArray<NSDictionary<NSString*, id<NSCoding>>*>* descriptions =
            SyphonServerDirectory.sharedDirectory.servers;
        if (!adoptedId_.empty()) {
            for (NSDictionary<NSString*, id<NSCoding>>* description in descriptions) {
                if (sourceId(description) == adoptedId_) {
                    selectedLabel_ = label(description);
                    return description;
                }
            }
            adoptedId_.clear();
        }
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

    FramePtr missing(const IoSettings& settings, const Format& format,
                     std::string message) {
        inputStatus_ = std::move(message);
        updateStatus();
        return settings.holdLast && held_ && held_->width == format.width &&
               held_->height == format.height ? held_ : FramePtr{};
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
    GLint maxTextureSize_ = 0;
    SyphonOpenGLClient* client_ = nil;
    SyphonOpenGLServer* server_ = nil;
    GLuint uploadTexture_ = 0;
    GLuint readFramebuffer_ = 0;
    GLuint scaledFramebuffer_ = 0;
    GLuint scaledTexture_ = 0;
    int scaledWidth_ = 0;
    int scaledHeight_ = 0;
    std::vector<uint8_t> upload_;
    FramePtr held_;
    uint64_t clientGeneration_ = 0;
    uint64_t heldGeneration_ = 0;
    std::string requestedId_;
    std::string requestedApplication_;
    std::string requestedName_;
    std::string adoptedId_;
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
