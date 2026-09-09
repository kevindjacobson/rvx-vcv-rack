#pragma once

#include "../plugin.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rvx {
namespace rackadapter {
namespace theme {

inline NVGcolor panel() { return nvgRGB(8, 9, 8); }
inline NVGcolor cell() { return nvgRGB(17, 19, 15); }
inline NVGcolor cellSignal() { return nvgRGB(13, 23, 15); }
inline NVGcolor cellAlert() { return nvgRGB(37, 12, 9); }
inline NVGcolor structure() { return nvgRGB(52, 56, 47); }
inline NVGcolor paper() { return nvgRGB(232, 226, 207); }
inline NVGcolor muted() { return nvgRGB(140, 145, 132); }
inline NVGcolor amber() { return nvgRGB(242, 154, 56); }
inline NVGcolor signal() { return nvgRGB(112, 217, 155); }
inline NVGcolor alert() { return nvgRGB(224, 82, 68); }
inline NVGcolor bypass() { return nvgRGB(168, 132, 199); }

inline std::string fontPath(bool semibold = false) {
    if (!pluginInstance)
        return std::string();
    return asset::plugin(pluginInstance, semibold
        ? "res/fonts/BarlowCondensed-SemiBold.ttf"
        : "res/fonts/BarlowCondensed-Regular.ttf");
}

inline std::string editableFontPath() {
    const std::string candidate = fontPath(false);
    if (APP && APP->window && !candidate.empty()) {
        const std::shared_ptr<window::Font> font = APP->window->loadFont(candidate);
        if (font && font->handle >= 0)
            return candidate;
    }
    return asset::system("res/fonts/DejaVuSans.ttf");
}

inline void useFont(NVGcontext* vg, bool semibold = false) {
    if (!APP || !APP->window)
        return;
    const std::string path = fontPath(semibold);
    if (!path.empty()) {
        std::shared_ptr<window::Font> font = APP->window->loadFont(path);
        if (font && font->handle >= 0) {
            nvgFontFaceId(vg, font->handle);
            return;
        }
    }
    if (APP->window->uiFont)
        nvgFontFaceId(vg, APP->window->uiFont->handle);
}

enum class TextRole { Primary, Secondary, Video, Value, Alert, Bypass };
enum class CellRole { Neutral, Signal, Alert };

inline NVGcolor textColor(TextRole role) {
    switch (role) {
    case TextRole::Secondary: return muted();
    case TextRole::Video: return signal();
    case TextRole::Value: return amber();
    case TextRole::Alert: return alert();
    case TextRole::Bypass: return bypass();
    default: return paper();
    }
}

struct LabelSpec {
    math::Vec position;
    std::string text;
    float size;
    TextRole role;
    int align;
};

struct CellSpec {
    math::Rect rect;
    CellRole role;
};

class Panel : public widget::Widget {
public:
    Panel(float hp, std::string title, std::string subtitle, int section)
        : title_(std::move(title)), subtitle_(std::move(subtitle)), section_(section) {
        box.size = math::Vec(hp * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
    }

    void label(float xMm, float yMm, std::string text, float size = 9.f,
               TextRole role = TextRole::Primary,
               int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE) {
        labels_.push_back({mm2px(math::Vec(xMm, yMm)), std::move(text), size, role, align});
    }

    void cellBox(float xMm, float yMm, float widthMm, float heightMm,
                 CellRole role = CellRole::Neutral) {
        cells_.push_back({math::Rect(mm2px(math::Vec(xMm, yMm)),
                                    mm2px(math::Vec(widthMm, heightMm))), role});
    }

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
        nvgFillColor(args.vg, panel());
        nvgFill(args.vg);

        for (const CellSpec& spec : cells_) {
            nvgBeginPath(args.vg);
            nvgRect(args.vg, spec.rect.pos.x, spec.rect.pos.y,
                    spec.rect.size.x, spec.rect.size.y);
            nvgFillColor(args.vg, spec.role == CellRole::Signal ? cellSignal()
                : spec.role == CellRole::Alert ? cellAlert() : cell());
            nvgFill(args.vg);
            nvgStrokeWidth(args.vg, 1.f);
            nvgStrokeColor(args.vg, spec.role == CellRole::Signal ? signal()
                : spec.role == CellRole::Alert ? alert() : structure());
            nvgStroke(args.vg);
        }

        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0.f, mm2px(1.8f), box.size.x, mm2px(1.f));
        nvgFillColor(args.vg, amber());
        nvgFill(args.vg);

