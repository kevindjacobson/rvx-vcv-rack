#include "../src/rack/Modules.cpp"
#include "../src/io/VideoBackend.hpp"

#include <cassert>
#include <iostream>

Plugin* pluginInstance = NULL;

namespace rvx {
std::unique_ptr<VideoBackend> makeSyphonBackend() {
    return std::unique_ptr<VideoBackend>();
}
} // namespace rvx

namespace {

json_t* savedIo(const char* publisher, const char* defaultPublisher, int64_t owner) {
    return json_pack("{s:i,s:s,s:s,s:i}",
        "rvxSchema", 1,
        "publisherName", publisher,
        "defaultPublisherName", defaultPublisher,
        "publisherOwnerModuleId", owner);
}

void restore(rvx::rackadapter::VideoIoModule& module, json_t* saved) {
    module.dataFromJson(saved);
    json_decref(saved);
    module.onAdd(rack::engine::Module::AddEvent{});
}

} // namespace

int main() {
    rack::Context context;
    rack::contextSet(&context);
    using rvx::rackadapter::ServiceRegistry;
    using rvx::rackadapter::VideoIoModule;

    {
        VideoIoModule restored;
        restored.id = 100;
        restore(restored, savedIo("RVX 2", "RVX 2", 100));

        VideoIoModule added;
        added.id = 101;
        added.onAdd(rack::engine::Module::AddEvent{});

        VideoIoModule duplicated;
        duplicated.id = 102;
        restore(duplicated, savedIo("RVX", "RVX", 101));

        assert(restored.node()->ioSettings().publisherName == "RVX 2");
        assert(added.node()->ioSettings().publisherName == "RVX");
        assert(duplicated.node()->ioSettings().publisherName == "RVX 3");

        VideoIoModule customA;
        customA.id = 103;
        restore(customA, savedIo("Program", "RVX 4", 103));
        VideoIoModule customB;
        customB.id = 104;
        restore(customB, savedIo("Program", "RVX 5", 104));
        assert(customA.node()->ioSettings().publisherName == "Program");
        assert(customB.node()->ioSettings().publisherName == "Program");
        assert(ServiceRegistry::instance().publisherNameConflict(customA.node()->key));
        assert(ServiceRegistry::instance().publisherNameConflict(customB.node()->key));
    }

    // Module destruction releases the reservation for a later restored patch.
    {
        VideoIoModule restoredAgain;
        restoredAgain.id = 200;
        restore(restoredAgain, savedIo("RVX 2", "RVX 2", 200));
        assert(restoredAgain.node()->ioSettings().publisherName == "RVX 2");
    }

    const rack::engine::Module::ProcessArgs first{48000.f, 1.f / 48000.f, 10};
    const rack::engine::Module::ProcessArgs discontinuous{48000.f, 1.f / 48000.f, 12};
    const rack::engine::Module::SampleRateChangeEvent rateChange{96000.f, 1.f / 96000.f};
    {
        rvx::rackadapter::TestImageModule source;
        source.process(first);
        source.process(discontinuous);
        source.onSampleRateChange(rateChange);
        assert(source.node()->resets.load() == 0);

        rvx::rackadapter::FrameDelayModule delay;
        delay.process(first);
        delay.process(discontinuous);
        delay.onSampleRateChange(rateChange);
        assert(delay.node()->resets.load() == 0);

        rvx::rackadapter::CvBridgeModule bridge;
        bridge.process(first);
        bridge.process(discontinuous);
        assert(bridge.node()->resets.load() == 1);
        bridge.onSampleRateChange(rateChange);
        assert(bridge.node()->resets.load() == 2);
    }

    rack::contextSet(NULL);
    std::cout << "rack host adapter tests passed\n";
    return 0;
}
