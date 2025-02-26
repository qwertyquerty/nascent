#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"

#define OLC_PGEX_MINIAUDIO
#include "olcPGEX_MiniAudio.h"

#include "Chart.h"

using namespace nascent;

class NascentGame : public olc::PixelGameEngine
{
    olc::MiniAudio ma;
    int song;
    
public:
NascentGame()
	{
		sAppName = "Nascent";
	}

public:
	bool OnUserCreate() override
	{
        Chart chart("assets/songs/Jack - v1/Various Artists - Dan ~ REFORM ~ JackMap Pack (DDMythical) [Last Chance ~ 7th ~ (Marathon)].osu.json");

        song = ma.LoadSound(chart.audio_path.string());
        ma.SetPitch(song, 0.9);
        ma.Play(song);

		return true;
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
        std::string fps_string = std::format("POS: {}", ma.GetCursorMilliseconds(song));
        DrawStringPropDecal({ 2, 2 }, fps_string, olc::YELLOW, {4, 4});
		return true;
	}
};

int main()
{
	NascentGame ns_game;
	
    if (ns_game.Construct(1920, 1080, 1, 1, true, false, false, false)) {
        ns_game.Start();
    }

	return 0;
}
