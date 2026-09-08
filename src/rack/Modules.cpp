#include "RackAdapter.hpp"
#include "../plugin.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace rack;

namespace rvx {
namespace rackadapter {

namespace {

const NVGcolor kPanel = nvgRGB(25, 28, 38);
const NVGcolor kInk = nvgRGB(232, 237, 244);
const NVGcolor kMuted = nvgRGB(142, 153, 170);
const NVGcolor kCyan = nvgRGB(77, 201, 255);
const NVGcolor kPink = nvgRGB(255, 90, 194);

struct LabelSpec {
    math::Vec position;
    std::string text;
    float size;
    NVGcolor color;
    int align;
    LabelSpec(math::Vec position, std::string text, float size = 9.f,
              NVGcolor color = kInk, int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE)
        : position(position), text(text), size(size), color(color), align(align) {}
};

struct PrototypePanel : widget::Widget {
    std::string title;
    NVGcolor accent;
    std::vector<LabelSpec> labels;

    PrototypePanel(float hp, std::string title, NVGcolor accent)
        : title(title), accent(accent) {
        box.size = math::Vec(hp * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
    }

    void label(float xMm, float yMm, std::string text, float size = 9.f,
               NVGcolor color = kInk, int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE) {
        labels.push_back(LabelSpec(mm2px(math::Vec(xMm, yMm)), text, size, color, align));
    }

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, kPanel);
        nvgFill(args.vg);

        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, mm2px(1.3f));
        nvgFillColor(args.vg, accent);
        nvgFill(args.vg);

        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f);
        nvgStrokeWidth(args.vg, 1.f);
        nvgStrokeColor(args.vg, nvgRGB(54, 60, 76));
        nvgStroke(args.vg);

        if (APP && APP->window && APP->window->uiFont)
            nvgFontFaceId(args.vg, APP->window->uiFont->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 13.f);
        nvgFillColor(args.vg, kInk);
        nvgText(args.vg, box.size.x / 2.f, mm2px(5.4f), title.c_str(), NULL);
        nvgFontSize(args.vg, 7.f);
        nvgFillColor(args.vg, kMuted);
        nvgText(args.vg, box.size.x / 2.f, mm2px(9.f), "RVX EXPERIMENTAL", NULL);

        for (size_t i = 0; i < labels.size(); ++i) {
            const LabelSpec& l = labels[i];
            nvgFontSize(args.vg, l.size);
            nvgFillColor(args.vg, l.color);
            nvgTextAlign(args.vg, l.align);
            nvgText(args.vg, l.position.x, l.position.y, l.text.c_str(), NULL);
        }
        widget::Widget::draw(args);
    }
};

