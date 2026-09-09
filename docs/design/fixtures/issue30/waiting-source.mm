// Issue #30 native fixture: advertises a real Syphon server without a frame.
// Build/run instructions and observation limits are in ../../VALIDATION.md.
#include "../../../../src/io/VideoBackend.hpp"
#import <Foundation/Foundation.h>
#include <csignal>
#include <cstdio>

namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
}

int main() {
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    @autoreleasepool {
        auto publisher = rvx::makeSyphonBackend();
        if (!publisher) return 1;
        rvx::IoSettings settings;
        settings.publish = true;
        settings.publisherName = "Healthy error missing late invalid overflow Long Source Issue30 / 720x480";
        NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:1800];
        publisher->publish(settings, {});
        std::printf("Native waiting fixture: %s\n", publisher->status().c_str());
        std::fflush(stdout);
        while (!stopped && deadline.timeIntervalSinceNow > 0) {
            [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        }
    }
    return 0;
}
