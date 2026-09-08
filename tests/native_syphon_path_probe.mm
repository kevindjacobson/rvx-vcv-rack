#include "NativeValidationJson.hpp"
#include "../src/io/VideoBackend.hpp"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unistd.h>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using rvx::native_validation::jsonQuote;
constexpr int kWidth = 720;
constexpr int kHeight = 480;
constexpr int kChannels = 4;
constexpr int kRateNumerator = 30000;
constexpr int kRateDenominator = 1001;
constexpr double kNominalFps = static_cast<double>(kRateNumerator) / kRateDenominator;
constexpr double kFrameInterval = static_cast<double>(kRateDenominator) / kRateNumerator;
constexpr double kMaximumDurationSeconds = 3600.0;
volatile std::sig_atomic_t stopRequested = 0;

void signalHandler(int) {
    stopRequested = 1;
}

double elapsedSeconds(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

void pumpRunLoop(double seconds) {
    @autoreleasepool {
        NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:std::max(0.0001, seconds)];
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:deadline];
    }
}

uint64_t hashPixels(const rvx::Frame& frame) {
    uint64_t hash = 1469598103934665603ull;
    auto addBytes = [&hash](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    addBytes(&frame.width, sizeof(frame.width));
    addBytes(&frame.height, sizeof(frame.height));
    addBytes(&frame.channels, sizeof(frame.channels));
    if (!frame.pixels.empty()) addBytes(frame.pixels.data(), frame.pixels.size() * sizeof(float));
    return hash;
}

std::shared_ptr<rvx::Frame> makePattern(uint64_t sequence) {
    static constexpr float bars[8][3] = {
        {1.f, 1.f, 1.f}, {1.f, 1.f, 0.f}, {0.f, 1.f, 1.f}, {0.f, 1.f, 0.f},
        {1.f, 0.f, 1.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, 0.f},
    };
    auto frame = std::make_shared<rvx::Frame>();
    frame->width = kWidth;
    frame->height = kHeight;
    frame->channels = kChannels;
    frame->sequence = sequence;
    frame->seconds = sequence * kFrameInterval;
    frame->pixels.resize(static_cast<size_t>(kWidth) * kHeight * kChannels);

    int movingX = static_cast<int>((sequence * 7) % kWidth);
    int movingY = static_cast<int>((sequence * 3) % kHeight);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            int bar = std::min(7, x * 8 / kWidth);
            float red = bars[bar][0] * 0.75f;
            float green = bars[bar][1] * 0.75f;
            float blue = bars[bar][2] * 0.75f;

            if ((x - movingX + kWidth) % kWidth < 18) {
                float value = ((y / 12 + static_cast<int>(sequence)) & 1) ? 1.f : 0.f;
                red = green = blue = value;
            }
            if ((y - movingY + kHeight) % kHeight < 4) {
                red = 1.f;
                green = 0.25f;
                blue = 0.f;
            }
            // The top-left blocks encode the low 24 frame-index bits.
            if (x < 24 * 8 && y < 24) {
                int bit = x / 8;
                float value = ((sequence >> bit) & 1u) ? 1.f : 0.f;
                red = value;
                green = 1.f - value;
                blue = 0.25f;
            }

            size_t index = (static_cast<size_t>(y) * kWidth + x) * kChannels;
            frame->pixels[index + 0] = red;
            frame->pixels[index + 1] = green;
            frame->pixels[index + 2] = blue;
            frame->pixels[index + 3] = 1.f;
        }
    }
    return frame;
}

std::vector<rvx::VideoSource> exactMatches(
    rvx::VideoBackend& backend,
    const std::string& serverName) {
    std::vector<rvx::VideoSource> matches;
    for (const auto& source : backend.sources()) {
        if (source.name == serverName) matches.push_back(source);
    }
    return matches;
}

struct Options {
    double duration = 0.0;
    double progressInterval = 10.0;
    std::string sourceName;
    std::string outputName;
    bool loopback = false;
};

