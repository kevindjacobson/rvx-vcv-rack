#include "NativeValidationJson.hpp"
#include "../src/io/VideoBackend.hpp"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <unistd.h>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using rvx::native_validation::jsonQuote;
constexpr int kWidth = 720, kHeight = 480, kChannels = 4;
constexpr int kRateNumerator = 30000, kRateDenominator = 1001;
constexpr double kNominalFps = double(kRateNumerator) / kRateDenominator;
constexpr double kFrameInterval = double(kRateDenominator) / kRateNumerator;
constexpr double kMaximumDuration = 3600.0, kDefaultStartupTimeout = 15.0;
constexpr double kMaximumStartupTimeout = 300.0, kMinimumCadenceRatio = 0.99;
constexpr double kMaximumSkippedSequenceRatio = 0.01;
constexpr double kMaximumEndFreshness = kFrameInterval * 3.0;
constexpr float kPixelTolerance = 2.5f / 255.f;
constexpr size_t kPixelsPerFrame = size_t(kWidth) * kHeight;
constexpr size_t kChannelsPerFrame = kPixelsPerFrame * kChannels;
volatile std::sig_atomic_t stopRequested = 0;

enum class Mode { Observe, Loopback, VerifyRelay, SelfTest };

void signalHandler(int) { stopRequested = 1; }
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
    auto add = [&hash](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    add(&frame.width, sizeof(frame.width));
    add(&frame.height, sizeof(frame.height));
    add(&frame.channels, sizeof(frame.channels));
    if (!frame.pixels.empty()) add(frame.pixels.data(), frame.pixels.size() * sizeof(float));
    return hash;
}

std::array<float, 4> patternPixel(int x, int y, uint64_t nonce, uint64_t sequence) {
    static constexpr float bars[8][3] = {
        {1, 1, 1}, {1, 1, 0}, {0, 1, 1}, {0, 1, 0},
        {1, 0, 1}, {1, 0, 0}, {0, 0, 1}, {0, 0, 0},
    };
    int bar = std::min(7, x * 8 / kWidth);
    std::array<float, 4> pixel{
        bars[bar][0] * .75f, bars[bar][1] * .75f, bars[bar][2] * .75f, 1.f};
    int movingX = int((sequence * 7) % kWidth);
    int movingY = int((sequence * 3) % kHeight);
    if ((x - movingX + kWidth) % kWidth < 18) {
        float value = ((y / 12 + int(sequence & 1u)) & 1) ? 1.f : 0.f;
        pixel[0] = pixel[1] = pixel[2] = value;
    }
    if ((y - movingY + kHeight) % kHeight < 4)
        pixel = {1.f, .25f, 0.f, 1.f};

    uint64_t header = 0;
    int bit = -1;
    if (y < 16 && x < 64 * 8) {
        header = nonce;
        bit = x / 8;
    }
    else if (y < 32 && y >= 16 && x < 32 * 8) {
        header = sequence;
        bit = x / 8;
    }
    if (bit >= 0) {
        float value = ((header >> bit) & 1u) ? 1.f : 0.f;
        pixel = {value, 1.f - value, .25f, 1.f};
    }
    return pixel;
}

rvx::FramePtr makePattern(uint64_t nonce, uint64_t sequence) {
    auto frame = std::make_shared<rvx::Frame>();
    frame->width = kWidth;
    frame->height = kHeight;
    frame->channels = kChannels;
    frame->sequence = sequence;
    frame->seconds = sequence * kFrameInterval;
    frame->pixels.resize(size_t(kWidth) * kHeight * kChannels);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            auto pixel = patternPixel(x, y, nonce, sequence);
            size_t index = (size_t(y) * kWidth + x) * kChannels;
            std::copy(pixel.begin(), pixel.end(), frame->pixels.begin() + index);
        }
    }
    return frame;
}

