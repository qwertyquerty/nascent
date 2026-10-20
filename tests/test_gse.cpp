#include "TestSupport.h"

#include "gse/Overlay.h"
#include "gse/SceneStack.h"
#include "gse/anim/Easing.h"
#include "gse/concurrency/Async.h"
#include "gse/input/Input.h"
#include "gse/input/KeyEvents.h"
#include "gse/render/Color.h"
#include "gse/render/MipDecal.h"
#include "gse/render/ShapeSprite.h"
#include "gse/ui/KeyRepeat.h"
#include "gse/ui/Layout.h"
#include "gse/util/Math.h"

#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace nascent;
using namespace nascent::test;

namespace {
    class RecordingScene : public Scene {
        public:
        std::string name;
        std::vector<std::string>& log;

        RecordingScene(std::string name, std::vector<std::string>& log) : name(std::move(name)), log(log) {}

        void on_create() override {
            log.push_back(name + " create");
        }

        void on_enter() override {
            log.push_back(name + " enter");
        }

        void on_exit() override {
            log.push_back(name + " exit");
        }

        void on_destroy() override {
            log.push_back(name + " destroy");
        }

        void update(float) override {}
        void draw(olc::PixelGameEngine*) override {}
    };

    class CountingOverlay : public Overlay {
        public:
        bool modal;
        int updates = 0;

        explicit CountingOverlay(bool modal) : modal(modal) {}

        void update(float) override {
            updates++;
        }

        void draw(olc::PixelGameEngine*) override {}

        bool is_modal() const override {
            return modal;
        }
    };

    struct FakeKeys {
        std::map<olc::Key, olc::HWButton> keys;

        Input::KeyState state() {
            return [this](olc::Key key) {
                auto found = keys.find(key);
                return found == keys.end() ? olc::HWButton{} : found->second;
            };
        }

        void press(olc::Key key) {
            olc::HWButton button;
            button.bPressed = true;
            button.bHeld = true;
            keys[key] = button;
        }

        void hold(olc::Key key) {
            olc::HWButton button;
            button.bHeld = true;
            keys[key] = button;
        }
    };
}

NASCENT_TEST(gse_key_event_queue) {
    KeyEventQueue queue;
    CHECK(queue.push(olc::Key::A, true, 1.0));
    CHECK(!queue.push(olc::Key::A, true, 2.0));
    CHECK(queue.push(olc::Key::A, false, 3.0));
    CHECK(!queue.push(olc::Key::NONE, true, 4.0));
    std::vector<KeyEvent> events = queue.drain();
    CHECK(events.size() == 2);
    CHECK(events[0].pressed && events[0].time == 1.0);
    CHECK(!events[1].pressed && events[1].time == 3.0);
    CHECK(queue.drain().empty());
}

NASCENT_TEST(gse_input_actions) {
    FakeKeys keys;
    Input input(keys.state());
    input.bind(actions::CONFIRM, {olc::Key::ENTER, olc::Key::SPACE});
    input.bind(actions::BACK, {olc::Key::ESCAPE});

    keys.press(olc::Key::SPACE);
    CHECK(input.pressed(actions::CONFIRM));
    CHECK(!input.pressed(actions::BACK));
    CHECK(input.take(actions::CONFIRM));
    CHECK(!input.pressed(actions::CONFIRM));
    CHECK(!input.take(actions::CONFIRM));
    CHECK(input.key_pressed(olc::Key::SPACE));

    input.begin_frame();
    CHECK(input.pressed(actions::CONFIRM));

    input.suppress();
    CHECK(!input.pressed(actions::CONFIRM));
    CHECK(!input.key_pressed(olc::Key::SPACE));
    CHECK(input.text().empty());
    CHECK(input.pressed_keys().empty());
    input.restore();
    CHECK(input.pressed(actions::CONFIRM));

    {
        SuppressedInput muted(input, true);
        CHECK(input.is_suppressed());
    }

    CHECK(!input.is_suppressed());
    keys.hold(olc::Key::ESCAPE);
    CHECK(input.held(actions::BACK));
    CHECK(!input.pressed(actions::BACK));
}