        const float headerLeft = mm2px(3.f);
        const float headerTop = mm2px(4.2f);
        const float headerBottom = mm2px(13.4f);
        const float idWidth = mm2px(9.f);
        const float idRight = box.size.x - mm2px(3.f);
        const float idLeft = idRight - idWidth;
        const float cut = mm2px(2.2f);

        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, headerLeft, headerTop);
        nvgLineTo(args.vg, idLeft - mm2px(1.5f), headerTop);
        nvgLineTo(args.vg, idLeft - mm2px(1.5f) + cut, headerTop + cut);
        nvgLineTo(args.vg, idLeft - mm2px(1.5f) + cut, headerBottom);
        nvgLineTo(args.vg, headerLeft, headerBottom);
        nvgClosePath(args.vg);
        nvgFillColor(args.vg, cell());
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, structure());
        nvgStroke(args.vg);

        nvgBeginPath(args.vg);
        nvgRect(args.vg, idLeft, headerTop, idWidth, headerBottom - headerTop);
        nvgFillColor(args.vg, amber());
        nvgFill(args.vg);

        useFont(args.vg, true);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 13.f);
        nvgFillColor(args.vg, paper());
        nvgText(args.vg, headerLeft + mm2px(2.f), mm2px(7.6f), title_.c_str(), NULL);
        nvgFontSize(args.vg, 6.5f);
        nvgTextLetterSpacing(args.vg, 1.2f);
        nvgFillColor(args.vg, muted());
        nvgText(args.vg, headerLeft + mm2px(2.f), mm2px(11.3f), subtitle_.c_str(), NULL);
        nvgTextLetterSpacing(args.vg, 0.f);

        char sectionText[3];
        std::snprintf(sectionText, sizeof(sectionText), "%02d", section_);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 14.f);
        nvgFillColor(args.vg, panel());
        nvgText(args.vg, idLeft + idWidth / 2.f, (headerTop + headerBottom) / 2.f,
                sectionText, NULL);

        drawRuler(args.vg, mm2px(3.f), box.size.x - mm2px(3.f), mm2px(15.2f));

        useFont(args.vg, true);
        for (const LabelSpec& spec : labels_) {
            nvgFontSize(args.vg, spec.size);
            nvgFillColor(args.vg, textColor(spec.role));
            nvgTextAlign(args.vg, spec.align);
            nvgText(args.vg, spec.position.x, spec.position.y, spec.text.c_str(), NULL);
        }

        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f);
        nvgStrokeWidth(args.vg, 1.f);
        nvgStrokeColor(args.vg, structure());
        nvgStroke(args.vg);
        widget::Widget::draw(args);
    }

private:
    static void drawRuler(NVGcontext* vg, float left, float right, float y) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, left, y);
        nvgLineTo(vg, right, y);
        const int divisions = std::max(4, static_cast<int>((right - left) / mm2px(7.f)));
        for (int i = 0; i <= divisions; ++i) {
            const float x = left + (right - left) * static_cast<float>(i) / divisions;
            const float height = (i % 5 == 0) ? mm2px(2.f) : mm2px(1.1f);
            nvgMoveTo(vg, x, y - height / 2.f);
            nvgLineTo(vg, x, y + height / 2.f);
        }
        nvgStrokeWidth(vg, 0.75f);
        nvgStrokeColor(vg, nvgRGBA(112, 217, 155, 170));
        nvgStroke(vg);
    }

    std::string title_;
    std::string subtitle_;
    int section_;
    std::vector<LabelSpec> labels_;
    std::vector<CellSpec> cells_;
};

class KnobScale : public widget::Widget {
public:
    explicit KnobScale(bool bipolar = true) : bipolar_(bipolar) {
        box.size = mm2px(math::Vec(15.f, 15.f));
    }

    void draw(const DrawArgs& args) override {
        const math::Vec center = box.size.div(2.f);
        const float radius = box.size.x * 0.43f;
        const float start = -0.83f * static_cast<float>(M_PI);
        const float end = 0.83f * static_cast<float>(M_PI);
        nvgBeginPath(args.vg);
        nvgArc(args.vg, center.x, center.y, radius, start, end, NVG_CW);
        nvgStrokeWidth(args.vg, 1.3f);
        nvgStrokeColor(args.vg, amber());
        nvgStroke(args.vg);
        for (int i = 0; i < 9; ++i) {
            const float angle = start + (end - start) * i / 8.f;
            const float inner = radius - ((i == 0 || i == 4 || i == 8) ? 3.5f : 2.f);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, center.x + std::cos(angle) * inner,
                      center.y + std::sin(angle) * inner);
            nvgLineTo(args.vg, center.x + std::cos(angle) * (radius + 2.f),
                      center.y + std::sin(angle) * (radius + 2.f));
            nvgStrokeWidth(args.vg, 0.8f);
            nvgStrokeColor(args.vg, (bipolar_ && i == 4) ? paper() : muted());
            nvgStroke(args.vg);
        }
        widget::Widget::draw(args);
    }

