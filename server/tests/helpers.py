import json
import random

import nascent_core as nc

SPACING = 160


def make_chart(note_count: int = 240, key_count: int = 4, title: str = "Test Song") -> dict:
    hits = []
    time = 1000

    for index in range(note_count):
        lane = (index * 3 + index // key_count) % key_count

        if index % 7 == 0:
            hits.append({"key": lane, "hit_type": 1, "time": time, "end_time": time + SPACING * 2})
            time += SPACING
            continue

        hits.append({"key": lane, "hit_type": 0, "time": time, "end_time": time})

        if index % 11 == 0:
            other = (lane + 2) % key_count
            hits.append({"key": other, "hit_type": 0, "time": time, "end_time": time})

        time += SPACING

    return {
        "info": {
            "title": title,
            "title_unicode": title,
            "artist": "Test Artist",
            "artist_unicode": "Test Artist",
            "mapper": "Tester",
            "difficulty_name": "Hard",
            "audio_file": "audio.mp3",
            "image_file": "",
            "audio_lead_in": 0,
            "preview_time": 0,
            "hit_accuracy": 5,
            "key_count": key_count,
            "damage": 5,
        },
        "timing_points": [{"time": 0, "beat_length": 500.0, "meter": 4}],
        "hit_objects": hits,
    }


def chart_hash(chart: dict) -> str:
    return nc.Chart.from_json(json.dumps(chart)).hash


CHART = make_chart()
HASH = chart_hash(CHART)


def ticket_for(api, token, chart_hash_value=HASH, rate=1.0):
    response = api.post("/api/v1/plays/tickets", {"chart_hash": chart_hash_value, "rate": rate}, token)
    assert response.status_code == 201, response.get_json()
    return response.get_json()["ticket"]


def submit(api, clock, token, replay, chart=CHART, ticket=True, include_chart=True, wait_minutes=5):
    body = {"chart_hash": chart_hash(chart), "replay": replay}

    if ticket:
        body["ticket"] = ticket_for(api, token, body["chart_hash"], replay["rate"])

    if include_chart:
        body["chart"] = chart

    clock.advance(minutes=wait_minutes)
    return api.post("/api/v1/plays", body, token)


def rank_chart(api, chart=CHART, status="ranked"):
    admin = api.admin()
    api.post("/api/v1/charts", chart, admin)
    response = api.put(f"/api/v1/moderation/charts/{chart_hash(chart)}/status", {"status": status}, admin)
    assert response.status_code == 200, response.get_json()
    return admin


def _replay_skeleton(chart: dict, rate: float, inputs: list, end_time: int) -> dict:
    return {
        "version": nc.REPLAY_FORMAT_VERSION,
        "played_at": "2026-10-09T12:00:00Z",
        "chart": {
            "path": "Test/chart.json",
            "gameplay_hash": chart_hash(chart),
            "title": chart["info"]["title"],
            "artist": chart["info"]["artist"],
            "difficulty_name": chart["info"]["difficulty_name"],
            "key_count": chart["info"]["key_count"],
        },
        "rate": rate,
        "settings": {"audio_input_offset": 0, "audio_visual_offset": 0, "scroll_speed": 200},
        "end_time": end_time,
        "result": {"accuracy": 1.0, "grade": "X", "max_combo": 0, "score_counts": {}, "average_error": 0.0, "difficulty": 0.0, "performance": 0.0},
        "inputs": inputs,
    }


def pitched_notes(chart: dict, rate: float) -> tuple[list, int]:
    native = nc.Chart.from_json(json.dumps(chart))
    end = native.end_ms_at(rate)
    empty = nc.Replay.from_json(json.dumps(_replay_skeleton(chart, rate, [], end + 2100)))
    return nc.simulate(native, empty).notes, end


def make_replay(chart: dict, rate: float = 1.0, seed: int = 1, spread: float = 12.0, end_extra: int = 2100) -> dict:
    rng = random.Random(seed)
    notes, end = pitched_notes(chart, rate)
    events = []

    for note in notes:
        press = note.time + round(rng.gauss(0, spread))

        if note.is_hold:
            release = note.end_time + round(rng.gauss(0, spread))
        else:
            release = press + rng.randint(35, 95)

        events.append((press, 1, note.lane))
        events.append((max(release, press + 1), 0, note.lane))

    events.sort(key=lambda event: (event[0], event[1]))
    inputs = [[time, lane, pressed] for time, pressed, lane in events]
    replay = _replay_skeleton(chart, rate, inputs, end + end_extra)
    return with_claimed_result(chart, replay)


def with_claimed_result(chart: dict, replay: dict) -> dict:
    native = nc.Chart.from_json(json.dumps(chart))
    simulation = nc.simulate(native, nc.Replay.from_json(json.dumps(replay)))
    replay["result"] = {
        "accuracy": simulation.accuracy,
        "grade": simulation.grade,
        "max_combo": simulation.max_combo,
        "score_counts": dict(simulation.score_counts),
        "average_error": simulation.average_error,
        "difficulty": 0.0,
        "performance": 0.0,
    }
    return replay