void usage(const char* program) {
    std::cerr
        << "usage: " << program
        << " --duration SECONDS --output-name EXACT_NAME [--source-name UNIQUE_NAME] [--progress SECONDS]\n"
        << "       " << program
        << " --loopback --duration SECONDS [--source-name UNIQUE_NAME] [--progress SECONDS]\n"
        << "       duration must be greater than 0 and no more than 3600\n";
}

bool parseNumber(const char* text, double& result) {
    char* end = nullptr;
    result = std::strtod(text, &end);
    return end && *end == '\0' && std::isfinite(result);
}

std::optional<Options> parseOptions(int argc, char** argv) {
    Options options;
    bool durationSeen = false;
    bool progressSeen = false;
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--duration" && i + 1 < argc) {
            if (durationSeen || !parseNumber(argv[++i], options.duration)) return std::nullopt;
            durationSeen = true;
        }
        else if (argument == "--progress" && i + 1 < argc) {
            if (progressSeen || !parseNumber(argv[++i], options.progressInterval)) return std::nullopt;
            progressSeen = true;
        }
        else if (argument == "--source-name" && i + 1 < argc) {
            if (!options.sourceName.empty()) return std::nullopt;
            options.sourceName = argv[++i];
        }
        else if (argument == "--output-name" && i + 1 < argc) {
            if (!options.outputName.empty()) return std::nullopt;
            options.outputName = argv[++i];
        }
        else if (argument == "--loopback") {
            if (options.loopback) return std::nullopt;
            options.loopback = true;
        }
        else if (argument == "--help") {
            usage(argv[0]);
            std::exit(0);
        }
        else {
            return std::nullopt;
        }
    }
    if (!durationSeen || options.duration <= 0.0 ||
        options.duration > kMaximumDurationSeconds || options.progressInterval < 0.0 ||
        options.progressInterval > kMaximumDurationSeconds) {
        return std::nullopt;
    }
    if (options.sourceName.empty())
        options.sourceName = "RVX validation source pid " + std::to_string(getpid());
    if (options.loopback) {
        if (!options.outputName.empty() && options.outputName != options.sourceName)
            return std::nullopt;
        options.outputName = options.sourceName;
    }
    else if (options.outputName.empty() || options.outputName == options.sourceName) {
        return std::nullopt;
    }
    return options;
}

struct Observations {
    uint64_t publishedFrames = 0;
    uint64_t missedPublishCadenceSlots = 0;
    double maxPublishStartLatenessMs = 0.0;
    uint64_t outputDiscoveryPolls = 0;
    uint64_t outputAbsentPolls = 0;
    uint64_t outputAmbiguousPolls = 0;
    uint64_t outputIdentityChanges = 0;
    uint64_t receivePolls = 0;
    uint64_t nullReceiveReturns = 0;
    uint64_t repeatedFramePtrReturns = 0;
    uint64_t completedFramePtrs = 0;
    uint64_t pixelHashChanges = 0;
    uint64_t pixelHashRepeatsOnNewFramePtr = 0;
    uint64_t formatMismatchFrames = 0;
    uint64_t receivedInterarrivalOverruns = 0;
    uint64_t estimatedMissedReceivedCadenceSlots = 0;
    double maxReceivedGapMs = 0.0;
    std::map<std::tuple<int, int, int>, uint64_t> receivedFormats;
};