NASCENT_TEST(gse_scene_stack_lifecycle) {
    std::vector<std::string> log;
    SceneStack stack(nullptr);
    RecordingScene base("base", log);

    stack.reset(&base);
    CHECK(stack.apply());
    CHECK(stack.top() == &base);
    CHECK((log == std::vector<std::string>{"base create", "base enter"}));

    log.clear();
    stack.push(std::make_unique<RecordingScene>("play", log));
    CHECK(stack.apply());
    CHECK(stack.depth() == 2);
    CHECK((log == std::vector<std::string>{"base exit", "play create", "play enter"}));

    log.clear();
    stack.replace(std::make_unique<RecordingScene>("results", log));
    stack.apply();
    CHECK(stack.depth() == 2);
    CHECK((log == std::vector<std::string>{"play exit", "play destroy", "results create", "results enter"}));

    log.clear();
    stack.pop();
    stack.apply();
    CHECK(stack.top() == &base);
    CHECK((log == std::vector<std::string>{"results exit", "results destroy", "base enter"}));
}

NASCENT_TEST(gse_scene_stack_transition) {
    std::vector<std::string> log;
    SceneStack stack(nullptr);
    stack.set_transition_duration(0.2f);
    RecordingScene base("base", log);
    stack.reset(&base);
    stack.apply();

    stack.push(std::make_unique<RecordingScene>("menu", log));
    stack.apply();
    stack.pop();
    stack.apply();
    log.clear();

    CHECK(stack.is_transitioning());
    CHECK(stack.outgoing_scene() != nullptr);
    stack.advance(1.0f);
    CHECK(stack.transition_progress() == 0.0f);

    for (int frame = 0; frame < 5; frame++) {
        stack.advance(1.0f);
    }

    CHECK(stack.is_transitioning());
    CHECK(stack.transition_progress() > 0.5f);
    stack.advance(1.0f);
    stack.advance(1.0f);
    CHECK(!stack.is_transitioning());
    CHECK((log == std::vector<std::string>{"menu destroy"}));

    int visited = 0;
    stack.for_each_created([&](Scene*) { visited++; });
    CHECK(visited == 1);
}

NASCENT_TEST(gse_overlay_stack) {
    OverlayStack overlays;
    CountingOverlay& toast = overlays.open(std::make_unique<CountingOverlay>(false));
    CountingOverlay& lower = overlays.open(std::make_unique<CountingOverlay>(true));
    CountingOverlay& upper = overlays.open(std::make_unique<CountingOverlay>(true));

    CHECK(overlays.has_modal());
    overlays.update(0.1f);
    CHECK(toast.updates == 1 && upper.updates == 1 && lower.updates == 0);
    CHECK(overlays.find<CountingOverlay>() == &toast);

    upper.close();
    overlays.update(0.1f);
    CHECK(lower.updates == 1);

    lower.close();
    overlays.update(0.1f);
    CHECK(!overlays.has_modal());
    CHECK(!overlays.empty());
    toast.close();
    CHECK(overlays.empty());
}

