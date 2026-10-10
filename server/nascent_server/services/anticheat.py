import statistics
from collections import defaultdict
from collections.abc import Mapping
from dataclasses import asdict, dataclass, field
from typing import Any

import nascent_core as nc

REVIEW = "review"
INFO = "info"


@dataclass
class Flag:
    code: str
    severity: str
    message: str
    data: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class PlayerHistory:
    ranked_play_count: int
    best_performance: float


def _stdev(values: list[float]) -> float:
    return statistics.pstdev(values) if len(values) > 1 else 0.0


def press_durations(inputs: list[tuple[int, int, bool]]) -> dict[tuple[int, int], int]:
    pressed_at: dict[int, int] = {}
    durations: dict[tuple[int, int], int] = {}

    for time, lane, pressed in inputs:
        if pressed:
            pressed_at.setdefault(lane, time)
        elif lane in pressed_at:
            start = pressed_at.pop(lane)
            durations[(lane, start)] = time - start

    return durations


def timing_flags(simulation: nc.Simulation, inputs: list[tuple[int, int, bool]], config: Mapping[str, Any]) -> list[Flag]:
    flags: list[Flag] = []
    min_samples = config["ANTICHEAT_MIN_SAMPLES"]
    played = [note for note in simulation.notes if note.hit_played and note.hit_score != "MISS"]

    hit_errors = [note.hit_error for note in played]

    if len(hit_errors) >= min_samples:
        spread = _stdev(hit_errors)

        if spread < config["ANTICHEAT_MIN_HIT_STDEV_MS"]:
            flags.append(Flag("robotic_hit_timing", REVIEW, "Hit timing is far more consistent than a human can play", {"stdev_ms": round(spread, 3), "samples": len(hit_errors)}))

    release_errors = [note.release_error for note in simulation.notes if note.is_hold and note.release_played and note.release_score != "MISS"]

    if len(release_errors) >= min_samples // 2:
        spread = _stdev(release_errors)

        if spread < config["ANTICHEAT_MIN_HIT_STDEV_MS"]:
            flags.append(Flag("robotic_release_timing", REVIEW, "Hold release timing is far more consistent than a human can play", {"stdev_ms": round(spread, 3), "samples": len(release_errors)}))

    durations = press_durations(inputs)
    tap_durations = [durations[key] for note in played if not note.is_hold and (key := (note.lane, note.time + note.hit_error)) in durations]

    if len(tap_durations) >= min_samples:
        spread = _stdev(tap_durations)

        if spread < config["ANTICHEAT_MIN_TAP_DURATION_STDEV_MS"]:
            flags.append(Flag("robotic_tap_duration", REVIEW, "Taps are held for nearly identical lengths of time", {"stdev_ms": round(spread, 3), "samples": len(tap_durations)}))

    chords: dict[int, list[int]] = defaultdict(list)

    for note in played:
        chords[note.time].append(note.time + note.hit_error)

    chord_presses = [presses for presses in chords.values() if len(presses) >= 2]

    if len(chord_presses) >= config["ANTICHEAT_MIN_CHORDS"]:
        identical = sum(1 for presses in chord_presses if len(set(presses)) == 1) / len(chord_presses)

        if identical > config["ANTICHEAT_MAX_IDENTICAL_CHORD_RATIO"]:
            flags.append(Flag("uniform_chords", INFO, "Almost every chord was pressed in the same millisecond", {"ratio": round(identical, 4), "chords": len(chord_presses)}))

    last_press: dict[int, int] = {}
    fast_repeats = 0

    for time, lane, pressed in inputs:
        if not pressed:
            continue

        if lane in last_press and time - last_press[lane] < config["ANTICHEAT_MIN_REPEAT_INTERVAL_MS"]:
            fast_repeats += 1

        last_press[lane] = time

    if fast_repeats > config["ANTICHEAT_MAX_FAST_REPEATS"]:
        flags.append(Flag("fast_repeats", INFO, "Keys were repeated faster than is humanly possible", {"count": fast_repeats}))

    return flags


def performance_flags(performance: float, history: PlayerHistory, config: Mapping[str, Any]) -> list[Flag]:
    if history.ranked_play_count >= config["ANTICHEAT_SPIKE_MIN_RANKED_PLAYS"]:
        limit = history.best_performance * config["ANTICHEAT_SPIKE_RATIO"] + config["ANTICHEAT_SPIKE_MARGIN"]

        if performance > limit:
            return [
                Flag(
                    "performance_spike",
                    REVIEW,
                    "This play is worth far more than the player's previous best",
                    {"performance": round(performance, 3), "previous_best": round(history.best_performance, 3)},
                )
            ]

        return []

    if performance > config["ANTICHEAT_NEW_ACCOUNT_PERFORMANCE"]:
        return [
            Flag(
                "new_account_high_performance",
                REVIEW,
                "A new account set a very high performance play",
                {"performance": round(performance, 3), "ranked_play_count": history.ranked_play_count},
            )
        ]

    return []


def needs_review(flags: list[Flag]) -> bool:
    return any(flag.severity == REVIEW for flag in flags)