bool closePixel(float actual, float expected) {
    return std::abs(actual - expected) <= kPixelTolerance;
}
std::optional<uint64_t> decodeBits(const rvx::Frame& frame, int y, int bitCount) {
    uint64_t value = 0;
    for (int bit = 0; bit < bitCount; ++bit) {
        size_t i = (size_t(y) * kWidth + bit * 8 + 4) * kChannels;
        float red = frame.pixels[i], green = frame.pixels[i + 1];
        if (!closePixel(frame.pixels[i + 2], .25f) || std::abs(red - green) < .5f)
            return std::nullopt;
        if (red > green) value |= uint64_t{1} << bit;
    }
    return value;
}
enum class PatternFailure { None, MalformedFrame, NonFiniteChannel, HeaderDecode, FullFrameMismatch };
struct PatternCheck {
    PatternFailure failure = PatternFailure::None;
    uint64_t nonce = 0, sequence = 0;
    size_t comparedPixels = 0, comparedChannels = 0;
};
PatternCheck checkPattern(const rvx::Frame& frame) {
    PatternCheck result;
    if (frame.width != kWidth || frame.height != kHeight || frame.channels != kChannels ||
        frame.pixels.size() != kChannelsPerFrame) {
        result.failure = PatternFailure::MalformedFrame;
        return result;
    }
    // Validate storage and every channel before decoding coordinates or comparing values.
    if (std::any_of(frame.pixels.begin(), frame.pixels.end(),
                    [](float value) { return !std::isfinite(value); })) {
        result.failure = PatternFailure::NonFiniteChannel;
        return result;
    }
    auto nonce = decodeBits(frame, 8, 64);
    auto sequence = decodeBits(frame, 24, 32);
    if (!nonce || !sequence) {
        result.failure = PatternFailure::HeaderDecode;
        return result;
    }
    result.nonce = *nonce;
    result.sequence = *sequence;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            auto expected = patternPixel(x, y, result.nonce, result.sequence);
            size_t i = (size_t(y) * kWidth + x) * kChannels;
            for (int c = 0; c < kChannels; ++c) {
                ++result.comparedChannels;
                if (!closePixel(frame.pixels[i + c], expected[c])) {
                    result.failure = PatternFailure::FullFrameMismatch;
                    return result;
                }
            }
            ++result.comparedPixels;
        }
    }
    return result;
}

std::vector<rvx::VideoSource> exactMatches(
    const std::vector<rvx::VideoSource>& sources,
    const std::string& application,
    const std::string& name) {
    std::vector<rvx::VideoSource> matches;
    for (const auto& source : sources)
        if (source.application == application && source.name == name) matches.push_back(source);
    return matches;
}
std::vector<rvx::VideoSource> exactMatches(
    rvx::VideoBackend& backend, const std::string& application, const std::string& name) {
    return exactMatches(backend.sources(), application, name);
}
std::vector<rvx::VideoSource> nameMatches(rvx::VideoBackend& backend, const std::string& name) {
    std::vector<rvx::VideoSource> matches;
    for (const auto& source : backend.sources()) if (source.name == name) matches.push_back(source);
    return matches;
}
std::string nonceText(uint64_t nonce) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << nonce;
    return out.str();
}
uint64_t makeNonce() {
    std::random_device random;
    uint64_t nonce = (uint64_t(random()) << 32) ^ random();
    nonce ^= uint64_t(getpid()) << 17;
    nonce ^= uint64_t(Clock::now().time_since_epoch().count());
    return nonce ? nonce : 1;
}

struct Options {
    Mode mode = Mode::Observe;
    double duration = 0, progress = 10, startupTimeout = kDefaultStartupTimeout;
    std::string sourceName, outputApplication, outputName;
};
void usage(const char* program) {
    std::cerr
        << "usage: " << program
        << " --duration SECONDS --output-application EXACT_APP --output-name EXACT_NAME"
           " [--source-name UNIQUE_NAME] [--progress SECONDS]\n"
        << "       " << program
        << " --verify-relay --duration MEASURED_SECONDS [--startup-timeout SECONDS]"
           " --output-application EXACT_APP --output-name EXACT_NAME"
           " [--source-name UNIQUE_NAME] [--progress SECONDS]\n"
        << "       " << program
        << " --loopback --duration SECONDS [--source-name UNIQUE_NAME] [--progress SECONDS]\n";
}
bool parseNumber(const char* text, double& result) {
    char* end = nullptr;
    result = std::strtod(text, &end);
    return end && *end == '\0' && std::isfinite(result);
}
std::optional<Options> parseOptions(int argc, char** argv) {
    Options o;
    bool duration = false, progress = false, startup = false, mode = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--duration" && i + 1 < argc) {
            if (duration || !parseNumber(argv[++i], o.duration)) return {};
            duration = true;
        }
        else if (a == "--progress" && i + 1 < argc) {
            if (progress || !parseNumber(argv[++i], o.progress)) return {};
            progress = true;
        }
        else if (a == "--startup-timeout" && i + 1 < argc) {
            if (startup || !parseNumber(argv[++i], o.startupTimeout)) return {};
            startup = true;
        }
        else if (a == "--source-name" && i + 1 < argc) {
            if (!o.sourceName.empty()) return {};
            o.sourceName = argv[++i];
        }
        else if (a == "--output-application" && i + 1 < argc) {
            if (!o.outputApplication.empty()) return {};
            o.outputApplication = argv[++i];
        }
        else if (a == "--output-name" && i + 1 < argc) {
            if (!o.outputName.empty()) return {};
            o.outputName = argv[++i];
        }
        else if (a == "--loopback") {
            if (mode) return {};
            mode = true; o.mode = Mode::Loopback;
        }
        else if (a == "--verify-relay") {
            if (mode) return {};
            mode = true; o.mode = Mode::VerifyRelay;
        }
        else if (a == "--self-test" && argc == 2) { o.mode = Mode::SelfTest; return o; }
        else if (a == "--help") { usage(argv[0]); std::exit(0); }
        else return {};
    }
    if (!duration || o.duration <= 0 || o.duration > kMaximumDuration ||
        o.progress < 0 || o.progress > kMaximumDuration || o.startupTimeout <= 0 ||
        o.startupTimeout > kMaximumStartupTimeout || (startup && o.mode != Mode::VerifyRelay))
        return {};
    if (o.sourceName.empty()) o.sourceName = "RVX validation source pid " + std::to_string(getpid());
    if (o.mode == Mode::Loopback) {
        if (!o.outputApplication.empty() || (!o.outputName.empty() && o.outputName != o.sourceName))
            return {};
        o.outputName = o.sourceName;
    }
    else if (o.outputApplication.empty() || o.outputName.empty() || o.outputName == o.sourceName)
        return {};
    return o;
}