NASCENT_TEST(gse_helpers) {
    CHECK(progress(0.5f, 0.0f, 1.0f) == 0.5f);
    CHECK(progress(2.0f, 0.0f, 1.0f) == 1.0f);
    CHECK(progress(-1.0f, 0.0f, 1.0f) == 0.0f);
    CHECK(ease_out(0.0f) == 0.0f && ease_out(1.0f) == 1.0f && ease_out(0.5f) > 0.5f);
    CHECK(ease_in(0.5f) < 0.5f);
    CHECK(std::abs(smoothstep(0.0, 10.0, 0.5) - 5.0f) < 0.001f);
    CHECK(positive_modulo(-1, 5) == 4 && positive_modulo(7, 5) == 2);

    olc::Pixel faded = fade(olc::Pixel(10, 20, 30, 200), 0.5);
    CHECK(faded.r == 10 && faded.a == 100);
    olc::Pixel mixed = mix(olc::Pixel(0, 0, 0, 0), olc::Pixel(200, 100, 50, 255), 0.5);
    CHECK(mixed.r == 100 && mixed.g == 50 && mixed.b == 25);

    std::atomic<int> sum = 0;
    parallel_for(100, [&](size_t index) { sum += (int)index; });
    CHECK(sum == 4950);
    std::future<int> done = run_detached([] { return 7; });
    done.wait();
    CHECK(is_ready(done) && done.get() == 7);

    KeyRepeat repeat;
    CHECK(repeat.update(true, true, 0.0f));
    CHECK(!repeat.update(false, true, KeyRepeat::DELAY / 2));
    CHECK(repeat.update(false, true, KeyRepeat::DELAY));
    CHECK(!repeat.update(false, false, 1.0f));

    Rect rect = {{10.0f, 10.0f}, {20.0f, 10.0f}};
    CHECK(rect.contains({15.0f, 15.0f}) && !rect.contains({35.0f, 15.0f}));
    CHECK(rect.inflated(5.0f).contains({33.0f, 15.0f}));
    CHECK(rect.center().x == 20.0f && rect.center().y == 15.0f);
}

NASCENT_TEST(gse_polygon_rasterizing) {
    std::unique_ptr<olc::Sprite> square = rasterize_polygon({{0.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 8.0f}, {0.0f, 8.0f}}, {8, 8});
    CHECK(square->GetPixel(4, 4).a == 255);
    CHECK(square->GetPixel(0, 0).a == 255);

    std::unique_ptr<olc::Sprite> star = rasterize_polygon(star_points({16.0f, 16.0f}, 16.0f, 7.0f), {32, 32});
    CHECK(star->GetPixel(16, 16).a == 255);
    CHECK(star->GetPixel(1, 30).a == 0);
    CHECK(star->GetPixel(16, 1).a > 0);
}

NASCENT_TEST(gse_mip_downsampling) {
    olc::Sprite source(4, 2);
    source.SetPixel(0, 0, olc::Pixel(255, 0, 0, 255));
    source.SetPixel(1, 0, olc::Pixel(255, 0, 0, 255));
    source.SetPixel(0, 1, olc::Pixel(0, 0, 255, 0));
    source.SetPixel(1, 1, olc::Pixel(0, 0, 255, 0));
    source.SetPixel(2, 0, olc::Pixel(0, 0, 0, 0));
    source.SetPixel(3, 0, olc::Pixel(0, 0, 0, 0));
    source.SetPixel(2, 1, olc::Pixel(0, 0, 0, 0));
    source.SetPixel(3, 1, olc::Pixel(0, 0, 0, 0));

    std::unique_ptr<olc::Sprite> half = downsample_half(source);
    CHECK(half->width == 2 && half->height == 1);
    olc::Pixel blended = half->GetPixel(0, 0);
    CHECK(blended.r == 255 && blended.b == 0 && blended.a == 128);
    CHECK(half->GetPixel(1, 0).a == 0);

    std::unique_ptr<olc::Sprite> tiny = downsample_half(*half);
    CHECK(tiny->width == 1 && tiny->height == 1);

    olc::Sprite odd(3, 1);
    odd.SetPixel(0, 0, olc::Pixel(0, 0, 0, 0));
    odd.SetPixel(1, 0, olc::Pixel(0, 0, 0, 0));
    odd.SetPixel(2, 0, olc::Pixel(255, 255, 255, 255));
    std::unique_ptr<olc::Sprite> edge = downsample_half(odd);
    CHECK(edge->width == 2 && edge->height == 1);
    CHECK(edge->GetPixel(1, 0).a == 255);
}