void addScrews(app::ModuleWidget* widget, float width) {
    widget->addChild(createWidget<ScrewSilver>(math::Vec(RACK_GRID_WIDTH, 0)));
    widget->addChild(createWidget<ScrewSilver>(math::Vec(width - 2.f * RACK_GRID_WIDTH, 0)));
    widget->addChild(createWidget<ScrewSilver>(math::Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    widget->addChild(createWidget<ScrewSilver>(math::Vec(width - 2.f * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
}

math::Vec pos(float xMm, float yMm) {
    return mm2px(math::Vec(xMm, yMm));
}

struct TestImageModule : Module {
    enum ParamIds { PATTERN_PARAM, PHASE_SPEED_PARAM, NUM_PARAMS };
    enum OutputIds { IMAGE_OUTPUT, FIELD_OUTPUT, NUM_OUTPUTS };

    TestImageModule() : Module(Kind::TestImage) {
        config(NUM_PARAMS, 0, NUM_OUTPUTS, 0);
        configSwitch(PATTERN_PARAM, 0.f, 3.f, 0.f, "Pattern",
            std::vector<std::string>{"Color bars", "Checker", "Ramp", "Raster phase probe"});
        configParam(PHASE_SPEED_PARAM, -4.f, 4.f, 0.f, "Phase speed", " cycles/s");
        configOutput(IMAGE_OUTPUT, "Image");
        configOutput(FIELD_OUTPUT, "Field");
    }
};

struct SignalProcessorModule : Module {
    enum ParamIds { GAIN_A_PARAM, GAIN_B_PARAM, OFFSET_PARAM, MODE_PARAM, NUM_PARAMS };
    enum InputIds { A_INPUT, B_INPUT, FIELD_INPUT, NUM_INPUTS };
    enum OutputIds { IMAGE_OUTPUT, FIELD_OUTPUT, NUM_OUTPUTS };

    SignalProcessorModule() : Module(Kind::Processor) {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        configParam(GAIN_A_PARAM, -2.f, 2.f, 1.f, "Gain A");
        configParam(GAIN_B_PARAM, -2.f, 2.f, 0.f, "Gain B");
        configParam(OFFSET_PARAM, -2.f, 2.f, 0.f, "Offset");
        configSwitch(MODE_PARAM, 0.f, 4.f, 0.f, "Mode",
            std::vector<std::string>{"Mix", "Field to grayscale", "Red", "Green", "Blue"});
        configInput(A_INPUT, "Image A");
        configInput(B_INPUT, "Image B");
        configInput(FIELD_INPUT, "Field modulation");
        configOutput(IMAGE_OUTPUT, "Image");
        configOutput(FIELD_OUTPUT, "Extracted field");
    }
};

struct CvBridgeModule : Module {
    enum ParamIds { MODE_PARAM, SCALE_PARAM, OFFSET_PARAM, NUM_PARAMS };
    enum InputIds { CV_INPUT, AUDIO_INPUT, TRIGGER_INPUT, NUM_INPUTS };
    enum OutputIds { FIELD_OUTPUT, NUM_OUTPUTS };
    dsp::SchmittTrigger triggerDetector;
    bool audioQueueGap = false;

    CvBridgeModule() : Module(Kind::CvBridge) {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        configSwitch(MODE_PARAM, 0.f, 2.f, 0.f, "Mode",
            std::vector<std::string>{"Latched CV", "Buffered audio to raster", "Trigger field"});
        configParam(SCALE_PARAM, -1.f, 1.f, 0.1f, "Volts to field scale");
        configParam(OFFSET_PARAM, -2.f, 2.f, 0.f, "Field offset");
        configInput(CV_INPUT, "CV");
        configInput(AUDIO_INPUT, "Audio");
        configInput(TRIGGER_INPUT, "Trigger");
        configOutput(FIELD_OUTPUT, "Video field");
    }

    void capture(const ProcessArgs& args) override {
        (void) args;
        node_->cvVoltage.store(inputs[CV_INPUT].getVoltage(), std::memory_order_relaxed);
        if (inputs[AUDIO_INPUT].isConnected()) {
            AudioSample sample;
            sample.seconds = audioSeconds_;
            sample.voltage = inputs[AUDIO_INPUT].getVoltage();
            sample.epoch = audioEpoch_;
            if (node_->audio.push(sample)) {
                audioQueueGap = false;
            }
            else if (!audioQueueGap) {
                // Missing captures break interpolation continuity. Mark one
                // epoch boundary per overflow burst, while the queue counts drops.
                ++audioEpoch_;
                incrementTrigger(node_->resets);
                audioQueueGap = true;
            }
        }
        if (triggerDetector.process(inputs[TRIGGER_INPUT].getVoltage()))
            incrementTrigger(node_->triggers);
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        triggerDetector.reset();
        audioQueueGap = false;
    }

    void onPortChange(const PortChangeEvent& e) override {
        if (e.type == engine::Port::INPUT && e.portId == AUDIO_INPUT) {
            // Rack sample time continues while this input is disconnected.
            // Reconnection starts a fresh capture run, never an interpolated gap.
            ++audioEpoch_;
            incrementTrigger(node_->resets);
            audioQueueGap = false;
        }
        Module::onPortChange(e);
    }
};

struct FrameDelayModule : Module {
    enum ParamIds {
        CLEAR_PARAM = kDelayClearParam,
        FRAMES_PARAM = kDelayFramesParam,
        NUM_PARAMS
    };
    enum InputIds { IMAGE_INPUT, CLEAR_INPUT, NUM_INPUTS };
    enum OutputIds { IMAGE_OUTPUT, NUM_OUTPUTS };
    dsp::SchmittTrigger clearButtonDetector;
    dsp::SchmittTrigger clearGateDetector;

    FrameDelayModule() : Module(Kind::Delay) {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, 0);
        configButton(CLEAR_PARAM, "Clear history");
        engine::ParamQuantity* frames = configParam(FRAMES_PARAM,
            static_cast<float>(kMinDelayFrames), static_cast<float>(kMaxDelayFrames),
            static_cast<float>(kDefaultDelayFrames), "Frames", " frames");
        frames->snapEnabled = true;
        frames->displayPrecision = 2;
        configInput(IMAGE_INPUT, "Image");
        configInput(CLEAR_INPUT, "Clear gate");
        configOutput(IMAGE_OUTPUT, "Delayed image");
    }

    void capture(const ProcessArgs& args) override {
        (void) args;
        const bool button = clearButtonDetector.process(params[CLEAR_PARAM].getValue());
        const bool gate = clearGateDetector.process(inputs[CLEAR_INPUT].getVoltage());
        if (button || gate)
            incrementTrigger(node_->resets);
    }

    void fromJson(json_t* rootJ) override {
        // Rack can restore into an existing module. Seed the appended parameter
        // so a legacy patch that omits it cannot inherit that object's old value.
        params[FRAMES_PARAM].setValue(static_cast<float>(kDefaultDelayFrames));
        Module::fromJson(rootJ);
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        clearButtonDetector.reset();
        clearGateDetector.reset();
    }
};

struct VideoMonitorModule : Module {
    enum InputIds { IMAGE_INPUT, NUM_INPUTS };
    VideoMonitorModule() : Module(Kind::Monitor) {
        config(0, NUM_INPUTS, 0, 0);
        configInput(IMAGE_INPUT, "Image");
    }
};

struct VideoIoModule : Module {
    enum InputIds { PUBLISH_INPUT, NUM_INPUTS };
    enum OutputIds { RECEIVE_OUTPUT, NUM_OUTPUTS };
    std::string defaultPublisherName;
    int64_t publisherOwnerModuleId = -1;
    bool restoredDefaultPublisher = false;
    bool publisherRestorePending = false;

    VideoIoModule() : Module(Kind::VideoIo) {
        config(0, NUM_INPUTS, NUM_OUTPUTS, 0);
        configInput(PUBLISH_INPUT, "Image to publish");
        configOutput(RECEIVE_OUTPUT, "Received image");
        defaultPublisherName = node_->ioSettings().publisherName;
    }

    void onAdd(const AddEvent& e) override {
        IoSettings io = node_->ioSettings();
        // Rack duplication strips the new module's top-level ID but preserves
        // our former owner ID. Regenerate only an untouched default name.
        // Reload of the same module ID preserves the saved name exactly.
        if (restoredDefaultPublisher && publisherOwnerModuleId >= 0
            && publisherOwnerModuleId != id) {
            defaultPublisherName = "RVX";
            io.publisherName = defaultPublisherName;
            node_->setIoSettings(io);
        }
        else if (restoredDefaultPublisher) {
            defaultPublisherName = io.publisherName;
        }
        publisherOwnerModuleId = id;
        const bool automatic = io.publisherName == defaultPublisherName;
        std::string requestedDefault = io.publisherName;
        attachNode(e, automatic ? &requestedDefault : NULL);
        if (automatic)
            defaultPublisherName = requestedDefault;
        publisherRestorePending = false;
    }

    void prepareRestoredState() override {
        if (!registered_ || !publisherRestorePending)
            return;
        publisherRestorePending = false;
        IoSettings io = node_->ioSettings();
        if (restoredDefaultPublisher && publisherOwnerModuleId >= 0
            && publisherOwnerModuleId != id) {
            defaultPublisherName = "RVX";
            io.publisherName = defaultPublisherName;
            node_->setIoSettings(io);
        }
        else if (restoredDefaultPublisher) {
            defaultPublisherName = io.publisherName;
        }
        publisherOwnerModuleId = id;
        const bool automatic = io.publisherName == defaultPublisherName;
        std::string requestedDefault = io.publisherName;
        ServiceRegistry::instance().updatePublisherName(
            node_, automatic ? &requestedDefault : NULL);
        if (automatic)
            defaultPublisherName = requestedDefault;
    }

    void appendData(json_t* rootJ) const override {
        const IoSettings io = node_->ioSettings();
        json_object_set_new(rootJ, "sourceId", json_string(io.sourceId.c_str()));
        json_object_set_new(rootJ, "sourceApplication", json_string(io.sourceApplication.c_str()));
        json_object_set_new(rootJ, "sourceName", json_string(io.sourceName.c_str()));
        json_object_set_new(rootJ, "publisherName", json_string(io.publisherName.c_str()));
        json_object_set_new(rootJ, "defaultPublisherName", json_string(defaultPublisherName.c_str()));
        json_object_set_new(rootJ, "publisherOwnerModuleId", json_integer(id));
        json_object_set_new(rootJ, "publish", json_boolean(io.publish));
        json_object_set_new(rootJ, "holdLast", json_boolean(io.holdLast));
    }

    void readData(json_t* rootJ, int schema) override {
        (void) schema;
        // Native preset/history JSON may omit I/O data or contain only source
        // settings. Neither case changes the publisher's automatic/custom identity.
        publisherRestorePending = json_is_string(json_object_get(rootJ, "publisherName"))
            || json_is_string(json_object_get(rootJ, "defaultPublisherName"))
            || json_is_integer(json_object_get(rootJ, "publisherOwnerModuleId"));
        IoSettings io = node_->ioSettings();
        json_t* value = json_object_get(rootJ, "sourceId");
        if (value && json_is_string(value)) io.sourceId = json_string_value(value);
        value = json_object_get(rootJ, "sourceApplication");
        if (value && json_is_string(value)) io.sourceApplication = json_string_value(value);
        value = json_object_get(rootJ, "sourceName");
        if (value && json_is_string(value)) io.sourceName = json_string_value(value);
        value = json_object_get(rootJ, "publisherName");
        if (value && json_is_string(value)) io.publisherName = json_string_value(value);
        value = json_object_get(rootJ, "defaultPublisherName");
        if (value && json_is_string(value)) defaultPublisherName = json_string_value(value);
        value = json_object_get(rootJ, "publisherOwnerModuleId");
        if (value && json_is_integer(value)) publisherOwnerModuleId = json_integer_value(value);
        value = json_object_get(rootJ, "publish");
        if (value) io.publish = json_boolean_value(value);
        value = json_object_get(rootJ, "holdLast");
        if (value) io.holdLast = json_boolean_value(value);
        if (io.publisherName.empty())
            io.publisherName = "RVX";
        restoredDefaultPublisher = io.publisherName == defaultPublisherName;
        node_->setIoSettings(io);
    }
};

struct Preview : widget::OpaqueWidget {
    std::weak_ptr<Node> node;
    std::shared_ptr<const Frame> lastFrame;
    NVGcontext* imageContext = NULL;
    int imageHandle = -1;
    int imageWidth = 0;
    int imageHeight = 0;
    std::vector<unsigned char> rgbaScratch;

    ~Preview() override {
        if (imageHandle > 0 && APP && APP->window && APP->window->vg == imageContext)
            nvgDeleteImage(imageContext, imageHandle);
    }

    void updateImage(NVGcontext* vg, const FramePtr& frame) {
        if (!frame || frame->width <= 0 || frame->height <= 0 || frame->channels < 3)
            return;
        const size_t pixelCount = static_cast<size_t>(frame->width) * frame->height;
        if (pixelCount > frame->pixels.size() / static_cast<size_t>(frame->channels))
            return;
        if (imageContext != vg) {
            // The old NanoVG context owns and reclaims its image. It might
            // already be destroyed, so never call into it after a context swap.
            imageContext = vg;
            imageHandle = -1;
            imageWidth = imageHeight = 0;
            lastFrame.reset();
        }
        if (frame == lastFrame)
            return;

        rgbaScratch.resize(pixelCount * 4);
        for (int y = 0; y < frame->height; ++y) {
            for (int x = 0; x < frame->width; ++x) {
                const size_t src = (static_cast<size_t>(y) * frame->width + x) * frame->channels;
                const size_t dst = (static_cast<size_t>(y) * frame->width + x) * 4;
                for (int c = 0; c < 3; ++c) {
                    const float value = std::max(0.f, std::min(1.f, frame->pixels[src + c]));
                    rgbaScratch[dst + c] = static_cast<unsigned char>(std::lround(value * 255.f));
                }
                float alpha = frame->channels >= 4 ? frame->pixels[src + 3] : 1.f;
                alpha = std::max(0.f, std::min(1.f, alpha));
                rgbaScratch[dst + 3] = static_cast<unsigned char>(std::lround(alpha * 255.f));
            }
        }
        if (imageHandle <= 0 || imageWidth != frame->width || imageHeight != frame->height) {
            if (imageHandle > 0)
                nvgDeleteImage(vg, imageHandle);
            imageHandle = nvgCreateImageRGBA(vg, frame->width, frame->height,
                NVG_IMAGE_NEAREST, rgbaScratch.data());
            imageWidth = frame->width;
            imageHeight = frame->height;
        }
        else {
            nvgUpdateImage(vg, imageHandle, rgbaScratch.data());
        }
        if (imageHandle > 0)
            lastFrame = frame;
    }

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.f);
        nvgFillColor(args.vg, nvgRGB(2, 4, 8));
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, 1.f);
        nvgStrokeColor(args.vg, nvgRGBA(77, 201, 255, 130));
        nvgStroke(args.vg);

        std::shared_ptr<Node> sharedNode = node.lock();
        std::shared_ptr<const NodeDisplay> display = sharedNode ? sharedNode->display() : std::shared_ptr<const NodeDisplay>();
        FramePtr frame = display ? display->preview : FramePtr();
        if (!frame && display)
            frame = display->outputs[0];
        updateImage(args.vg, frame);
        if (imageHandle > 0 && imageWidth > 0 && imageHeight > 0) {
            const float imageAspect = static_cast<float>(imageWidth) / imageHeight;
            const float boxAspect = box.size.x / box.size.y;
            math::Rect target(math::Vec(0, 0), box.size);
            if (imageAspect > boxAspect) {
                target.size.y = box.size.x / imageAspect;
                target.pos.y = (box.size.y - target.size.y) / 2.f;
            }
            else {
                target.size.x = box.size.y * imageAspect;
                target.pos.x = (box.size.x - target.size.x) / 2.f;
            }
            NVGpaint paint = nvgImagePattern(args.vg, target.pos.x, target.pos.y,
                target.size.x, target.size.y, 0.f, imageHandle, 1.f);
            nvgBeginPath(args.vg);
            nvgRect(args.vg, target.pos.x, target.pos.y, target.size.x, target.size.y);
            nvgFillPaint(args.vg, paint);
            nvgFill(args.vg);
        }

        if (APP && APP->window && APP->window->uiFont)
            nvgFontFaceId(args.vg, APP->window->uiFont->handle);
        nvgFontSize(args.vg, 8.f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
        nvgFillColor(args.vg, kInk);
        std::string status = display ? display->status : "Waiting for renderer";
        if (status.empty() && frame)
            status = std::to_string(frame->width) + " x " + std::to_string(frame->height)
                + "  frame " + std::to_string(frame->sequence);
        nvgText(args.vg, 5.f, box.size.y - 4.f, status.c_str(), NULL);
        widget::OpaqueWidget::draw(args);
    }
};

struct SourceChoice : app::LedDisplayChoice {
    std::weak_ptr<Node> node;

    void step() override {
        std::shared_ptr<Node> n = node.lock();
        if (n) {
            const IoSettings io = n->ioSettings();
            text = io.sourceName.empty() ? "From App: None" : "From App: " + io.sourceApplication + " / " + io.sourceName;
        }
        app::LedDisplayChoice::step();
    }

    void onAction(const ActionEvent& e) override {
        (void) e;
        std::shared_ptr<Node> n = node.lock();
        if (!n)
            return;
        ui::Menu* menu = createMenu();
        menu->addChild(createMenuLabel("Syphon source"));
        menu->addChild(createCheckMenuItem("None", "",
            [n]() { return n->ioSettings().sourceId.empty(); },
            [n]() {
                IoSettings io = n->ioSettings();
                io.sourceId.clear(); io.sourceApplication.clear(); io.sourceName.clear();
                n->setIoSettings(io);
            }));
        std::shared_ptr<const NodeDisplay> display = n->display();
        if (!display || display->sources.empty()) {
            menu->addChild(createMenuLabel("No Syphon servers found"));
            return;
        }
        for (size_t i = 0; i < display->sources.size(); ++i) {
            const VideoSource source = display->sources[i];
            const std::string label = source.application + " / " + source.name;
            menu->addChild(createCheckMenuItem(label, "",
                [n, source]() { return n->ioSettings().sourceId == source.id; },
                [n, source]() {
                    IoSettings io = n->ioSettings();
                    io.sourceId = source.id;
                    io.sourceApplication = source.application;
                    io.sourceName = source.name;
                    n->setIoSettings(io);
                }));
        }
    }
};

struct PublisherField : app::LedDisplayTextField {
    std::weak_ptr<Node> node;

    void step() override {
        std::shared_ptr<Node> n = node.lock();
        if (n) {
            IoSettings io = n->ioSettings();
            if (APP->event->selectedWidget == this) {
                if (io.publisherName != text) {
                    io.publisherName = text;
                    n->setIoSettings(io);
                }
            }
            else if (text != io.publisherName) {
                setText(io.publisherName);
            }
        }
        app::LedDisplayTextField::step();
    }
};

struct IoToggle : app::LedDisplayChoice {
    std::weak_ptr<Node> node;
    bool publish = true;

    void step() override {
        std::shared_ptr<Node> n = node.lock();
        if (n) {
            const IoSettings io = n->ioSettings();
            text = publish ? (io.publish ? "Publishing: On" : "Publishing: Off")
                           : (io.holdLast ? "Missing: Hold last" : "Missing: Black");
        }
        app::LedDisplayChoice::step();
    }

    void onAction(const ActionEvent& e) override {
        (void) e;
        std::shared_ptr<Node> n = node.lock();
        if (!n)
            return;
        IoSettings io = n->ioSettings();
        if (publish) io.publish = !io.publish;
        else io.holdLast = !io.holdLast;
        n->setIoSettings(io);
    }
};

struct StatusText : widget::Widget {
    std::weak_ptr<Node> node;
    void draw(const DrawArgs& args) override {
        std::shared_ptr<Node> n = node.lock();
        std::shared_ptr<const NodeDisplay> display = n ? n->display() : std::shared_ptr<const NodeDisplay>();
        std::string status = display ? display->status : "Starting video worker";
        if (n && ServiceRegistry::instance().invalidNativeOutputCount(n->key) > 0)
            status = "Invalid cable: video output is 0 V";
        else if (n && n->kind == Kind::VideoIo
            && ServiceRegistry::instance().publisherNameConflict(n->key))
            status = "Publisher name already in use";
        if (status.empty()) status = "Ready";
        std::string folded = status;
        std::transform(folded.begin(), folded.end(), folded.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool problem = folded.find("invalid") != std::string::npos
            || folded.find("error") != std::string::npos
            || folded.find("overflow") != std::string::npos
            || folded.find("late") != std::string::npos;
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
        nvgFillColor(args.vg, problem ? nvgRGBA(90, 19, 61, 210) : nvgRGBA(8, 11, 18, 170));
        nvgFill(args.vg);
        if (APP && APP->window && APP->window->uiFont)
            nvgFontFaceId(args.vg, APP->window->uiFont->handle);
        nvgFontSize(args.vg, 8.f);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFillColor(args.vg, problem ? kInk : kMuted);
        nvgTextBox(args.vg, 4.f, 3.f, box.size.x - 8.f, status.c_str(), NULL);
        widget::Widget::draw(args);
    }
};

struct FrameCountDisplay : app::LedDisplayChoice {
    engine::ParamQuantity* quantity = NULL;

    void step() override {
        text = std::to_string(quantity
            ? normalizedDelayFrames(quantity->getValue())
            : kDefaultDelayFrames);
        app::LedDisplayChoice::step();
    }

    void onButton(const ButtonEvent& e) override {
        (void) e;
    }
};

struct TestImageWidget : ModuleWidget {
    TestImageWidget(TestImageModule* module) : ModuleWidget(module) {
        PrototypePanel* panel = new PrototypePanel(12, "TEST IMAGE", kCyan);
        panel->label(16.f, 19.f, "PATTERN", 8.f, kMuted);
        panel->label(45.f, 19.f, "PHASE SPEED", 8.f, kMuted);
        panel->label(17.f, 91.f, "IMAGE", 8.f, kCyan);
        panel->label(44.f, 91.f, "FIELD", 8.f, kPink);
        setPanel(panel);
        addScrews(this, box.size.x);
        addParam(createParamCentered<RoundBlackSnapKnob>(pos(16.f, 33.f), module, TestImageModule::PATTERN_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(pos(45.f, 33.f), module, TestImageModule::PHASE_SPEED_PARAM));
        StatusText* status = new StatusText;
        status->box.pos = pos(6.f, 49.f);
        status->box.size = pos(49.f, 18.f);
        if (module) status->node = module->node();
        addChild(status);
        addVideoOutput(pos(17.f, 101.f), TestImageModule::IMAGE_OUTPUT, PortType::Image);
        addVideoOutput(pos(44.f, 101.f), TestImageModule::FIELD_OUTPUT, PortType::Field);
    }
};

struct SignalProcessorWidget : ModuleWidget {
    SignalProcessorWidget(SignalProcessorModule* module) : ModuleWidget(module) {
        PrototypePanel* panel = new PrototypePanel(16, "SIGNAL PROCESSOR", kPink);
        panel->label(11.f, 18.f, "GAIN A", 7.f, kMuted);
        panel->label(31.f, 18.f, "GAIN B", 7.f, kMuted);
        panel->label(51.f, 18.f, "OFFSET", 7.f, kMuted);
        panel->label(71.f, 18.f, "MODE", 7.f, kMuted);
        panel->label(11.f, 83.f, "A", 8.f, kCyan);
        panel->label(31.f, 83.f, "B", 8.f, kCyan);
        panel->label(51.f, 83.f, "FIELD", 8.f, kPink);
        panel->label(31.f, 111.f, "IMAGE", 8.f, kCyan);
        panel->label(57.f, 111.f, "FIELD", 8.f, kPink);
        setPanel(panel);
        addScrews(this, box.size.x);
        addParam(createParamCentered<RoundBlackKnob>(pos(11.f, 31.f), module, SignalProcessorModule::GAIN_A_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(pos(31.f, 31.f), module, SignalProcessorModule::GAIN_B_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(pos(51.f, 31.f), module, SignalProcessorModule::OFFSET_PARAM));
        addParam(createParamCentered<RoundBlackSnapKnob>(pos(71.f, 31.f), module, SignalProcessorModule::MODE_PARAM));
        StatusText* status = new StatusText;
        status->box.pos = pos(6.f, 49.f);
        status->box.size = pos(69.f, 18.f);
        if (module) status->node = module->node();
        addChild(status);
        addVideoInput(pos(11.f, 94.f), SignalProcessorModule::A_INPUT, PortType::Image);
        addVideoInput(pos(31.f, 94.f), SignalProcessorModule::B_INPUT, PortType::Image);
        addVideoInput(pos(51.f, 94.f), SignalProcessorModule::FIELD_INPUT, PortType::Field);
        addVideoOutput(pos(31.f, 121.f), SignalProcessorModule::IMAGE_OUTPUT, PortType::Image);
        addVideoOutput(pos(57.f, 121.f), SignalProcessorModule::FIELD_OUTPUT, PortType::Field);
    }
};

struct CvBridgeWidget : ModuleWidget {
    CvBridgeWidget(CvBridgeModule* module) : ModuleWidget(module) {
        PrototypePanel* panel = new PrototypePanel(12, "CV BRIDGE", kPink);
        panel->label(11.f, 18.f, "MODE", 7.f, kMuted);
        panel->label(31.f, 18.f, "SCALE", 7.f, kMuted);
        panel->label(51.f, 18.f, "OFFSET", 7.f, kMuted);
        panel->label(10.f, 79.f, "CV", 8.f, kInk);
        panel->label(30.f, 79.f, "AUDIO", 8.f, kInk);
        panel->label(50.f, 79.f, "TRIG", 8.f, kInk);
        panel->label(30.f, 108.f, "VIDEO FIELD", 8.f, kPink);
        setPanel(panel);
        addScrews(this, box.size.x);
        addParam(createParamCentered<RoundBlackSnapKnob>(pos(11.f, 31.f), module, CvBridgeModule::MODE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(pos(31.f, 31.f), module, CvBridgeModule::SCALE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(pos(51.f, 31.f), module, CvBridgeModule::OFFSET_PARAM));
        StatusText* status = new StatusText;
        status->box.pos = pos(6.f, 49.f);
        status->box.size = pos(49.f, 18.f);
        if (module) status->node = module->node();
        addChild(status);
        addInput(createInputCentered<PJ301MPort>(pos(10.f, 90.f), module, CvBridgeModule::CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(pos(30.f, 90.f), module, CvBridgeModule::AUDIO_INPUT));
        addInput(createInputCentered<PJ301MPort>(pos(50.f, 90.f), module, CvBridgeModule::TRIGGER_INPUT));
        addVideoOutput(pos(30.f, 119.f), CvBridgeModule::FIELD_OUTPUT, PortType::Field);
    }
};

struct FrameDelayWidget : ModuleWidget {
    FrameDelayWidget(FrameDelayModule* module) : ModuleWidget(module) {
        PrototypePanel* panel = new PrototypePanel(10, "FRAME DELAY", kCyan);
        panel->label(25.f, 20.f, "FRAMES", 8.f, kMuted);
        panel->label(25.f, 56.f, "CLEAR HISTORY", 8.f, kMuted);
        panel->label(14.f, 80.f, "IMAGE", 8.f, kCyan);
        panel->label(37.f, 80.f, "CLEAR", 8.f, kInk);
        panel->label(25.f, 109.f, "DELAYED", 8.f, kCyan);
        setPanel(panel);
        addScrews(this, box.size.x);
        addParam(createParamCentered<RoundBlackSnapKnob>(pos(25.f, 35.f), module,
            FrameDelayModule::FRAMES_PARAM));
        FrameCountDisplay* frameCount = new FrameCountDisplay;
        frameCount->box.pos = pos(18.f, 46.f);
        frameCount->box.size = pos(14.f, 8.f);
        frameCount->quantity = module
            ? module->getParamQuantity(FrameDelayModule::FRAMES_PARAM) : NULL;
        frameCount->text = std::to_string(kDefaultDelayFrames);
        addChild(frameCount);
        addParam(createParamCentered<LEDButton>(pos(25.f, 64.f), module,
            FrameDelayModule::CLEAR_PARAM));
        StatusText* status = new StatusText;
        status->box.pos = pos(5.f, 69.f);
        status->box.size = pos(40.f, 9.f);
        if (module) status->node = module->node();
        addChild(status);
        addVideoInput(pos(14.f, 89.f), FrameDelayModule::IMAGE_INPUT, PortType::Image);
        addInput(createInputCentered<PJ301MPort>(pos(37.f, 89.f), module, FrameDelayModule::CLEAR_INPUT));
        addVideoOutput(pos(25.f, 119.f), FrameDelayModule::IMAGE_OUTPUT, PortType::Image);
    }
};

struct VideoMonitorWidget : ModuleWidget {
    VideoMonitorWidget(VideoMonitorModule* module) : ModuleWidget(module) {
        PrototypePanel* panel = new PrototypePanel(16, "VIDEO MONITOR", kCyan);
        panel->label(40.5f, 105.f, "IMAGE IN", 8.f, kCyan);
        setPanel(panel);
        addScrews(this, box.size.x);
        Preview* preview = new Preview;
        preview->box.pos = pos(5.f, 15.f);
        preview->box.size = pos(71.f, 82.f);
        if (module) preview->node = module->node();
        addChild(preview);
        addVideoInput(pos(40.5f, 116.f), VideoMonitorModule::IMAGE_INPUT, PortType::Image);
    }
};

struct VideoIoWidget : ModuleWidget {
    VideoIoWidget(VideoIoModule* module) : ModuleWidget(module) {
        PrototypePanel* panel = new PrototypePanel(18, "VIDEO I/O", kCyan);
        panel->label(7.f, 18.f, "SYPHON", 8.f, kMuted, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        panel->label(13.f, 108.f, "TO APP", 8.f, kCyan);
        panel->label(78.f, 108.f, "FROM APP", 8.f, kCyan);
        setPanel(panel);
        addScrews(this, box.size.x);

        const std::shared_ptr<Node> node = module ? module->node() : std::shared_ptr<Node>();
        SourceChoice* source = new SourceChoice;
        source->box.pos = pos(6.f, 22.f);
        source->box.size = pos(79.f, 9.f);
        source->node = node;
        addChild(source);

        PublisherField* publisher = new PublisherField;
        publisher->box.pos = pos(6.f, 35.f);
        publisher->box.size = pos(79.f, 9.f);
        publisher->placeholder = "Publisher name";
        publisher->node = node;
        addChild(publisher);

        IoToggle* publish = new IoToggle;
        publish->box.pos = pos(6.f, 48.f);
        publish->box.size = pos(38.f, 9.f);
        publish->node = node;
        publish->publish = true;
        addChild(publish);

        IoToggle* missing = new IoToggle;
        missing->box.pos = pos(47.f, 48.f);
        missing->box.size = pos(38.f, 9.f);
        missing->node = node;
        missing->publish = false;
        addChild(missing);

        StatusText* status = new StatusText;
        status->box.pos = pos(6.f, 62.f);
        status->box.size = pos(79.f, 24.f);
        status->node = node;
        addChild(status);

        addVideoInput(pos(13.f, 119.f), VideoIoModule::PUBLISH_INPUT, PortType::Image);
        addVideoOutput(pos(78.f, 119.f), VideoIoModule::RECEIVE_OUTPUT, PortType::Image);
    }
};

} // namespace

} // namespace rackadapter
} // namespace rvx

Model* modelRvxTestImage = createModel<rvx::rackadapter::TestImageModule, rvx::rackadapter::TestImageWidget>("TestImage");
Model* modelRvxSignalProcessor = createModel<rvx::rackadapter::SignalProcessorModule, rvx::rackadapter::SignalProcessorWidget>("SignalProcessor");
Model* modelRvxCvBridge = createModel<rvx::rackadapter::CvBridgeModule, rvx::rackadapter::CvBridgeWidget>("CvBridge");
Model* modelRvxFrameDelay = createModel<rvx::rackadapter::FrameDelayModule, rvx::rackadapter::FrameDelayWidget>("FrameDelay");
Model* modelRvxVideoMonitor = createModel<rvx::rackadapter::VideoMonitorModule, rvx::rackadapter::VideoMonitorWidget>("VideoMonitor");
Model* modelRvxVideoIo = createModel<rvx::rackadapter::VideoIoModule, rvx::rackadapter::VideoIoWidget>("VideoIo");
