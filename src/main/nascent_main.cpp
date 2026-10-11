#include "olcPixelGameEngine.h"

#include "GameNascent.h"
#include "Platform.h"

using namespace nascent;

class NascentMain : public olc::PixelGameEngine {
    GameNascent game;

    public:
    NascentMain() : game(this) {
        sAppName = "Nascent";
    }

    public:
    bool OnUserCreate() override {

        game.init();

        return true;
    }

    bool OnUserUpdate(float elapsed_time) override {
        game.update(elapsed_time);

        if (game.should_quit()) {
            return false;
        }

        game.draw(this);
        return true;
    }
};

int main() {
    use_executable_directory_for_assets();

    NascentMain ns_main;

    if (ns_main.Construct(1920, 1080, 1, 1, true, false, false, false)) {
        ns_main.Start();
    }

    return 0;
}
