#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"

#define OLC_PGEX_MINIAUDIO
#include "olcPGEX_MiniAudio.h"

#define OLC_PGEX_TTF
#include "olcPGEX_TTF.h"

#include "GameNascent.h"

using namespace nascent;

class NascentMain : public olc::PixelGameEngine
{
	GameNascent game;
    
public:
NascentMain() : game(this)
	{
		sAppName = "Nascent";
	}

public:
	bool OnUserCreate() override
	{
		
		game.init();

		return true;
	}

	bool OnUserUpdate(float elapsed_time) override
	{
		game.update(elapsed_time);
		game.draw(this);
		return true;
	}
};

int main()
{
	NascentMain ns_main;
	
    if (ns_main.Construct(1920, 1080, 1, 1, true, false, false, true)) {
        ns_main.Start();
    }

	return 0;
}
