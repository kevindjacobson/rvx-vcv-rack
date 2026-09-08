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
    plugin->addModel(modelRvxFrameDelay);
    rack::plugin::plugins.push_back(plugin);

    using rvx::rackadapter::FrameDelayModule;
    static_assert(FrameDelayModule::CLEAR_PARAM == rvx::kDelayClearParam,
        "Clear parameter identity must remain stable");
    static_assert(FrameDelayModule::FRAMES_PARAM == rvx::kDelayFramesParam,
        "Frames must be appended at parameter index 1");
    static_assert(FrameDelayModule::IMAGE_INPUT == 0,
        "Image input identity must remain stable");
    static_assert(FrameDelayModule::CLEAR_INPUT == 1,
        "Clear gate identity must remain stable");
    static_assert(FrameDelayModule::IMAGE_OUTPUT == 0,
        "Image output identity must remain stable");

    using rvx::rackadapter::StatusSeverity;
    using rvx::rackadapter::classifyStatus;
    assert(classifyStatus("Ready") == StatusSeverity::Healthy);
    assert(classifyStatus("receiving Camera / Main; publishing RVX")
        == StatusSeverity::Healthy);
    assert(classifyStatus("Bypassed") == StatusSeverity::Healthy);
    assert(classifyStatus("Syphon ready; publisher waiting for frame")
        == StatusSeverity::Waiting);
    assert(classifyStatus("video graph exceeds node limit")
        == StatusSeverity::Problem);
    assert(classifyStatus("Syphon input source is ambiguous")
        == StatusSeverity::Problem);
    assert(classifyStatus("backend unavailable") == StatusSeverity::Problem);
    assert(classifyStatus("unrecognized backend state") == StatusSeverity::Problem);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    context.engine = new rack::engine::Engine;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    // Use Rack's actual parameter quantity and module JSON paths so native
    // editing, reset, patch persistence, and legacy omission stay coherent.
    {
        FrameDelayModule delay;
        rack::engine::ParamQuantity* frames =
            delay.getParamQuantity(FrameDelayModule::FRAMES_PARAM);
        assert(delay.getNumParams() == 2);
        assert(delay.getNumInputs() == 2);
        assert(delay.getNumOutputs() == 1);
        assert(frames->snapEnabled);
        assert(frames->getMinValue() == static_cast<float>(rvx::kMinDelayFrames));
        assert(frames->getMaxValue() == static_cast<float>(rvx::kMaxDelayFrames));
        assert(frames->getDefaultValue() == static_cast<float>(rvx::kDefaultDelayFrames));
        assert(frames->getValue() == static_cast<float>(rvx::kDefaultDelayFrames));
        assert(delay.node()->params[rvx::kDelayFramesParam].load()
            == static_cast<float>(rvx::kDefaultDelayFrames));

        frames->setDisplayValueString("61");
        assert(frames->getValue() == static_cast<float>(rvx::kMaxDelayFrames));
        frames->setDisplayValueString("0");
        assert(frames->getValue() == static_cast<float>(rvx::kMinDelayFrames));
        frames->setDisplayValueString("29.6");
        assert(frames->getValue() == 30.f);
        rvx::rackadapter::FrameCountDisplay readout;
        readout.quantity = frames;
        readout.step();
        assert(readout.text == "30");
        delay.process({48000.f, 1.f / 48000.f, 0});
        assert(delay.node()->params[rvx::kDelayFramesParam].load() == 30.f);

        const uint64_t beforeClear = delay.node()->resets.load();
        delay.inputs[FrameDelayModule::CLEAR_INPUT].channels = 1;
        delay.inputs[FrameDelayModule::CLEAR_INPUT].setVoltage(10.f);
        delay.process({48000.f, 1.f / 48000.f, 1});
        assert(delay.node()->resets.load() == beforeClear + 1);
        delay.process({48000.f, 1.f / 48000.f, 2});
        assert(delay.node()->resets.load() == beforeClear + 1);
        delay.inputs[FrameDelayModule::CLEAR_INPUT].setVoltage(0.f);
        delay.process({48000.f, 1.f / 48000.f, 3});
        delay.params[FrameDelayModule::CLEAR_PARAM].setValue(1.f);
        delay.process({48000.f, 1.f / 48000.f, 4});
        assert(delay.node()->resets.load() == beforeClear + 2);

        const uint64_t beforeModuleReset = delay.node()->resets.load();
        delay.onReset(rack::engine::Module::ResetEvent{});
        assert(frames->getValue() == static_cast<float>(rvx::kDefaultDelayFrames));
        assert(delay.node()->params[rvx::kDelayFramesParam].load()
            == static_cast<float>(rvx::kDefaultDelayFrames));
        assert(delay.node()->resets.load() == beforeModuleReset + 1);
        delay.process({48000.f, 1.f / 48000.f, 5});
        assert(delay.node()->params[rvx::kDelayFramesParam].load()
            == static_cast<float>(rvx::kDefaultDelayFrames));
    }

    {
        FrameDelayModule* legacy =
            static_cast<FrameDelayModule*>(modelRvxFrameDelay->createModule());
        legacy->params[FrameDelayModule::FRAMES_PARAM].setValue(42.f);
        legacy->process({48000.f, 1.f / 48000.f, 0});
        json_t* oldPatch = moduleJson("FrameDelay", NULL);
        json_object_set_new(oldPatch, "params",
            json_pack("[{s:i,s:f}]", "id", FrameDelayModule::CLEAR_PARAM, "value", 0.0));
        legacy->fromJson(oldPatch);
        json_decref(oldPatch);
        assert(legacy->params[FrameDelayModule::FRAMES_PARAM].getValue()
            == static_cast<float>(rvx::kDefaultDelayFrames));
        assert(legacy->node()->params[rvx::kDelayFramesParam].load()
            == static_cast<float>(rvx::kDefaultDelayFrames));

        legacy->params[FrameDelayModule::FRAMES_PARAM].setValue(30.f);
        oldPatch = moduleJson("FrameDelay", NULL);
        legacy->fromJson(oldPatch);
        json_decref(oldPatch);
        assert(legacy->params[FrameDelayModule::FRAMES_PARAM].getValue()
            == static_cast<float>(rvx::kDefaultDelayFrames));
        assert(legacy->node()->params[rvx::kDelayFramesParam].load()
            == static_cast<float>(rvx::kDefaultDelayFrames));
        delete legacy;
    }

    {
        FrameDelayModule* saved =
            static_cast<FrameDelayModule*>(modelRvxFrameDelay->createModule());
        saved->getParamQuantity(FrameDelayModule::FRAMES_PARAM)->setDisplayValueString("42");
        json_t* patch = saved->toJson();
        FrameDelayModule* restored =
            static_cast<FrameDelayModule*>(modelRvxFrameDelay->createModule());
        restored->fromJson(patch);
        json_decref(patch);
        assert(restored->params[FrameDelayModule::FRAMES_PARAM].getValue() == 42.f);
        assert(restored->node()->params[rvx::kDelayFramesParam].load() == 42.f);
        delete saved;
        delete restored;
    }
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    delete context.engine;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    context.engine = NULL;

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

        // A native-only restore must not reinterpret a live user rename as
        // automatic publisher metadata, including when the name collides.
        for (int colliding = 0; colliding < 2; ++colliding) {
            restored = moduleJson("VideoIo", savedIo("RVX", "RVX", 300));
            ioA->fromJson(restored);
            json_decref(restored);
            const std::string defaultBefore = ioA->defaultPublisherName;
            rvx::IoSettings renamed = ioA->node()->ioSettings();
            renamed.publisherName = "Program";
            ioA->node()->setIoSettings(renamed);
            ServiceRegistry::instance().updatePublisherName(ioA->node(), NULL);
            if (colliding) {
                renamed = ioB->node()->ioSettings();
                renamed.publisherName = "Program";
                ioB->node()->setIoSettings(renamed);
                ServiceRegistry::instance().updatePublisherName(ioB->node(), NULL);
            }
            restored = moduleJson("VideoIo", NULL, true);
            ioA->fromJson(restored);
            json_decref(restored);
            assert(ioA->node()->ioSettings().publisherName == "Program");
            assert(ioA->defaultPublisherName == defaultBefore);
            assert(ioA->node()->bypass.load());
            assert(ServiceRegistry::instance().publisherNameConflict(ioA->node()->key)
                == static_cast<bool>(colliding));
        }

        // Restore ioB's original automatic reservation for the existing cases.
        restored = moduleJson("VideoIo", savedIo("RVX 2", "RVX 2", 301));
        ioB->fromJson(restored);
        json_decref(restored);

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

    // Exercise the real capture callback and renderer across a cable gap.
    {
        rvx::rackadapter::CvBridgeModule bridge;
        bridge.params[0].setValue(1.f);
        bridge.params[1].setValue(1.f);
        bridge.inputs[1].channels = 1; // Test fixture: Rack engine connection bookkeeping.
        bridge.inputs[1].setVoltage(1.f);
        rvx::Renderer renderer;
        const rvx::Format format{4, 1, 30000, 1001};
        const double period = 1001.0 / 30000.0;
        rvx::Graph graph{{bridge.node()}, {}, 1};
        int64_t frame = 0;
        auto capture = [&](int count) {
            for (int i = 0; i < count; ++i, ++frame)
                bridge.process({48000.f, 1.f / 48000.f, frame});
        };
        auto expect = [&](float value) {
            for (float pixel : bridge.node()->display()->outputs[0]->pixels)
                assert(std::abs(pixel - value) < 1e-4f);
        };
        capture(2000);
        renderer.render(graph, format, 0, 0);
        expect(1.f);
        bridge.inputs[1].channels = 0;
        bridge.onPortChange({false, rack::engine::Port::INPUT, 1});
        capture(2800);
        renderer.render(graph, format, 1, period);
        expect(0.f);
        renderer.render(graph, format, 2, 2 * period);
        expect(0.f);
        bridge.inputs[1].channels = 1; // Test fixture: Rack engine connection bookkeeping.
        bridge.inputs[1].setVoltage(20.f);
        bridge.onPortChange({true, rack::engine::Port::INPUT, 1});
        capture(1);
        renderer.render(graph, format, 3, 3 * period);
        expect(0.f);
        capture(2000);
        renderer.render(graph, format, 4, 4 * period);
        expect(20.f);
    }

    // Queue loss is another capture discontinuity, counted once per burst.
    {
        rvx::rackadapter::CvBridgeModule bridge;
        bridge.inputs[1].channels = 1; // Test fixture: Rack engine connection bookkeeping.
        for (int64_t frame = 0; frame < 32770; ++frame)
            bridge.process({48000.f, 1.f / 48000.f, frame});
        assert(bridge.node()->audio.dropped.load() == 3);
        assert(bridge.node()->resets.load() == 1);
        rvx::AudioSample sample;
        while (bridge.node()->audio.pop(sample)) {}
        const uint64_t priorEpoch = sample.epoch;
        bridge.process({48000.f, 1.f / 48000.f, 32770});
        assert(bridge.node()->audio.pop(sample));
        assert(sample.epoch == priorEpoch + 1);
    }

    rack::plugin::plugins.clear();
    delete plugin;
    rack::contextSet(NULL);
    std::cout << "rack host adapter tests passed\n";
    return 0;
}
