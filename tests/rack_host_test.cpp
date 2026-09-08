#include "../src/rack/Modules.cpp"
#include "../src/io/VideoBackend.hpp"
#include <plugin.hpp>

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

json_t* moduleJson(const char* model, json_t* data, bool bypass = false) {
    json_t* root = json_pack("{s:s,s:s,s:b}",
        "plugin", "RVX", "model", model, "bypass", bypass);
    if (data)
        json_object_set_new(root, "data", data);
    return root;
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

    rack::plugin::Plugin* plugin = new rack::plugin::Plugin;
    plugin->slug = "RVX";
    plugin->version = "2.0.0";
    plugin->addModel(modelRvxTestImage);
    plugin->addModel(modelRvxVideoIo);
    rack::plugin::plugins.push_back(plugin);

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
        source.outputs[rvx::rackadapter::TestImageModule::IMAGE_OUTPUT].setVoltage(5.f);
        source.process(first);
        assert(source.outputs[rvx::rackadapter::TestImageModule::IMAGE_OUTPUT].getVoltage() == 0.f);
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

    // Rack restores params and bypass before calling dataFromJson(), and may
    // omit data entirely. The shared render state must be coherent immediately.
    {
        rvx::rackadapter::TestImageModule* source =
            static_cast<rvx::rackadapter::TestImageModule*>(modelRvxTestImage->createModule());
        source->onAdd(rack::engine::Module::AddEvent{});
        VideoIoModule* ioA = static_cast<VideoIoModule*>(modelRvxVideoIo->createModule());
        ioA->id = 300;
        ioA->onAdd(rack::engine::Module::AddEvent{});
        VideoIoModule* ioB = static_cast<VideoIoModule*>(modelRvxVideoIo->createModule());
        ioB->id = 301;
        ioB->onAdd(rack::engine::Module::AddEvent{});
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
        context.engine = new rack::engine::Engine;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        json_t* restored = moduleJson("TestImage", NULL, true);
        json_object_set_new(restored, "params",
            json_pack("[{s:i,s:f}]", "id", 1, "value", 2.0));
        source->fromJson(restored);
        json_decref(restored);
        assert(source->isBypassed());
        assert(source->node()->bypass.load());
        assert(source->params[1].getValue() == 2.f);
        assert(source->node()->params[1].load() == 2.f);
        assert(source->node()->resets.load() == 1);

        // Existing-module state restore re-reserves automatic names and keeps
        // explicit duplicate names visible instead of silently rewriting them.
        restored = moduleJson("VideoIo", savedIo("RVX 2", "RVX 2", 300));
        ioA->fromJson(restored);
        json_decref(restored);
        assert(ioA->node()->ioSettings().publisherName == "RVX");
        restored = moduleJson("VideoIo", savedIo("Program", "RVX", 300));
        ioA->fromJson(restored);
        json_decref(restored);
        restored = moduleJson("VideoIo", savedIo("Program", "RVX 2", 301));
        ioB->fromJson(restored);
        json_decref(restored);
        assert(ioA->node()->ioSettings().publisherName == "Program");
        assert(ioB->node()->ioSettings().publisherName == "Program");
        assert(ServiceRegistry::instance().publisherNameConflict(ioA->node()->key));

        delete source;
        delete ioA;
        delete ioB;
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
        delete context.engine;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        context.engine = NULL;
    }

    rack::plugin::plugins.clear();
    delete plugin;
    rack::contextSet(NULL);
    std::cout << "rack host adapter tests passed\n";
    return 0;
}