void printProgress(
    const Options& options,
    const Observations& observations,
    double elapsed,
    const std::string& status) {
    std::cout
        << "{\"event\":\"progress\",\"elapsedSeconds\":" << elapsed
        << ",\"sourceName\":" << jsonQuote(options.sourceName)
        << ",\"outputName\":" << jsonQuote(options.outputName)
        << ",\"publishedFrames\":" << observations.publishedFrames
        << ",\"completedFramePtrs\":" << observations.completedFramePtrs
        << ",\"pixelHashChanges\":" << observations.pixelHashChanges
        << ",\"formatMismatchFrames\":" << observations.formatMismatchFrames
        << ",\"receiverStatus\":" << jsonQuote(status) << "}\n"
        << std::flush;
}

} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        auto options = parseOptions(argc, argv);
        if (!options) {
            usage(argv[0]);
            return 64;
        }
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        auto publisher = rvx::makeSyphonBackend();
        auto receiver = rvx::makeSyphonBackend();
        if (!publisher || !receiver) {
            std::cerr << "failed to create RVX Syphon backends\n";
            return 10;
        }

        rvx::IoSettings publishSettings;
        publishSettings.publish = true;
        publishSettings.publisherName = options->sourceName;
        const rvx::Format workingFormat{kWidth, kHeight, kRateNumerator, kRateDenominator};

        pumpRunLoop(0.02);
        if (!exactMatches(*receiver, options->sourceName).empty()) {
            std::cerr << "source name already exists: " << options->sourceName << "\n";
            return 11;
        }

        publisher->publish(publishSettings, makePattern(0));
        rvx::VideoSource publishedSource;
        auto publicationStart = Clock::now();
        while (elapsedSeconds(publicationStart) < 1.0 && publishedSource.id.empty()) {
            pumpRunLoop(0.005);
            auto matches = exactMatches(*receiver, options->sourceName);
            if (matches.size() > 1) {
                std::cerr << "source name became ambiguous: " << options->sourceName << "\n";
                return 12;
            }
            if (matches.size() == 1) publishedSource = matches.front();
        }
        if (publishedSource.id.empty()) {
            std::cerr << "published source was not discovered: " << options->sourceName << "\n";
            return 13;
        }

        std::cout
            << "{\"event\":\"ready\",\"pid\":" << getpid()
            << ",\"sourceName\":" << jsonQuote(options->sourceName)
            << ",\"expectedOutputName\":" << jsonQuote(options->outputName)
            << ",\"durationSeconds\":" << options->duration
            << ",\"format\":{\"width\":" << kWidth << ",\"height\":" << kHeight
            << ",\"channels\":" << kChannels << ",\"rateNumerator\":" << kRateNumerator
            << ",\"rateDenominator\":" << kRateDenominator
            << ",\"nominalFps\":" << kNominalFps << "}"
            << ",\"publishedSource\":{\"application\":"
            << jsonQuote(publishedSource.application) << ",\"name\":"
            << jsonQuote(publishedSource.name) << "}"
            << ",\"publisherStatus\":" << jsonQuote(publisher->status()) << "}\n"
            << std::flush;

        Observations observations;
        observations.publishedFrames = 1;
        uint64_t lastPublishedIndex = 0;
        rvx::IoSettings receiveSettings;
        rvx::VideoSource selectedOutput;
        rvx::FramePtr previousFrame;
        uint64_t previousHash = 0;
        Clock::time_point firstReceivedAt{};
        Clock::time_point previousReceivedAt{};
        Clock::time_point lastReceivedAt{};
        auto start = Clock::now();
        double nextProgress = options->progressInterval > 0.0
            ? options->progressInterval : INFINITY;

        while (!stopRequested && elapsedSeconds(start) < options->duration) {
            auto now = Clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            uint64_t dueIndex = static_cast<uint64_t>(std::floor(elapsed * kNominalFps));
            if (dueIndex > lastPublishedIndex) {
                if (dueIndex > lastPublishedIndex + 1)
                    observations.missedPublishCadenceSlots += dueIndex - lastPublishedIndex - 1;
                observations.maxPublishStartLatenessMs = std::max(
                    observations.maxPublishStartLatenessMs,
                    (elapsed - dueIndex * kFrameInterval) * 1000.0);
                publisher->publish(publishSettings, makePattern(dueIndex));
                ++observations.publishedFrames;
                lastPublishedIndex = dueIndex;
            }

            ++observations.outputDiscoveryPolls;
            auto matches = exactMatches(*receiver, options->outputName);
            if (matches.empty()) {
                ++observations.outputAbsentPolls;
            }
            else if (matches.size() > 1) {
                ++observations.outputAmbiguousPolls;
            }
            else {
                if (selectedOutput.id.empty() || selectedOutput.id != matches.front().id) {
                    if (!selectedOutput.id.empty()) ++observations.outputIdentityChanges;
                    selectedOutput = matches.front();
                    receiveSettings.sourceId = selectedOutput.id;
                    receiveSettings.sourceApplication = selectedOutput.application;
                    receiveSettings.sourceName = selectedOutput.name;
                    receiveSettings.holdLast = false;
                    previousFrame.reset();
                }

                ++observations.receivePolls;
                rvx::FramePtr frame = receiver->receive(
                    receiveSettings, workingFormat, observations.receivePolls, elapsed);
                if (!frame) {
                    ++observations.nullReceiveReturns;
                }
                else if (frame == previousFrame) {
                    ++observations.repeatedFramePtrReturns;
                }
                else {
                    auto receivedAt = Clock::now();
                    uint64_t hash = hashPixels(*frame);
                    if (observations.completedFramePtrs > 0) {
                        if (hash != previousHash) ++observations.pixelHashChanges;
                        else ++observations.pixelHashRepeatsOnNewFramePtr;
                        double gap = std::chrono::duration<double>(
                            receivedAt - previousReceivedAt).count();
                        observations.maxReceivedGapMs = std::max(
                            observations.maxReceivedGapMs, gap * 1000.0);
                        if (gap > kFrameInterval * 1.5) {
                            ++observations.receivedInterarrivalOverruns;
                            uint64_t slots = static_cast<uint64_t>(
                                std::llround(gap / kFrameInterval));
                            if (slots > 1)
                                observations.estimatedMissedReceivedCadenceSlots += slots - 1;
                        }
                    }
                    else {
                        firstReceivedAt = receivedAt;
                    }
                    previousReceivedAt = receivedAt;
                    lastReceivedAt = receivedAt;
                    ++observations.completedFramePtrs;
                    ++observations.receivedFormats[
                        {frame->width, frame->height, frame->channels}];
                    if (frame->width != kWidth || frame->height != kHeight ||
                        frame->channels != kChannels) {
                        ++observations.formatMismatchFrames;
                    }
                    previousHash = hash;
                    previousFrame = std::move(frame);
                }
            }

            elapsed = elapsedSeconds(start);
            if (elapsed >= nextProgress) {
                printProgress(*options, observations, elapsed, receiver->status());
                nextProgress += options->progressInterval;
            }
            double untilNextPublish =
                (lastPublishedIndex + 1) * kFrameInterval - elapsed;
            pumpRunLoop(std::clamp(untilNextPublish, 0.0005, 0.003));
        }

        double actualDuration = elapsedSeconds(start);
        double activeReceiveSeconds = 0.0;
        double activeReceiveFps = 0.0;
        if (observations.completedFramePtrs >= 2) {
            activeReceiveSeconds = std::chrono::duration<double>(
                lastReceivedAt - firstReceivedAt).count();
            if (activeReceiveSeconds > 0.0) {
                activeReceiveFps = static_cast<double>(observations.completedFramePtrs - 1) /
                    activeReceiveSeconds;
            }
        }
        double observedWindow = std::min(options->duration, actualDuration);
        double observedInterior = std::nextafter(observedWindow, 0.0);
        uint64_t nominalSlots = static_cast<uint64_t>(
            std::floor(observedInterior * kNominalFps)) + 1;
        uint64_t receivedShortfall = nominalSlots > observations.completedFramePtrs
            ? nominalSlots - observations.completedFramePtrs : 0;
        bool passed = observations.completedFramePtrs >= 3 &&
            observations.pixelHashChanges >= 2 && observations.formatMismatchFrames == 0 &&
            observations.outputAmbiguousPolls == 0;

        std::cout
            << "{\"event\":\"summary\",\"passed\":" << (passed ? "true" : "false")
            << ",\"interrupted\":" << (stopRequested ? "true" : "false")
            << ",\"requestedDurationSeconds\":" << options->duration
            << ",\"actualDurationSeconds\":" << actualDuration
            << ",\"sourceName\":" << jsonQuote(options->sourceName)
            << ",\"expectedOutputName\":" << jsonQuote(options->outputName)
            << ",\"nominalFps\":" << kNominalFps
            << ",\"publishedFrames\":" << observations.publishedFrames
            << ",\"nominalCadenceSlotsInObservedWindow\":" << nominalSlots
            << ",\"missedPublishCadenceSlots\":"
            << observations.missedPublishCadenceSlots
            << ",\"maxPublishStartLatenessMs\":"
            << observations.maxPublishStartLatenessMs
            << ",\"outputDiscoveryPolls\":" << observations.outputDiscoveryPolls
            << ",\"outputAbsentPolls\":" << observations.outputAbsentPolls
            << ",\"outputAmbiguousPolls\":" << observations.outputAmbiguousPolls
            << ",\"outputIdentityChanges\":" << observations.outputIdentityChanges
            << ",\"selectedOutput\":{\"application\":"
            << jsonQuote(selectedOutput.application) << ",\"name\":"
            << jsonQuote(selectedOutput.name) << "}"
            << ",\"receivePolls\":" << observations.receivePolls
            << ",\"nullReceiveReturns\":" << observations.nullReceiveReturns
            << ",\"repeatedFramePtrReturns\":"
            << observations.repeatedFramePtrReturns
            << ",\"completedFramePtrs\":" << observations.completedFramePtrs
            << ",\"pixelHashChanges\":" << observations.pixelHashChanges
            << ",\"pixelHashRepeatsOnNewFramePtr\":"
            << observations.pixelHashRepeatsOnNewFramePtr
            << ",\"formatMismatchFrames\":" << observations.formatMismatchFrames
            << ",\"receivedFormats\":[";
        bool firstFormat = true;
        for (const auto& [format, count] : observations.receivedFormats) {
            if (!firstFormat) std::cout << ",";
            firstFormat = false;
            const auto [width, height, channels] = format;
            std::cout
                << "{\"width\":" << width << ",\"height\":" << height
                << ",\"channels\":" << channels << ",\"frames\":" << count << "}";
        }
        std::cout
            << "]"
            << ",\"activeReceiveWindowSeconds\":" << activeReceiveSeconds
            << ",\"receivedFpsInActiveWindow\":" << activeReceiveFps
            << ",\"receivedToNominalFpsRatio\":" << (activeReceiveFps / kNominalFps)
            << ",\"receivedShortfallFramesVsObservedWindowNominal\":"
            << receivedShortfall
            << ",\"receivedInterarrivalOverrunCount\":"
            << observations.receivedInterarrivalOverruns
            << ",\"estimatedMissedReceivedCadenceSlots\":"
            << observations.estimatedMissedReceivedCadenceSlots
            << ",\"maxReceivedGapMs\":" << observations.maxReceivedGapMs
            << ",\"publisherStatus\":" << jsonQuote(publisher->status())
            << ",\"receiverStatus\":" << jsonQuote(receiver->status())
            << ",\"semantics\":{"
            << "\"completedFramePtrs\":\"new RVX receiver FramePtr identities, not caller ticks\","
            << "\"pixelHashChanges\":\"full completed 720x480 float-frame payload hash changes\","
            << "\"receivedFormats\":\"RVX working-frame formats after backend conversion; the public backend API does not expose native Syphon source dimensions\","
            << "\"fpsAndOverruns\":\"Syphon has no FPS metadata; rates and interarrival overruns are steady-clock observations\","
            << "\"observedWindowShortfall\":\"includes time before the expected output server was discovered\"}}\n"
            << std::flush;

        rvx::IoSettings off;
        publisher->publish(off, {});
        return stopRequested ? 130 : (passed ? 0 : 20);
    }
}
