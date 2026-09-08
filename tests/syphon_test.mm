#include "../src/io/VideoBackend.hpp"

#import <Foundation/Foundation.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <unistd.h>

namespace {

bool close(float actual, float expected) {
    return std::abs(actual - expected) <= (1.5f / 255.f);
}

void pumpNotifications() {
    NSDate* limit = [NSDate dateWithTimeIntervalSinceNow:0.01];
    [NSRunLoop.currentRunLoop runUntilDate:limit];
}

} // namespace

int main() {
    @autoreleasepool {
        rvx::Format format{4, 3, 30000, 1001};
        auto sender = rvx::makeSyphonBackend();
        auto receiver = rvx::makeSyphonBackend();
        if (!sender || !receiver) return 10;

        auto input = std::make_shared<rvx::Frame>();
        input->width = format.width;
        input->height = format.height;
        input->channels = 4;
        input->sequence = 1;
        input->pixels.resize(4 * 3 * 4);
        for (int y = 0; y < input->height; ++y) {
            for (int x = 0; x < input->width; ++x) {
                size_t i = static_cast<size_t>(y * input->width + x) * 4;
                input->pixels[i + 0] = x / 3.f;
                input->pixels[i + 1] = y / 2.f;
                input->pixels[i + 2] = (x + y) / 5.f;
                input->pixels[i + 3] = (x == 0 && y == 0) ? 0.5f : 1.f;
            }
        }

        rvx::IoSettings publish;
        publish.publish = true;
        publish.publisherName = "RVX loopback " + std::to_string(getpid());
        rvx::VideoSource selected;
        for (int attempt = 0; attempt < 300 && selected.id.empty(); ++attempt) {
            sender->publish(publish, input);
            pumpNotifications();
            for (const rvx::VideoSource& source : receiver->sources()) {
                if (source.name == publish.publisherName) {
                    selected = source;
                    break;
                }
            }
        }
        if (selected.id.empty()) {
            std::fprintf(stderr, "publisher was not discovered; sender=%s receiver=%s\n",
                         sender->status().c_str(), receiver->status().c_str());
            return 11;
        }

        rvx::IoSettings receive;
        receive.sourceId = selected.id;
        receive.sourceApplication = selected.application;
        receive.sourceName = selected.name;
        rvx::FramePtr output;
        for (int attempt = 0; attempt < 300 && !output; ++attempt) {
            sender->publish(publish, input);
            pumpNotifications();
            output = receiver->receive(receive, format, 2, 0.0);
        }
        if (!output) {
            std::fprintf(stderr, "no received frame: %s\n", receiver->status().c_str());
            return 12;
        }
        if (output->width != input->width || output->height != input->height ||
            output->pixels.size() != input->pixels.size()) return 13;
        for (size_t i = 0; i < input->pixels.size(); ++i) {
            if (!close(output->pixels[i], input->pixels[i])) {
                std::fprintf(stderr, "pixel mismatch at %zu: %.5f != %.5f\n",
                             i, output->pixels[i], input->pixels[i]);
                return 14;
            }
        }

        // A canvas change must reconvert the live Syphon image. Starting with a
        // reduced frame makes a stale-cache or CPU-upscale implementation visible.
        auto canvasReceiver = rvx::makeSyphonBackend();
        rvx::Format smallFormat{2, 2, 30000, 1001};
        rvx::FramePtr small;
        for (int attempt = 0; attempt < 300 && !small; ++attempt) {
            sender->publish(publish, input);
            pumpNotifications();
            small = canvasReceiver->receive(receive, smallFormat, 30, 0.0);
        }
        if (!small || small->width != 2 || small->height != 2) return 30;
        rvx::FramePtr restored = canvasReceiver->receive(receive, format, 31, 0.0);
        if (!restored || restored->sequence != 31 ||
            restored->pixels.size() != input->pixels.size()) return 31;
        for (size_t i = 0; i < input->pixels.size(); ++i) {
            if (!close(restored->pixels[i], input->pixels[i])) return 32;
        }
        if (canvasReceiver->receive(receive, rvx::Format{8192, 8192, 30000, 1001},
                                    32, 0.0) ||
            canvasReceiver->status().find("unsupported") == std::string::npos) return 33;

        // Changing the requested source invalidates the previous source's cache,
        // even if the new server is discoverable but has not published a frame.
        auto waitingSender = rvx::makeSyphonBackend();
        rvx::IoSettings waitingPublish;
        waitingPublish.publish = true;
        waitingPublish.publisherName = publish.publisherName + " waiting";
        rvx::VideoSource waitingSource;
        for (int attempt = 0; attempt < 300 && waitingSource.id.empty(); ++attempt) {
            waitingSender->publish(waitingPublish, {});
            pumpNotifications();
            for (const rvx::VideoSource& source : receiver->sources()) {
                if (source.name == waitingPublish.publisherName) waitingSource = source;
            }
        }
        if (waitingSource.id.empty()) return 34;
        rvx::IoSettings waitingReceive;
        waitingReceive.sourceId = waitingSource.id;
        waitingReceive.sourceApplication = waitingSource.application;
        waitingReceive.sourceName = waitingSource.name;
        if (receiver->receive(waitingReceive, format, 33, 0.0) ||
            receiver->status().find("waiting for frame") == std::string::npos) return 35;
        waitingSender->publish(rvx::IoSettings{}, {});

        rvx::IoSettings relayPublish;
        relayPublish.publish = true;
        relayPublish.publisherName = publish.publisherName + " relay";
        receiver->publish(relayPublish, output);
        bool relayDiscovered = false;
        for (int attempt = 0; attempt < 300 && !relayDiscovered; ++attempt) {
            pumpNotifications();
            for (const rvx::VideoSource& source : sender->sources()) {
                if (source.name == relayPublish.publisherName) relayDiscovered = true;
            }
        }
        if (!relayDiscovered) return 15;
        receiver->publish(rvx::IoSettings{}, {});

        auto resizedInput = std::make_shared<rvx::Frame>();
        resizedInput->width = 2;
        resizedInput->height = 2;
        resizedInput->channels = 4;
        resizedInput->sequence = 2;
        resizedInput->pixels = {
            1.f, 0.f, 0.f, 1.f,  0.f, 1.f, 0.f, 1.f,
            0.f, 0.f, 1.f, 1.f,  1.f, 1.f, 1.f, 0.25f
        };
        rvx::FramePtr resizedOutput;
        for (int attempt = 0; attempt < 300 &&
             (!resizedOutput || resizedOutput->sequence != 7); ++attempt) {
            sender->publish(publish, resizedInput);
            pumpNotifications();
            resizedOutput = receiver->receive(receive, format, 7, 0.0);
        }
        if (!resizedOutput || resizedOutput->sequence != 7) return 16;
        for (int y = 0; y < format.height; ++y) {
            for (int x = 0; x < format.width; ++x) {
                size_t from = static_cast<size_t>((y * 2 / format.height) * 2 +
                                                  (x * 2 / format.width)) * 4;
                size_t to = static_cast<size_t>(y * format.width + x) * 4;
                for (size_t channel = 0; channel < 4; ++channel) {
                    if (!close(resizedOutput->pixels[to + channel],
                               resizedInput->pixels[from + channel])) return 17;
                }
            }
        }
        output = resizedOutput;

        rvx::IoSettings off;
        sender->publish(off, {});
        bool retired = false;
        for (int attempt = 0; attempt < 300 && !retired; ++attempt) {
            pumpNotifications();
            retired = true;
            for (const rvx::VideoSource& source : receiver->sources()) {
                if (source.id == selected.id) retired = false;
            }
        }
        if (!retired) return 18;

        receive.holdLast = true;
        if (receiver->receive(receive, format, 3, 0.0) != output) return 19;
        receive.holdLast = false;
        if (receiver->receive(receive, format, 4, 0.0)) return 20;

        rvx::VideoSource restarted;
        for (int attempt = 0; attempt < 300 && restarted.id.empty(); ++attempt) {
            sender->publish(publish, input);
            pumpNotifications();
            for (const rvx::VideoSource& source : receiver->sources()) {
                if (source.name == publish.publisherName && source.id != selected.id) {
                    restarted = source;
                    break;
                }
            }
        }
        if (restarted.id.empty()) return 21;
        rvx::FramePtr reconnected;
        for (int attempt = 0; attempt < 300 && !reconnected; ++attempt) {
            sender->publish(publish, input);
            pumpNotifications();
            reconnected = receiver->receive(receive, format, 5, 0.0);
        }
        if (!reconnected) return 22;

        auto duplicate = rvx::makeSyphonBackend();
        rvx::VideoSource duplicateSource;
        for (int attempt = 0; attempt < 300 && duplicateSource.id.empty(); ++attempt) {
            duplicate->publish(publish, input);
            pumpNotifications();
            for (const rvx::VideoSource& source : receiver->sources()) {
                if (source.name == publish.publisherName && source.id != restarted.id) {
                    duplicateSource = source;
                    break;
                }
            }
        }
        if (duplicateSource.id.empty()) return 23;
        rvx::FramePtr afterDuplicate = receiver->receive(receive, format, 6, 0.0);
        if (!afterDuplicate || receiver->status().find("ambiguous") != std::string::npos)
            return 25;
        receive.sourceId = "missing-instance";
        if (receiver->receive(receive, format, 7, 0.0) ||
            receiver->status().find("ambiguous") == std::string::npos) return 24;

        duplicate->publish(off, {});
        sender->publish(off, {});

        std::printf("Syphon loopback, relay, resize, restart and ambiguity passed: %s\n",
                    selected.name.c_str());
        return 0;
    }
}
