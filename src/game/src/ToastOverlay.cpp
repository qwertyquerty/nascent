#include "ToastOverlay.h"

#include "Skin.h"
#include "gse/Game.h"
#include "gse/render/Color.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <utility>

namespace nascent {
    ToastOverlay::ToastOverlay(Skin* skin) : skin(skin) {}

    void ToastOverlay::show(std::string message, bool is_error, float duration) {
        this->message = std::move(message);
        this->is_error = is_error;
        timer = duration;
    }

    void ToastOverlay::update(float elapsed_time) {
        timer = std::max(0.0f, timer - elapsed_time);
        Input& input = game->input();

        if (timer > FADE_TIME && input.clicked(0) && area.contains(input.mouse_position()) && input.take_click(0)) {
            timer = FADE_TIME;
        }
    }

    bool ToastOverlay::is_modal() const {
        return false;
    }

    void ToastOverlay::draw(olc::PixelGameEngine* window) {
        if (timer <= 0 || message.empty()) {
            area = {};
            return;
        }

        olc::Font& font = skin->display_font_small;
        olc::FontRect bounds = text_bounds(font, message);
        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t padding = Ui::px(PADDING);
        int32_t toast_top = Ui::px(TOP);
        int32_t width = bounds.size.x + padding * 2;
        int32_t height = metrics.size.y + padding * 2;
        int32_t x = (window->GetScreenSize().x - width) / 2;
        float opacity = std::min(1.0f, timer / FADE_TIME);

        olc::Pixel border = fade(skin->color(is_error ? SkinColor::NEGATIVE : SkinColor::POSITIVE), opacity);
        float left = (float)x;
        float top = (float)toast_top;
        float edge = std::max(1.0f, Ui::pxf(BORDER));
        area = {{left, top}, {(float)width, (float)height}};

        window->FillRectDecal({left, top}, {(float)width, (float)height}, fade(skin->color(SkinColor::TOAST_BACKGROUND), opacity));
        window->FillRectDecal({left, top}, {(float)width, edge}, border);
        window->FillRectDecal({left, top + height - edge}, {(float)width, edge}, border);
        window->FillRectDecal({left, top + edge}, {edge, height - edge * 2}, border);
        window->FillRectDecal({left + width - edge, top + edge}, {edge, height - edge * 2}, border);
        draw_text(font, message, {x + padding, toast_top + padding - metrics.offset.y}, fade(skin->color(SkinColor::TEXT), opacity));
    }
}