private:
    bool bipolar_;
};

inline void addKnobScale(app::ModuleWidget* widget, math::Vec center, bool bipolar = true) {
    KnobScale* scale = new KnobScale(bipolar);
    scale->box.pos = center.minus(scale->box.size.div(2.f));
    widget->addChild(scale);
}

class RvxKnob : public RoundKnob {
public:
    RvxKnob() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/rvx-knob.svg")));
        bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/rvx-knob-bg.svg")));
    }
};

class RvxSnapKnob : public RvxKnob {
public:
    RvxSnapKnob() { snap = true; }
};

class UtilityPort : public app::SvgPort {
public:
    UtilityPort() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/rvx-utility-port.svg")));
    }
};

inline void styleChoice(app::LedDisplayChoice* choice, TextRole role = TextRole::Value) {
    choice->fontPath = fontPath(false);
    choice->color = textColor(role);
    choice->bgColor = panel();
    choice->textOffset = math::Vec(5.f, 1.f);
}

class ConsoleChoice : public app::LedDisplayChoice {
public:
    explicit ConsoleChoice(TextRole role = TextRole::Value, bool chevron = true,
                           bool centered = false)
        : role_(role), chevron_(chevron), centered_(centered) {
        styleChoice(this, role);
    }

    void setRole(TextRole role) {
        role_ = role;
        color = textColor(role);
    }

    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f);
        nvgFillColor(args.vg, panel());
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, 1.f);
        nvgStrokeColor(args.vg, textColor(role_));
        nvgStroke(args.vg);

        nvgSave(args.vg);
        const float reserve = chevron_ ? 13.f : 5.f;
        nvgScissor(args.vg, 4.f, 1.f, std::max(0.f, box.size.x - 4.f - reserve),
                   std::max(0.f, box.size.y - 2.f));
        useFont(args.vg, centered_ || role_ != TextRole::Secondary);
        nvgFontSize(args.vg, centered_ ? 12.f : 9.f);
        nvgTextAlign(args.vg, (centered_ ? NVG_ALIGN_CENTER : NVG_ALIGN_LEFT)
            | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, textColor(role_));
        nvgText(args.vg, centered_ ? box.size.x / 2.f : 5.f, box.size.y / 2.f,
                text.c_str(), NULL);
        nvgRestore(args.vg);

        if (chevron_) {
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, box.size.x - 9.f, box.size.y / 2.f - 3.f);
            nvgLineTo(args.vg, box.size.x - 5.f, box.size.y / 2.f);
            nvgLineTo(args.vg, box.size.x - 9.f, box.size.y / 2.f + 3.f);
            nvgStrokeWidth(args.vg, 1.2f);
            nvgStrokeColor(args.vg, textColor(role_));
            nvgStroke(args.vg);
        }
        widget::OpaqueWidget::draw(args);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        // LedDisplayChoice paints its stock 12 px label on layer 1. This choice
        // owns all of its text rendering so it can clip long Syphon names.
        widget::OpaqueWidget::drawLayer(args, layer);
    }

private:
    TextRole role_;
    bool chevron_;
    bool centered_;
};

inline void styleTextField(app::LedDisplayTextField* field) {
    // Rack's stock editable field has no font fallback of its own, so resolve
    // the packaged face before giving it a path it will use for text/cursors.
    field->fontPath = editableFontPath();
    field->color = amber();
    field->bgColor = panel();
    field->textOffset = math::Vec(5.f, 1.f);
}

class Guard : public widget::Widget {
public:
    Guard() { box.size = mm2px(math::Vec(12.f, 9.f)); }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f);
        nvgFillColor(args.vg, cellAlert());
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, 1.5f);
        nvgStrokeColor(args.vg, alert());
        nvgStroke(args.vg);
        widget::Widget::draw(args);
    }
};

inline void addGuard(app::ModuleWidget* widget, math::Vec center) {
    Guard* guard = new Guard;
    guard->box.pos = center.minus(guard->box.size.div(2.f));
    widget->addChild(guard);
}

} // namespace theme
} // namespace rackadapter
} // namespace rvx
