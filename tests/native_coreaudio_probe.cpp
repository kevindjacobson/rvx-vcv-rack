#include "NativeValidationJson.hpp"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using rvx::native_validation::jsonQuote;
constexpr double kMaximumDurationSeconds = 3600.0;
volatile std::sig_atomic_t stopRequested = 0;
std::atomic<uint64_t> overloadNotifications{0};

void signalHandler(int) {
    stopRequested = 1;
}

template <typename T>
std::optional<T> scalarProperty(
    AudioObjectID object,
    AudioObjectPropertySelector selector,
    AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
    if (!AudioObjectHasProperty(object, &address)) return std::nullopt;
    T value{};
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value) != noErr ||
        size != sizeof(value)) {
        return std::nullopt;
    }
    return value;
}

template <typename T>
std::vector<T> arrayProperty(
    AudioObjectID object,
    AudioObjectPropertySelector selector,
    AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (!AudioObjectHasProperty(object, &address) ||
        AudioObjectGetPropertyDataSize(object, &address, 0, nullptr, &size) != noErr || size == 0) {
        return {};
    }
    std::vector<T> values(size / sizeof(T));
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, values.data()) != noErr)
        return {};
    values.resize(size / sizeof(T));
    return values;
}

std::optional<std::string> stringProperty(
    AudioObjectID object,
    AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress address{
        selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    if (!AudioObjectHasProperty(object, &address)) return std::nullopt;
    CFStringRef value = nullptr;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value) != noErr || !value)
        return std::nullopt;
    CFIndex maximum = CFStringGetMaximumSizeForEncoding(
        CFStringGetLength(value), kCFStringEncodingUTF8) + 1;
    std::vector<char> bytes(static_cast<size_t>(maximum));
    bool converted = CFStringGetCString(value, bytes.data(), maximum, kCFStringEncodingUTF8);
    CFRelease(value);
    if (!converted) return std::nullopt;
    return std::string(bytes.data());
}

UInt32 channelCount(AudioDeviceID device, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress address{
        kAudioDevicePropertyStreamConfiguration, scope, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (!AudioObjectHasProperty(device, &address) ||
        AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr || size == 0)
        return 0;
    std::vector<unsigned char> storage(size);
    auto* buffers = reinterpret_cast<AudioBufferList*>(storage.data());
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, buffers) != noErr)
        return 0;
    UInt32 channels = 0;
    for (UInt32 i = 0; i < buffers->mNumberBuffers; ++i)
        channels += buffers->mBuffers[i].mNumberChannels;
    return channels;
}

struct Device {
    AudioDeviceID id = kAudioObjectUnknown;
    std::string name;
};

std::vector<Device> devices() {
    std::vector<Device> result;
    for (AudioDeviceID id : arrayProperty<AudioDeviceID>(
             kAudioObjectSystemObject, kAudioHardwarePropertyDevices)) {
        result.push_back({id, stringProperty(id, kAudioObjectPropertyName).value_or("")});
    }
    return result;
}

std::optional<AudioDeviceID> defaultDevice(AudioObjectPropertySelector selector) {
    return scalarProperty<AudioDeviceID>(kAudioObjectSystemObject, selector);
}

std::string statusText(OSStatus status) {
    if (status == noErr) return "noErr";
    UInt32 value = static_cast<UInt32>(status);
    char code[5] = {
        static_cast<char>((value >> 24) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>(value & 0xff),
        0,
    };
    bool printable = std::all_of(code, code + 4, [](char c) { return c >= 32 && c <= 126; });
    std::ostringstream out;
    out << status;
    if (printable) out << " ('" << code << "')";
    return out.str();
}

OSStatus overloadListener(
    AudioObjectID,
    UInt32 addressCount,
    const AudioObjectPropertyAddress addresses[],
    void*) {
    for (UInt32 i = 0; i < addressCount; ++i) {
        if (addresses[i].mSelector == kAudioDeviceProcessorOverload)
            overloadNotifications.fetch_add(1, std::memory_order_relaxed);
    }
    return noErr;
}

bool parseNumber(const char* text, double& result) {
    char* end = nullptr;
    result = std::strtod(text, &end);
    return end && *end == '\0' && std::isfinite(result);
}

void usage(const char* program) {
    std::cerr
        << "usage: " << program << "\n"
        << "       " << program << " --observe-default-output --duration SECONDS\n"
        << "       " << program << " --observe-device ID|EXACT_NAME --duration SECONDS\n"
        << "       duration must be greater than 0 and no more than 3600\n";
}

struct Options {
    bool observeDefaultOutput = false;
    std::optional<std::string> deviceSelector;
    std::optional<double> duration;
};

std::optional<Options> parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--observe-default-output") {
            if (options.observeDefaultOutput || options.deviceSelector) return std::nullopt;
            options.observeDefaultOutput = true;
        }
        else if (argument == "--observe-device" && i + 1 < argc) {
            if (options.observeDefaultOutput || options.deviceSelector) return std::nullopt;
            options.deviceSelector = argv[++i];
            if (options.deviceSelector->empty()) return std::nullopt;
        }
        else if (argument == "--duration" && i + 1 < argc) {
            double duration = 0.0;
            if (options.duration || !parseNumber(argv[++i], duration)) return std::nullopt;
            options.duration = duration;
        }
        else if (argument == "--help") {
            usage(argv[0]);
            std::exit(0);
        }
        else {
            return std::nullopt;
        }
    }
    bool observing = options.observeDefaultOutput || options.deviceSelector.has_value();
    if (observing != options.duration.has_value()) return std::nullopt;
    if (options.duration && (*options.duration <= 0.0 || *options.duration > kMaximumDurationSeconds))
        return std::nullopt;
    return options;
}

