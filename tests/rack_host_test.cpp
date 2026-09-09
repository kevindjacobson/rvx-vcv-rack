#include "../src/rack/Modules.cpp"
#include "../src/io/VideoBackend.hpp"
#include <plugin.hpp>

#include <cassert>
#include <iostream>
#include <limits>

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
    plugin->addModel(modelRvxVideoRecorder);
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

    using rvx::rackadapter::VideoRecorderModule;
    static_assert(VideoRecorderModule::RECORD_PARAM == 0);
    static_assert(VideoRecorderModule::PLAY_PARAM == 1);
    static_assert(VideoRecorderModule::POSITION_PARAM == 2);
    static_assert(VideoRecorderModule::SPEED_PARAM == 3);
    static_assert(VideoRecorderModule::LOOP_PARAM == 4);
    static_assert(VideoRecorderModule::CLEAR_PARAM == 5);
    static_assert(VideoRecorderModule::CAPACITY_PARAM == 6);
    static_assert(VideoRecorderModule::IMAGE_INPUT == 0);
    static_assert(VideoRecorderModule::POSITION_INPUT == 1);
    static_assert(VideoRecorderModule::IMAGE_OUTPUT == 0);

    using rvx::rackadapter::StatusSeverity;
    using rvx::rackadapter::classifyStatus;
    assert(classifyStatus("Ready") == StatusSeverity::Healthy);
    assert(classifyStatus("receiving Camera / Main; publishing RVX")
        == StatusSeverity::Healthy);
    assert(classifyStatus("receiving Chocolate / Late Show; publishing Error Studies")
        == StatusSeverity::Healthy);
    assert(classifyStatus("Bypassed") == StatusSeverity::Healthy);
    assert(classifyStatus("recording") == StatusSeverity::Healthy);
    assert(classifyStatus("playing") == StatusSeverity::Healthy);
    assert(classifyStatus("position CV override") == StatusSeverity::Healthy);
    assert(classifyStatus("playback endpoint hold") == StatusSeverity::Healthy);
    assert(classifyStatus("empty clip") == StatusSeverity::Healthy);
    assert(classifyStatus("clip retained; release Record to rearm") == StatusSeverity::Healthy);
    assert(classifyStatus("clip cleared: format change") == StatusSeverity::Healthy);
    assert(classifyStatus("recording: missing valid image input") == StatusSeverity::Problem);
    assert(classifyStatus("non-finite position CV") == StatusSeverity::Problem);
    assert(classifyStatus("Syphon ready; publisher waiting for frame")
        == StatusSeverity::Waiting);
    assert(classifyStatus("Starting video worker") == StatusSeverity::Waiting);
    assert(classifyStatus("Module preview") == StatusSeverity::Waiting);
    assert(classifyStatus("video graph exceeds node limit")
        == StatusSeverity::Problem);
    assert(classifyStatus("Syphon input source is ambiguous")
        == StatusSeverity::Problem);
    assert(classifyStatus("Syphon input could not connect; publishing RVX")
        == StatusSeverity::Problem);
    assert(classifyStatus("backend unavailable") == StatusSeverity::Problem);
    assert(classifyStatus("unrecognized backend state") == StatusSeverity::Problem);
    assert(classifyStatus("unrecognized backend state; publishing RVX")
        == StatusSeverity::Problem);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    context.engine = new rack::engine::Engine;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    // Actual SDK adapter fixtures exercise capture/restore, not native clicks.
    {
        VideoRecorderModule recorder;
        assert(recorder.getNumParams() == 7);
        assert(recorder.getNumInputs() == 2);
        assert(recorder.getNumOutputs() == 1);
        assert(rvx::inputType(rvx::Kind::Recorder, 0) == rvx::PortType::Image);
        assert(rvx::inputType(rvx::Kind::Recorder, 1) == rvx::PortType::Audio);
        assert(rvx::outputType(rvx::Kind::Recorder, 0) == rvx::PortType::Image);
        const float defaults[] = {0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 60.f};
        for (int i = 0; i < VideoRecorderModule::NUM_PARAMS; ++i) {
            assert(recorder.getParamQuantity(i)->getValue() == defaults[i]);
            assert(recorder.node()->params[i].load() == defaults[i]);
        }
        auto* capacity = recorder.getParamQuantity(VideoRecorderModule::CAPACITY_PARAM);
        assert(capacity->snapEnabled);
        assert(capacity->getMinValue() == 1.f);
        assert(capacity->getMaxValue() == 60.f);
        capacity->setDisplayValueString("19.6");
        assert(capacity->getValue() == 20.f);
        capacity->setDisplayValueString("100");
        assert(capacity->getValue() == 60.f);
        capacity->setDisplayValueString("0");
        assert(capacity->getValue() == 1.f);
        auto* position = recorder.getParamQuantity(VideoRecorderModule::POSITION_PARAM);
        position->setDisplayValueString("75");
        assert(position->getValue() == 0.75f);
        assert(position->getDisplayValue() == 75.f);
        auto* speed = recorder.getParamQuantity(VideoRecorderModule::SPEED_PARAM);
        assert(speed->getMinValue() == -4.f);
        assert(speed->getMaxValue() == 4.f);
        recorder.process({48000.f, 1.f / 48000.f, 0});
        assert(!recorder.node()->recorderPositionConnected.load());
        recorder.inputs[VideoRecorderModule::POSITION_INPUT].channels = 1;
        recorder.inputs[VideoRecorderModule::POSITION_INPUT].setVoltage(7.5f);
        recorder.process({48000.f, 1.f / 48000.f, 1});
        assert(recorder.node()->recorderPositionConnected.load());
        assert(recorder.node()->cvVoltage.load() == 7.5f);
        recorder.inputs[VideoRecorderModule::POSITION_INPUT].setVoltage(
            std::numeric_limits<float>::quiet_NaN());
        recorder.process({48000.f, 1.f / 48000.f, 2});
        assert(recorder.node()->recorderPositionConnected.load());
        assert(std::isnan(recorder.node()->cvVoltage.load()));
        recorder.inputs[VideoRecorderModule::POSITION_INPUT].channels = 0;
        recorder.process({48000.f, 1.f / 48000.f, 3});
        assert(!recorder.node()->recorderPositionConnected.load());

        const uint64_t beforeClear = recorder.node()->resets.load();
        recorder.params[VideoRecorderModule::CLEAR_PARAM].setValue(1.f);
        recorder.process({48000.f, 1.f / 48000.f, 4});
        recorder.process({48000.f, 1.f / 48000.f, 5});
        assert(recorder.node()->resets.load() == beforeClear + 1);
        recorder.params[VideoRecorderModule::CLEAR_PARAM].setValue(0.f);
        recorder.process({48000.f, 1.f / 48000.f, 6});
        assert(recorder.node()->resets.load() == beforeClear + 1);
        assert(recorder.node()->params[rvx::kRecorderClearParam].load() == 0.f);
        // The durable reset counter preserves a pulse shorter than one video tick.
        recorder.params[VideoRecorderModule::CLEAR_PARAM].setValue(1.f);
        recorder.process({48000.f, 1.f / 48000.f, 7});
        assert(recorder.node()->resets.load() == beforeClear + 2);
        recorder.outputs[0].setVoltage(5.f);
        recorder.processBypass({48000.f, 1.f / 48000.f, 8});
        assert(recorder.outputs[0].getVoltage() == 0.f);

        recorder.params[VideoRecorderModule::RECORD_PARAM].setValue(1.f);
        recorder.params[VideoRecorderModule::PLAY_PARAM].setValue(1.f);
        const uint64_t beforeReset = recorder.node()->resets.load();
        recorder.onReset(rack::engine::Module::ResetEvent{});
        assert(recorder.node()->resets.load() == beforeReset + 1);
        for (int i = 0; i < VideoRecorderModule::NUM_PARAMS; ++i) {
            assert(recorder.params[i].getValue() == defaults[i]);
            assert(recorder.node()->params[i].load() == defaults[i]);
        }

        rvx::rackadapter::RecorderCountDisplay readout;
        readout.node = recorder.node();
        readout.step();
        assert(readout.text == "EMPTY / 0 FRAMES");
        auto display = std::make_shared<rvx::NodeDisplay>();
        display->recorderFrames = 60;
        display->recorderIndex = 59;
        recorder.node()->publishDisplay(display);
        readout.step();
        assert(readout.text == "FRAME 60 / 60");
    }
    {
        auto* saved = static_cast<VideoRecorderModule*>(modelRvxVideoRecorder->createModule());
        const float settings[] = {1.f, 1.f, 0.75f, -0.5f, 0.f, 1.f, 42.f};
        for (int i = 0; i < VideoRecorderModule::NUM_PARAMS; ++i)
            saved->params[i].setValue(settings[i]);
        json_t* patch = saved->toJson();
        auto* restored = static_cast<VideoRecorderModule*>(modelRvxVideoRecorder->createModule());
        restored->fromJson(patch);
        json_decref(patch);
        const float expected[] = {0.f, 0.f, 0.75f, -0.5f, 0.f, 0.f, 42.f};
        for (int i = 0; i < VideoRecorderModule::NUM_PARAMS; ++i) {
            assert(restored->params[i].getValue() == expected[i]);
            assert(restored->node()->params[i].load() == expected[i]);
        }
        assert(restored->node()->resets.load() == 1);
        // Native-only JSON can omit custom data; restore still stops transport.
        restored->params[VideoRecorderModule::RECORD_PARAM].setValue(1.f);
        restored->params[VideoRecorderModule::PLAY_PARAM].setValue(1.f);
        patch = moduleJson("VideoRecorder", nullptr, true);
        restored->fromJson(patch);
        json_decref(patch);
        assert(restored->node()->bypass.load());
        assert(restored->node()->params[rvx::kRecorderRecordParam].load() == 0.f);
        assert(restored->node()->params[rvx::kRecorderPlayParam].load() == 0.f);
        assert(restored->node()->resets.load() == 2);
        restored->params[VideoRecorderModule::RECORD_PARAM].setValue(1.f);
        patch = json_pack("{s:i}", "rvxSchema", 1);
        restored->dataFromJson(patch);
        json_decref(patch);
        assert(restored->node()->params[rvx::kRecorderRecordParam].load() == 0.f);
        assert(restored->node()->resets.load() == 3);
        delete saved;
        delete restored;
    }

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

    // Connect Rack's real voltage capture to the renderer's retained-frame seek.
    {
        rvx::rackadapter::TestImageModule source;
        VideoRecorderModule recorder;
        const rvx::Format format{4, 1, 30000, 1001};
        const double period = 1001.0 / 30000.0;
        rvx::Graph graph{{source.node(), recorder.node()},
            {{source.node()->key, 0, recorder.node()->key, 0}}, 1};
        rvx::Renderer renderer;
        uint64_t tick = 0;
        auto render = [&]() {
            recorder.process({48000.f, 1.f / 48000.f, static_cast<int64_t>(tick)});
            renderer.render(graph, format, tick, tick * period);
            ++tick;
        };
        recorder.params[VideoRecorderModule::CAPACITY_PARAM].setValue(3.f);
        recorder.params[VideoRecorderModule::RECORD_PARAM].setValue(1.f);
        render();
        const auto firstFrame = source.node()->display()->outputs[0];
        render();
        render();
        const auto lastFrame = source.node()->display()->outputs[0];
        assert(recorder.node()->display()->recorderFrames == 3);
        recorder.params[VideoRecorderModule::RECORD_PARAM].setValue(0.f);
        recorder.params[VideoRecorderModule::POSITION_PARAM].setValue(1.f);
        render();
        assert(recorder.node()->display()->outputs[0] == lastFrame);
        recorder.inputs[VideoRecorderModule::POSITION_INPUT].channels = 1;
        recorder.inputs[VideoRecorderModule::POSITION_INPUT].setVoltage(-2.f);
        render();
        assert(recorder.node()->display()->outputs[0] == firstFrame);
        recorder.inputs[VideoRecorderModule::POSITION_INPUT].setVoltage(12.f);
        render();
        assert(recorder.node()->display()->outputs[0] == lastFrame);
        recorder.inputs[VideoRecorderModule::POSITION_INPUT].setVoltage(
            std::numeric_limits<float>::quiet_NaN());
        render();
        assert(recorder.node()->display()->outputs[0] == firstFrame);
        assert(recorder.node()->display()->status.find("non-finite position CV")
            != std::string::npos);
        recorder.inputs[VideoRecorderModule::POSITION_INPUT].channels = 0;
        render();
        assert(recorder.node()->display()->outputs[0] == lastFrame);
        assert(recorder.node()->display()->status.find("non-finite position CV")
            == std::string::npos);
        // Clear high and low occur between video ticks; the clip still clears.
        recorder.params[VideoRecorderModule::CLEAR_PARAM].setValue(1.f);
        recorder.process({48000.f, 1.f / 48000.f, static_cast<int64_t>(tick)});
        recorder.params[VideoRecorderModule::CLEAR_PARAM].setValue(0.f);
        render();
        assert(recorder.node()->display()->recorderFrames == 0);
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
