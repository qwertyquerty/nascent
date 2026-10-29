import io
import zipfile

import pytest

import nascent

OSU = """osu file format v14

[General]
AudioFilename: audio.mp3
Mode: 3

[Metadata]
Title:Song
Artist:Someone
Creator:Mapper
Version:Hard

[Difficulty]
CircleSize:4
OverallDifficulty:8

[TimingPoints]
0,250,4,1,0,100,1,0

[HitObjects]
{objects}
"""

SM = """#TITLE:Song;
#ARTIST:Someone;
#OFFSET:0;
#BPMS:0=180;
#NOTES:
     dance-single:
     Mapper:
     Hard:
     10:
     0,0,0,0,0:
{rows}
;
#NOTES:
     dance-single:
     Mapper:
     Easy:
     3:
     0,0,0,0,0:
1000
0000
0100
0000
;
#NOTES:
     lights-cabinet:
     :
     Easy:
     1:
     0,0,0,0,0:
00000000
;
"""


def osu_text():
    objects = [f"{64 + 128 * (index % 4)},192,{index * 125},1,0,0:0:0:0:" for index in range(256)]
    return OSU.format(objects="\n".join(objects))


def sm_text():
    rows = ["1000", "0100", "0010", "0001"] * 64
    measures = [rows[index : index + 16] for index in range(0, len(rows), 16)]
    return SM.format(rows="\n,\n".join("\n".join(measure) for measure in measures))


def test_osu_file(tmp_path):
    path = tmp_path / "chart.osu"
    path.write_text(osu_text(), encoding="utf-8")
    ratings = nascent.difficulty(path)

    assert len(ratings) == 1
    assert ratings[0].chart.title == "Song"
    assert ratings[0].chart.key_count == 4
    assert ratings[0].rate == 1.0
    assert ratings[0].difficulty > 0
    assert nascent.difficulty(path, 1.5)[0].difficulty > ratings[0].difficulty


def test_stepmania_file(tmp_path):
    path = tmp_path / "song.sm"
    path.write_text(sm_text(), encoding="utf-8")
    charts = nascent.load(path)

    assert [chart.difficulty_name for chart in charts] == ["Hard", "Easy"]
    assert charts[0].note_count == 256
    ratings = nascent.difficulty(path)
    assert ratings[0].difficulty > ratings[1].difficulty


def test_detects_format_from_bytes():
    assert nascent.load(osu_text().encode())[0].difficulty_name == "Hard"
    assert len(nascent.load(sm_text().encode())) == 2
    chart = nascent.load(osu_text().encode())[0]
    assert nascent.load(chart.to_json().encode())[0].hash == chart.hash


def test_archive():
    rows = "\n".join(["1000", "0100", "0010", "0001"] * 16)
    ssc = f"#TITLE:Song;\n#OFFSET:0;\n#BPMS:0=180;\n#NOTEDATA:;\n#STEPSTYPE:dance-single;\n#DIFFICULTY:Challenge;\n#METER:12;\n#NOTES:\n{rows}\n;\n"
    buffer = io.BytesIO()

    with zipfile.ZipFile(buffer, "w") as archive:
        archive.writestr("song/chart.osu", osu_text())
        archive.writestr("pack/song/song.sm", sm_text())
        archive.writestr("pack/song/song.ssc", ssc)
        archive.writestr("song/readme.txt", "hello")

    charts = nascent.load(buffer.getvalue())
    assert sorted(chart.difficulty_name for chart in charts) == ["Challenge", "Hard"]