std::optional<Device> resolveDevice(
    const Options& options,
    const std::vector<Device>& available,
    std::string& error) {
    if (options.observeDefaultOutput) {
        auto id = defaultDevice(kAudioHardwarePropertyDefaultOutputDevice);
        auto match = std::find_if(available.begin(), available.end(),
                                  [&](const Device& device) { return id && device.id == *id; });
        if (match == available.end()) error = "default output device is unavailable";
        else return *match;
        return std::nullopt;
    }

    const std::string& selector = *options.deviceSelector;
    char* end = nullptr;
    unsigned long numeric = std::strtoul(selector.c_str(), &end, 10);
    if (end && *end == '\0') {
        auto match = std::find_if(available.begin(), available.end(),
                                  [&](const Device& device) { return device.id == numeric; });
        if (match == available.end()) error = "CoreAudio device ID is unavailable";
        else return *match;
        return std::nullopt;
    }

    std::vector<Device> matches;
    std::copy_if(available.begin(), available.end(), std::back_inserter(matches),
                 [&](const Device& device) { return device.name == selector; });
    if (matches.empty()) error = "exact device name is unavailable";
    else if (matches.size() > 1) error = "exact device name is ambiguous; use a current numeric ID";
    else return matches.front();
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
    auto options = parseOptions(argc, argv);
    if (!options) {
        usage(argv[0]);
        return 64;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    const auto available = devices();
    const auto defaultInput = defaultDevice(kAudioHardwarePropertyDefaultInputDevice);
    const auto defaultOutput = defaultDevice(kAudioHardwarePropertyDefaultOutputDevice);
    const auto defaultSystemOutput = defaultDevice(kAudioHardwarePropertyDefaultSystemOutputDevice);

    std::optional<Device> observed;
    std::string resolutionError;
    if (options->duration) {
        observed = resolveDevice(*options, available, resolutionError);
        if (!observed) {
            std::cerr << resolutionError << "\n";
            return 65;
        }
    }

    const AudioObjectPropertyAddress overloadAddress{
        kAudioDeviceProcessorOverload,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    bool propertyPresent = false;
    OSStatus addStatus = noErr;
    OSStatus removeStatus = noErr;
    double observedSeconds = 0.0;
    if (observed) {
        propertyPresent = AudioObjectHasProperty(observed->id, &overloadAddress);
        overloadNotifications.store(0, std::memory_order_relaxed);
        addStatus = AudioObjectAddPropertyListener(
            observed->id, &overloadAddress, overloadListener, nullptr);
        if (addStatus == noErr) {
            auto start = std::chrono::steady_clock::now();
            while (!stopRequested) {
                observedSeconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start).count();
                if (observedSeconds >= *options->duration) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            observedSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            removeStatus = AudioObjectRemovePropertyListener(
                observed->id, &overloadAddress, overloadListener, nullptr);
        }
    }

    std::cout
        << "{\"schemaVersion\":1,\"semantics\":{"
        << "\"overloadNotifications\":\"device-wide CoreAudio notifications that an I/O cycle missed its deadline; not a Rack-specific underrun count\"},"
        << "\"devices\":[";
    for (size_t i = 0; i < available.size(); ++i) {
        if (i) std::cout << ",";
        const auto& device = available[i];
        auto sampleRate = scalarProperty<Float64>(device.id, kAudioDevicePropertyNominalSampleRate);
        auto bufferFrames = scalarProperty<UInt32>(device.id, kAudioDevicePropertyBufferFrameSize);
        auto running = scalarProperty<UInt32>(device.id, kAudioDevicePropertyDeviceIsRunningSomewhere);
        std::cout
            << "{\"name\":" << jsonQuote(device.name)
            << ",\"isDefaultInput\":" << (defaultInput && device.id == *defaultInput ? "true" : "false")
            << ",\"isDefaultOutput\":" << (defaultOutput && device.id == *defaultOutput ? "true" : "false")
            << ",\"isDefaultSystemOutput\":" << (defaultSystemOutput && device.id == *defaultSystemOutput ? "true" : "false")
            << ",\"sampleRateHz\":" << (sampleRate ? std::to_string(*sampleRate) : "null")
            << ",\"bufferFrameSize\":" << (bufferFrames ? std::to_string(*bufferFrames) : "null")
            << ",\"inputChannels\":" << channelCount(device.id, kAudioObjectPropertyScopeInput)
            << ",\"outputChannels\":" << channelCount(device.id, kAudioObjectPropertyScopeOutput)
            << ",\"runningSomewhere\":";
        if (running) std::cout << (*running ? "true" : "false");
        else std::cout << "null";
        std::cout << ",\"processorOverloadPropertyPresent\":"
                  << (AudioObjectHasProperty(device.id, &overloadAddress) ? "true" : "false")
                  << "}";
    }
    std::cout << "]";
    if (observed) {
        std::cout
            << ",\"observation\":{\"deviceName\":" << jsonQuote(observed->name)
            << ",\"requestedDurationSeconds\":" << *options->duration
            << ",\"observedDurationSeconds\":" << observedSeconds
            << ",\"interrupted\":" << (stopRequested ? "true" : "false")
            << ",\"propertyPresent\":" << (propertyPresent ? "true" : "false")
            << ",\"listenerRegistrationStatus\":" << jsonQuote(statusText(addStatus))
            << ",\"listenerRemovalStatus\":" << jsonQuote(statusText(removeStatus))
            << ",\"deviceWideOverloadNotificationCount\":"
            << overloadNotifications.load(std::memory_order_relaxed) << "}";
    }
    std::cout << "}\n";
    if (observed && addStatus != noErr) return 66;
    return stopRequested ? 130 : 0;
}