struct Observations {
    uint64_t published = 0, missedPublishSlots = 0;
    double maxPublishLatenessMs = 0;
    uint64_t discoveryPolls = 0, absentPolls = 0, ambiguousPolls = 0, identityChanges = 0;
    uint64_t receivePolls = 0, nullReturns = 0, repeatedPtrs = 0, completedPtrs = 0;
    uint64_t hashChanges = 0, hashRepeats = 0, formatMismatches = 0;
    uint64_t patternFailures = 0, malformedFrames = 0, nonFiniteFrames = 0;
    uint64_t headerDecodeFailures = 0, fullFrameMismatchFrames = 0;
    uint64_t fullFrameChecks = 0, fullFrameMatches = 0;
    uint64_t fullFramePixelsCompared = 0, fullFrameChannelsCompared = 0;
    uint64_t nonceMismatches = 0, validContent = 0;
    uint64_t sequenceRepeats = 0, sequenceRegressions = 0, skippedSequences = 0;
    uint64_t interarrivalOverruns = 0, estimatedMissedSlots = 0;
    double maxGapMs = 0;
    std::map<std::tuple<int, int, int>, uint64_t> formats;
    std::vector<double> gaps;
    std::vector<double> fullFrameCheckMs;
};
bool relayPasses(const Observations& s, bool startupComplete, double actualMeasured,
                 double requestedMeasured, double cadenceRatio,
                 double skippedRatio, double endFreshness, bool interrupted = false) {
    return !interrupted && startupComplete && actualMeasured >= requestedMeasured && s.validContent >= 3 &&
        s.absentPolls == 0 && s.ambiguousPolls == 0 && s.identityChanges == 0 &&
        s.formatMismatches == 0 && s.patternFailures == 0 && s.nonceMismatches == 0 &&
        s.sequenceRepeats == 0 && s.sequenceRegressions == 0 &&
        cadenceRatio >= kMinimumCadenceRatio && skippedRatio < kMaximumSkippedSequenceRatio &&
        endFreshness <= kMaximumEndFreshness;
}
double sum(const std::vector<double>& values) {
    double result = 0;
    for (double value : values) result += value;
    return result;
}
double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    size_t i = std::clamp<size_t>(size_t(std::ceil(fraction * values.size())), 1, values.size());
    return values[i - 1];
}
void printProgress(const Options& o, const Observations& s, double elapsed,
                   const char* phase, const std::string& status) {
    std::cout << "{\"event\":\"progress\",\"phase\":" << jsonQuote(phase)
              << ",\"elapsedSeconds\":" << elapsed
              << ",\"publishedFrames\":" << s.published
              << ",\"completedFramePtrs\":" << s.completedPtrs
              << ",\"contentValidFrames\":" << s.validContent
              << ",\"receiverStatus\":" << jsonQuote(status) << "}\n" << std::flush;
}
bool runSelfTest() {
    std::vector<rvx::VideoSource> sources{{"one", "Rack", "output"}, {"two", "Other", "output"}};
    if (exactMatches(sources, "Rack", "output").size() != 1 ||
        !exactMatches(sources, "Wrong", "output").empty()) return false;
    constexpr uint64_t nonce = 0xa59c3187de42f06bull;
    auto frame = makePattern(nonce, 123456);
    auto checked = checkPattern(*frame);
    if (checked.failure != PatternFailure::None || checked.nonce != nonce ||
        checked.sequence != 123456 || checked.comparedPixels != kPixelsPerFrame ||
        checked.comparedChannels != kChannelsPerFrame) return false;
    auto foreignFrame = makePattern(nonce ^ 1u, 123456);
    auto foreignCheck = checkPattern(*foreignFrame);
    if (foreignCheck.failure != PatternFailure::None || foreignCheck.nonce == nonce) return false;

    rvx::Frame quantized = *frame;
    for (float& value : quantized.pixels) value = std::round(value * 255.f) / 255.f;
    if (checkPattern(quantized).failure != PatternFailure::None) return false;

    // This body coordinate is outside both protected headers and all historical sentinels.
    constexpr int corruptX = 350, corruptY = 220;
    static constexpr int oldXs[] = {3, 97, 191, 287, 383, 479, 575, 701};
    static constexpr int oldYs[] = {47, 103, 181, 263, 347, 431};
    if (corruptY < 32 || std::find(std::begin(oldXs), std::end(oldXs), corruptX) != std::end(oldXs) ||
        std::find(std::begin(oldYs), std::end(oldYs), corruptY) != std::end(oldYs)) return false;
    size_t corruptIndex = (size_t(corruptY) * kWidth + corruptX) * kChannels;
    for (int channel = 0; channel < kChannels; ++channel) {
        rvx::Frame corrupt = *frame;
        float expected = corrupt.pixels[corruptIndex + channel];
        corrupt.pixels[corruptIndex + channel] = expected > .5f ? 0.f : 1.f;
        if (checkPattern(corrupt).failure != PatternFailure::FullFrameMismatch) return false;
    }

    rvx::Frame withinTolerance = *frame;
    withinTolerance.pixels[corruptIndex] += kPixelTolerance * .5f;
    if (checkPattern(withinTolerance).failure != PatternFailure::None) return false;
    rvx::Frame outsideTolerance = *frame;
    outsideTolerance.pixels[corruptIndex] += kPixelTolerance * 1.5f;
    if (checkPattern(outsideTolerance).failure != PatternFailure::FullFrameMismatch) return false;

    rvx::Frame flipped = *frame;
    for (int y = 32; y < kHeight; ++y) {
        for (int x = 0; x < kWidth / 2; ++x) {
            size_t left = (size_t(y) * kWidth + x) * kChannels;
            size_t right = (size_t(y) * kWidth + (kWidth - 1 - x)) * kChannels;
            for (int c = 0; c < kChannels; ++c)
                std::swap(flipped.pixels[left + c], flipped.pixels[right + c]);
        }
    }
    if (checkPattern(flipped).failure != PatternFailure::FullFrameMismatch) return false;

    rvx::Frame wrongContent = *frame;
    auto nextPattern = makePattern(nonce, 123457);
    std::copy(nextPattern->pixels.begin() + size_t(32) * kWidth * kChannels,
              nextPattern->pixels.end(),
              wrongContent.pixels.begin() + size_t(32) * kWidth * kChannels);
    if (checkPattern(wrongContent).failure != PatternFailure::FullFrameMismatch) return false;

    for (int channel = 0; channel < kChannels; ++channel) {
        for (float invalid : {NAN, INFINITY, -INFINITY}) {
            rvx::Frame nonFinite = *frame;
            nonFinite.pixels[corruptIndex + channel] = invalid;
            if (checkPattern(nonFinite).failure != PatternFailure::NonFiniteChannel) return false;
        }
    }
    rvx::Frame malformed = *frame;
    malformed.width = kWidth - 1;
    if (checkPattern(malformed).failure != PatternFailure::MalformedFrame) return false;
    malformed = *frame;
    malformed.channels = kChannels - 1;
    if (checkPattern(malformed).failure != PatternFailure::MalformedFrame) return false;
    malformed = *frame;
    malformed.pixels.pop_back();
    if (checkPattern(malformed).failure != PatternFailure::MalformedFrame) return false;
    malformed = *frame;
    malformed.pixels.push_back(0.f);
    if (checkPattern(malformed).failure != PatternFailure::MalformedFrame) return false;

    Observations healthy;
    healthy.validContent = 60;
    if (!relayPasses(healthy, true, 2, 2, 1, 0, kFrameInterval)) return false;

    // Each event independently invalidates a continuous, identity-bound relay.
    const std::pair<const char*, uint64_t Observations::*> invalidEvents[] = {
        {"missing output", &Observations::absentPolls},
        {"ambiguous output", &Observations::ambiguousPolls},
        {"replacement output", &Observations::identityChanges},
        {"wrong format", &Observations::formatMismatches},
        {"damaged pattern", &Observations::patternFailures},
        {"unrelated source", &Observations::nonceMismatches},
        {"replayed source frame", &Observations::sequenceRepeats},
        {"out-of-order source frame", &Observations::sequenceRegressions},
    };
    for (const auto& [name, counter] : invalidEvents) {
        auto sample = healthy;
        sample.*counter = 1;
        if (relayPasses(sample, true, 2, 2, 1, 0, kFrameInterval)) {
            std::cerr << "incorrect relay acceptance: " << name << "\n";
            return false;
        }
    }
    if (relayPasses(healthy, true, 2, 2, 1, 0, kFrameInterval, true)) {
        std::cerr << "incorrect relay acceptance: interrupted run\n";
        return false;
    }
    struct TimingCase {
        const char* name;
        uint64_t validFrames;
        bool started;
        double elapsed, cadence, skipped, freshness;
    };
    const TimingCase invalidTiming[] = {
        {"startup never completed", 60, false, 2, 1, 0, kFrameInterval},
        {"measurement ended early", 60, true, 1, 1, 0, kFrameInterval},
        {"insufficient source frames", 2, true, 2, 1, 0, kFrameInterval},
        {"reduced receive cadence", 60, true, 2, .98, 0, kFrameInterval},
        {"excess source sequence loss", 60, true, 2, 1, .01, kFrameInterval},
        // The server stays discoverable after three early frames. Endpoint FPS
        // alone can look healthy; stale final content must still reject it.
        {"discoverable but stale output", 3, true, 2, 1, 0, 1.5},
    };
    for (const auto& test : invalidTiming) {
        auto sample = healthy;
        sample.validContent = test.validFrames;
        if (relayPasses(sample, test.started, test.elapsed, 2, test.cadence,
                        test.skipped, test.freshness)) {
            std::cerr << "incorrect relay acceptance: " << test.name << "\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        auto options = parseOptions(argc, argv);
        if (!options) { usage(argv[0]); return 64; }
        if (options->mode == Mode::SelfTest) {
            bool passed = runSelfTest();
            std::cout << "{\"event\":\"selfTest\",\"passed\":" << (passed ? "true" : "false") << "}\n";
            return passed ? 0 : 70;
        }
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
        auto publisher = rvx::makeSyphonBackend(), receiver = rvx::makeSyphonBackend();
        if (!publisher || !receiver) { std::cerr << "failed to create Syphon backends\n"; return 10; }

        uint64_t nonce = makeNonce();
        rvx::IoSettings publishSettings;
        publishSettings.publish = true;
        publishSettings.publisherName = options->sourceName;
        rvx::Format format{kWidth, kHeight, kRateNumerator, kRateDenominator};
        pumpRunLoop(.02);
        if (!nameMatches(*receiver, options->sourceName).empty()) {
            std::cerr << "source name already exists: " << options->sourceName << "\n"; return 11;
        }
        publisher->publish(publishSettings, makePattern(nonce, 0));
        rvx::VideoSource publishedSource;
        auto publicationStart = Clock::now();
        while (elapsedSeconds(publicationStart) < 1 && publishedSource.id.empty()) {
            pumpRunLoop(.005);
            auto matches = nameMatches(*receiver, options->sourceName);
            if (matches.size() > 1) { std::cerr << "source name became ambiguous\n"; return 12; }
            if (matches.size() == 1) publishedSource = matches.front();
        }
        if (publishedSource.id.empty()) { std::cerr << "published source was not discovered\n"; return 13; }
        if (options->mode == Mode::Loopback) options->outputApplication = publishedSource.application;
        const char* modeName = options->mode == Mode::VerifyRelay ? "verifyRelay" :
            (options->mode == Mode::Loopback ? "loopback" : "observe");
        std::cout << "{\"event\":\"ready\",\"mode\":" << jsonQuote(modeName)
                  << ",\"pid\":" << getpid()
                  << ",\"expectedOutputApplication\":" << jsonQuote(options->outputApplication)
                  << ",\"expectedOutputName\":" << jsonQuote(options->outputName)
                  << ",\"nonceHex\":" << jsonQuote(nonceText(nonce))
                  << ",\"measuredDurationSeconds\":" << options->duration;
        if (options->mode == Mode::VerifyRelay)
            std::cout << ",\"startupTimeoutSeconds\":" << options->startupTimeout;
        std::cout << ",\"publishedSource\":{\"application\":" << jsonQuote(publishedSource.application)
                  << ",\"name\":" << jsonQuote(publishedSource.name) << "}}\n" << std::flush;

        Observations s;
        s.published = 1;
        uint64_t lastPublished = 0, previousHash = 0;
        rvx::VideoSource selected;
        rvx::IoSettings receiveSettings;
        rvx::FramePtr previousFrame;
        std::optional<uint64_t> previousSequence;
        Clock::time_point firstReceived{}, previousReceived{}, lastReceived{}, lastValid{};
        auto runStart = Clock::now(), phaseStart = runStart;
        bool startup = options->mode == Mode::VerifyRelay, startupSucceeded = !startup;
        double nextProgress = options->progress > 0 ? options->progress : INFINITY;

        auto resetMeasurement = [&] {
            uint64_t published = s.published, missed = s.missedPublishSlots;
            double lateness = s.maxPublishLatenessMs;
            s = {};
            s.published = published; s.missedPublishSlots = missed; s.maxPublishLatenessMs = lateness;
            previousFrame.reset(); previousSequence.reset(); previousHash = 0;
            firstReceived = previousReceived = lastReceived = lastValid = {};
        };

        while (!stopRequested) {
            double phaseElapsed = elapsedSeconds(phaseStart);
            if (startup) {
                if (startupSucceeded) {
                    startup = false;
                    double startupElapsed = elapsedSeconds(runStart);
                    resetMeasurement();
                    phaseStart = Clock::now();
                    nextProgress = options->progress > 0 ? options->progress : INFINITY;
                    std::cout << "{\"event\":\"measurementStarted\",\"startupElapsedSeconds\":"
                              << startupElapsed << ",\"measuredDurationSeconds\":"
                              << options->duration << "}\n" << std::flush;
                }
                else if (phaseElapsed >= options->startupTimeout) break;
            }
            else if (phaseElapsed >= options->duration) break;

            double runElapsed = elapsedSeconds(runStart);
            uint64_t due = uint64_t(std::floor(runElapsed * kNominalFps));
            if (due > lastPublished) {
                if (due > lastPublished + 1) s.missedPublishSlots += due - lastPublished - 1;
                s.maxPublishLatenessMs = std::max(s.maxPublishLatenessMs,
                    (runElapsed - due * kFrameInterval) * 1000);
                publisher->publish(publishSettings, makePattern(nonce, due));
                ++s.published; lastPublished = due;
            }

            ++s.discoveryPolls;
            auto matches = exactMatches(*receiver, options->outputApplication, options->outputName);
            if (matches.empty()) ++s.absentPolls;
            else if (matches.size() > 1) ++s.ambiguousPolls;
            else {
                if (selected.id.empty() || selected.id != matches.front().id) {
                    if (!selected.id.empty()) ++s.identityChanges;
                    selected = matches.front();
                    receiveSettings.sourceId = selected.id;
                    receiveSettings.sourceApplication = selected.application;
                    receiveSettings.sourceName = selected.name;
                    receiveSettings.holdLast = false;
                    previousFrame.reset();
                }
                ++s.receivePolls;
                auto frame = receiver->receive(receiveSettings, format, s.receivePolls, runElapsed);
                if (!frame) ++s.nullReturns;
                else if (frame == previousFrame) ++s.repeatedPtrs;
                else {
                    auto receivedAt = Clock::now();
                    uint64_t hash = hashPixels(*frame);
                    if (s.completedPtrs) {
                        hash == previousHash ? ++s.hashRepeats : ++s.hashChanges;
                        double gap = std::chrono::duration<double>(receivedAt - previousReceived).count();
                        s.gaps.push_back(gap); s.maxGapMs = std::max(s.maxGapMs, gap * 1000);
                        if (gap > kFrameInterval * 1.5) {
                            ++s.interarrivalOverruns;
                            uint64_t slots = uint64_t(std::llround(gap / kFrameInterval));
                            if (slots > 1) s.estimatedMissedSlots += slots - 1;
                        }
                    }
                    else firstReceived = receivedAt;
                    previousReceived = lastReceived = receivedAt;
                    ++s.completedPtrs;
                    ++s.formats[{frame->width, frame->height, frame->channels}];
                    if (frame->width != kWidth || frame->height != kHeight || frame->channels != kChannels)
                        ++s.formatMismatches;
                    if (options->mode == Mode::VerifyRelay || options->mode == Mode::Loopback) {
                        auto checkStarted = Clock::now();
                        auto checked = checkPattern(*frame);
                        double checkMs = std::chrono::duration<double, std::milli>(
                            Clock::now() - checkStarted).count();
                        ++s.fullFrameChecks;
                        s.fullFramePixelsCompared += checked.comparedPixels;
                        s.fullFrameChannelsCompared += checked.comparedChannels;
                        s.fullFrameCheckMs.push_back(checkMs);
                        if (checked.failure != PatternFailure::None) {
                            ++s.patternFailures;
                            if (checked.failure == PatternFailure::MalformedFrame) ++s.malformedFrames;
                            else if (checked.failure == PatternFailure::NonFiniteChannel) ++s.nonFiniteFrames;
                            else if (checked.failure == PatternFailure::HeaderDecode) ++s.headerDecodeFailures;
                            else if (checked.failure == PatternFailure::FullFrameMismatch)
                                ++s.fullFrameMismatchFrames;
                        }
                        else if (checked.nonce != nonce) {
                            ++s.fullFrameMatches;
                            ++s.nonceMismatches;
                        }
                        else {
                            ++s.fullFrameMatches;
                            ++s.validContent; lastValid = receivedAt;
                            if (previousSequence) {
                                if (checked.sequence == *previousSequence) ++s.sequenceRepeats;
                                else if (checked.sequence < *previousSequence) ++s.sequenceRegressions;
                                else if (checked.sequence > *previousSequence + 1)
                                    s.skippedSequences += checked.sequence - *previousSequence - 1;
                            }
                            previousSequence = checked.sequence;
                            if (startup) startupSucceeded = true;
                        }
                    }
                    previousHash = hash; previousFrame = std::move(frame);
                }
            }
            phaseElapsed = elapsedSeconds(phaseStart);
            if (phaseElapsed >= nextProgress) {
                printProgress(*options, s, phaseElapsed, startup ? "startup" : "measurement", receiver->status());
                nextProgress += options->progress;
            }
            runElapsed = elapsedSeconds(runStart);
            pumpRunLoop(std::clamp((lastPublished + 1) * kFrameInterval - runElapsed, .0005, .003));
        }

        double actualMeasured = startupSucceeded && !startup ? elapsedSeconds(phaseStart) : 0;
        double activeSeconds = s.completedPtrs >= 2
            ? std::chrono::duration<double>(lastReceived - firstReceived).count() : 0;
        double activeFps = activeSeconds > 0 ? double(s.completedPtrs - 1) / activeSeconds : 0;
        double cadenceRatio = activeFps / kNominalFps;
        uint64_t sequenceSpan = s.validContent + s.skippedSequences;
        double skippedRatio = sequenceSpan ? double(s.skippedSequences) / sequenceSpan : 1;
        double endFreshness = lastValid == Clock::time_point{} ? INFINITY
            : std::chrono::duration<double>(Clock::now() - lastValid).count();
        bool relayPassed = options->mode == Mode::VerifyRelay && relayPasses(
            s, startupSucceeded && !startup, actualMeasured, options->duration,
            cadenceRatio, skippedRatio, endFreshness, stopRequested);
        bool loopbackPassed = options->mode == Mode::Loopback && !stopRequested && s.completedPtrs >= 3 &&
            s.hashChanges >= 2 && s.absentPolls == 0 && s.ambiguousPolls == 0 &&
            s.identityChanges == 0 && s.formatMismatches == 0 && s.validContent >= 3 &&
            s.patternFailures == 0 && s.nonceMismatches == 0 && s.sequenceRegressions == 0;

        std::cout << "{\"event\":\"summary\",\"mode\":" << jsonQuote(modeName);
        if (options->mode == Mode::Observe) std::cout << ",\"transportAssertion\":null";
        else std::cout << ",\"passed\":" << (relayPassed || loopbackPassed ? "true" : "false");
        std::cout << ",\"interrupted\":" << (stopRequested ? "true" : "false")
                  << ",\"startupSucceeded\":" << (startupSucceeded ? "true" : "false")
                  << ",\"requestedMeasuredDurationSeconds\":" << options->duration
                  << ",\"actualMeasuredDurationSeconds\":" << actualMeasured
                  << ",\"totalRunDurationSeconds\":" << elapsedSeconds(runStart)
                  << ",\"sourceName\":" << jsonQuote(options->sourceName)
                  << ",\"expectedOutputApplication\":" << jsonQuote(options->outputApplication)
                  << ",\"expectedOutputName\":" << jsonQuote(options->outputName)
                  << ",\"nonceHex\":" << jsonQuote(nonceText(nonce))
                  << ",\"nominalFps\":" << kNominalFps
                  << ",\"publishedFrames\":" << s.published
                  << ",\"missedPublishCadenceSlots\":" << s.missedPublishSlots
                  << ",\"maxPublishStartLatenessMs\":" << s.maxPublishLatenessMs
                  << ",\"outputDiscoveryPolls\":" << s.discoveryPolls
                  << ",\"outputAbsentPolls\":" << s.absentPolls
                  << ",\"outputAmbiguousPolls\":" << s.ambiguousPolls
                  << ",\"outputIdentityChanges\":" << s.identityChanges
                  << ",\"selectedOutput\":{\"application\":" << jsonQuote(selected.application)
                  << ",\"name\":" << jsonQuote(selected.name) << "}"
                  << ",\"receivePolls\":" << s.receivePolls
                  << ",\"nullReceiveReturns\":" << s.nullReturns
                  << ",\"repeatedFramePtrReturns\":" << s.repeatedPtrs
                  << ",\"completedFramePtrs\":" << s.completedPtrs
                  << ",\"pixelHashChanges\":" << s.hashChanges
                  << ",\"pixelHashRepeatsOnNewFramePtr\":" << s.hashRepeats
                  << ",\"formatMismatchFrames\":" << s.formatMismatches
                  << ",\"patternDecodeFailures\":" << s.patternFailures
                  << ",\"patternVerificationFailures\":" << s.patternFailures
                  << ",\"malformedFrameFailures\":" << s.malformedFrames
                  << ",\"nonFiniteFrameFailures\":" << s.nonFiniteFrames
                  << ",\"headerDecodeFailures\":" << s.headerDecodeFailures
                  << ",\"fullFrameMismatchFrames\":" << s.fullFrameMismatchFrames
                  << ",\"nonceMismatchFrames\":" << s.nonceMismatches
                  << ",\"contentValidFrames\":" << s.validContent
                  << ",\"sequenceRepeatFrames\":" << s.sequenceRepeats
                  << ",\"sequenceRegressionFrames\":" << s.sequenceRegressions
                  << ",\"skippedSourceSequenceFrames\":" << s.skippedSequences
                  << ",\"skippedSourceSequenceRatio\":" << skippedRatio
                  << ",\"activeReceiveWindowSeconds\":" << activeSeconds
                  << ",\"receivedFpsInActiveWindow\":" << activeFps
                  << ",\"receivedToNominalFpsRatio\":" << cadenceRatio
                  << ",\"receivedInterarrivalP99Ms\":" << percentile(s.gaps, .99) * 1000
                  << ",\"receivedInterarrivalOverrunCount\":" << s.interarrivalOverruns
                  << ",\"estimatedMissedReceivedCadenceSlots\":" << s.estimatedMissedSlots
                  << ",\"maxReceivedGapMs\":" << s.maxGapMs
                  << ",\"endFreshnessSeconds\":";
        if (std::isfinite(endFreshness)) std::cout << endFreshness; else std::cout << "null";
        double totalFullFrameCheckMs = sum(s.fullFrameCheckMs);
        std::cout << ",\"fullFrameVerification\":{"
                  << "\"coverage\":\"every RGBA channel of every 720x480 pixel\","
                  << "\"pixelsPerFrame\":" << kPixelsPerFrame
                  << ",\"channelsPerPixel\":" << kChannels
                  << ",\"channelsPerFrame\":" << kChannelsPerFrame
                  << ",\"perChannelTolerance\":" << kPixelTolerance
                  << ",\"finiteChannelsRequired\":true"
                  << ",\"attemptedFrames\":" << s.fullFrameChecks
                  << ",\"matchedFrames\":" << s.fullFrameMatches
                  << ",\"comparedPixels\":" << s.fullFramePixelsCompared
                  << ",\"comparedChannels\":" << s.fullFrameChannelsCompared
                  << ",\"costMs\":{\"total\":" << totalFullFrameCheckMs
                  << ",\"mean\":" << (s.fullFrameCheckMs.empty() ? 0 : totalFullFrameCheckMs / s.fullFrameCheckMs.size())
                  << ",\"p50\":" << percentile(s.fullFrameCheckMs, .50)
                  << ",\"p95\":" << percentile(s.fullFrameCheckMs, .95)
                  << ",\"p99\":" << percentile(s.fullFrameCheckMs, .99)
                  << ",\"max\":" << percentile(s.fullFrameCheckMs, 1.0) << "}}"
                  << ",\"receivedFormats\":[";
        bool first = true;
        for (const auto& [key, count] : s.formats) {
            if (!first) std::cout << ",";
            first = false;
            auto [width, height, channels] = key;
            std::cout << "{\"width\":" << width << ",\"height\":" << height
                      << ",\"channels\":" << channels << ",\"frames\":" << count << "}";
        }
        std::cout << "]"
                  << ",\"limits\":{\"minimumCadenceRatio\":" << kMinimumCadenceRatio
                  << ",\"maximumSkippedSourceSequenceRatio\":" << kMaximumSkippedSequenceRatio
                  << ",\"maximumEndFreshnessSeconds\":" << kMaximumEndFreshness << "}"
                  << ",\"semantics\":{"
                  << "\"observationMode\":\"records exact-identity observations and makes no transport pass assertion\","
                  << "\"verifyRelayMode\":\"starts measured duration only after exact-identity nonce-bound full-frame content arrives\","
                  << "\"receivedFormats\":\"working formats after backend conversion; native source dimensions are not exposed\","
                  << "\"fpsAndOverruns\":\"Syphon has no FPS metadata; rates and gaps use the steady clock\"}}\n"
                  << std::flush;
        rvx::IoSettings off;
        publisher->publish(off, {});
        if (stopRequested) return 130;
        if (options->mode == Mode::Observe) return 0;
        return relayPassed || loopbackPassed ? 0 : 20;
    }
}