def test_difficulty_breakdown():
    breakdown = nascent.difficulty_breakdown(osu_text().encode(), rate=1.2)[0]

    assert breakdown.rate == 1.2
    assert breakdown.difficulty == nascent.difficulty(osu_text().encode(), rate=1.2)[0].difficulty
    assert list(breakdown.mix) == nascent.SKILLS and list(breakdown.ratings) == nascent.SKILLS
    assert abs(sum(breakdown.mix.values()) - 1.0) < 1e-9
    assert all(value >= 0 for value in breakdown.mix.values())
    assert all(0 <= value <= breakdown.difficulty + 1e-9 for value in breakdown.ratings.values())
    assert max(breakdown.mix, key=breakdown.mix.get) == "stream"
    assert breakdown.sustained_seconds > 0

    jacks = OSU.format(objects="\n".join(f"64,192,{index * 125},1,0,0:0:0:0:" for index in range(256)))
    jack = nascent.difficulty_breakdown(jacks.encode())[0]
    assert max(jack.mix, key=jack.mix.get) == "jackspeed"

    with pytest.raises(ValueError):
        made_chart().difficulty_breakdown(0.0)


def test_every_rate():
    curves = nascent.difficulty_rates(osu_text().encode())

    assert list(curves[0]) == list(nascent.RATES)
    assert nascent.RATES[0] == nascent.RATE_MIN and nascent.RATES[-1] == nascent.RATE_MAX
    values = list(curves[0].values())
    assert values == sorted(values)


def test_errors(tmp_path):
    with pytest.raises(ValueError, match="could not tell"):
        nascent.load(b"hello")

    with pytest.raises(ValueError, match="not supported"):
        nascent.load(b"#NOTES:lights-cabinet::Easy:1::00000000;\n")

    with pytest.raises(ValueError, match="osu!mania"):
        nascent.load(b"osu file format v14\n[General]\nMode: 0\n")


def test_calculator_identity():
    assert nascent.CALCULATOR_NAME == "Nascent Calc V3"
    assert len(nascent.difficulty_fingerprint()) == 64
    assert nascent.__version__


def made_chart():
    notes = [nascent.Note(index % 4, 1000 + index * 250) for index in range(32)]
    notes.append(nascent.Note(0, 10000, end_time=11000))
    return nascent.Chart.create(notes, 4, [nascent.TimingPoint(0, 240)], title="Made", difficulty_name="Test")


def test_chart_data():
    chart = made_chart()

    assert chart.title == "Made" and chart.key_count == 4 and chart.note_count == 33 and chart.hold_count == 1
    assert chart.notes[0] == nascent.Note(0, 1000)
    assert chart.notes[-1].is_hold and chart.notes[-1].end_time == 11000
    assert chart.notes_at(2.0)[1].time == 625
    assert chart.timing_points[0].bpm == pytest.approx(240)
    assert chart.timing_points_at(1.5)[0].bpm == pytest.approx(360)
    assert chart.bpm == pytest.approx(240)
    assert chart.difficulty() > 0
    assert nascent.load(chart.to_json().encode())[0].hash == chart.hash

    with pytest.raises(ValueError, match="lane"):
        nascent.Chart.create([nascent.Note(5, 0)], 4)


def test_judging():
    windows = nascent.judgement_windows()

    assert list(windows) == ["PERFECT", "AMAZING", "GREAT", "GOOD", "BAD"]
    assert nascent.judge(0) == "PERFECT"
    assert nascent.judge(-windows["GREAT"]) == "GREAT"
    assert nascent.judge(windows["BAD"] + 1000) is None
    assert nascent.HIT_SCORE_ACCURACY["PERFECT"] == 1.0
    assert nascent.judgement_windows(nascent.MAX_HIT_ACCURACY)["PERFECT"] <= windows["PERFECT"]


def test_attempt_and_replay():
    chart = made_chart()
    attempt = nascent.Attempt(chart)

    for note in chart.notes:
        attempt.press(note.lane, note.time)
        attempt.release(note.lane, note.end_time if note.is_hold else note.time + 50)

    attempt.update(12000)
    result = attempt.result()
    assert result.accuracy == 1.0 and result.grade == "X" and attempt.max_combo == 34

    replay = nascent.Replay.from_json(attempt.replay().to_json())
    assert nascent.simulate(chart, replay).accuracy == 1.0

    missed = nascent.Attempt(chart, rate=1.5)
    missed.update(20000)
    assert missed.result().accuracy == 0.0

    with pytest.raises(ValueError, match="time order"):
        attempt.press(0, 0)
