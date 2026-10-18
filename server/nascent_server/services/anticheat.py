import itertools
import re
import statistics
from collections import defaultdict
from collections.abc import Mapping
from dataclasses import asdict, dataclass, field
from typing import Any

import nascent_core as nc

REVIEW = "review"
INFO = "info"

RELEASE_VERSION = re.compile(r"^\d+\.\d+\.\d+(\.(indev|alpha|beta|rc)\d+)?$")
OFFICIAL_COMPILERS = {"Windows": "MSVC", "Linux": "GCC", "macOS": "Clang"}
CLOCK_MIN_INTERVAL_MS = 500.0
FINGERPRINT_MIN_NOTES = 50
FINGERPRINT_MIN_TAPS = 30
FINGERPRINT_MIN_CHORDS = 15
FINGERPRINT_MAX_TAP_MS = 150
FINGERPRINT_RULES = {
    "hold_ms": (-2.0, 4.0, 3.0),
    "error_sd": (-3.0, None, 1.0),
    "error_drift": (-3.0, None, 0.05),
    "chord_spread_ms": (-3.0, None, 1.0),
}


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
    recent_performances: list[float] = field(default_factory=list)


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
        flags = []
        baseline = history.best_performance
        limit = baseline * config["ANTICHEAT_SPIKE_RATIO"] + config["ANTICHEAT_SPIKE_MARGIN"]

        if performance > limit:
            flags.append(
                Flag(
                    "performance_spike",
                    REVIEW,
                    "This play is worth far more than the player's established best",
                    {"performance": round(performance, 3), "previous_best": round(baseline, 3)},
                )
            )

        streak_floor = baseline * config["ANTICHEAT_STREAK_RATIO"]
        streak = sum(1 for value in history.recent_performances if value > streak_floor)

        if performance > streak_floor and streak + 1 >= config["ANTICHEAT_STREAK_COUNT"]:
            flags.append(
                Flag(
                    "performance_streak",
                    REVIEW,
                    "Several recent plays are well beyond the player's established best",
                    {"performance": round(performance, 3), "previous_best": round(baseline, 3), "recent_above": streak + 1},
                )
            )

        return flags

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


def build_flags(diagnostics: Any, staff: bool, config: Mapping[str, Any]) -> list[Flag]:
    if not isinstance(diagnostics, dict) or not isinstance(diagnostics.get("version"), str):
        return []

    version = diagnostics["version"]
    platform = diagnostics.get("platform") if isinstance(diagnostics.get("platform"), str) else ""
    official = diagnostics.get("official_build")
    reasons = []

    if official is False:
        reasons.append("not_official")

    if official is True and not diagnostics.get("build_commit"):
        reasons.append("missing_commit")

    if not RELEASE_VERSION.match(version):
        reasons.append("version")

    parts = platform.split()
    expected = OFFICIAL_COMPILERS.get(parts[0]) if parts else None

    if expected is not None and (len(parts) < 3 or parts[2] != expected):
        reasons.append("toolchain")

    if not reasons:
        return []

    severity = REVIEW if config["ANTICHEAT_FLAG_UNOFFICIAL_BUILDS"] and not staff else INFO
    data = {"version": version[:64], "platform": platform[:64], "commit": str(diagnostics.get("build_commit") or "")[:64], "reasons": reasons}
    return [Flag("unofficial_build", severity, "This play was recorded on a build that isn't an official release", data)]


def clock_flags(samples: Any, config: Mapping[str, Any]) -> list[Flag]:
    if samples is None:
        return [Flag("clock_samples_missing", INFO, "This replay is from a client without clock samples, so the time-warp check was skipped")]

    if not isinstance(samples, list) or len(samples) < config["ANTICHEAT_CLOCK_MIN_SAMPLES"] or len(samples) > config["ANTICHEAT_CLOCK_MAX_SAMPLES"]:
        return []

    points = []

    for sample in samples:
        if not isinstance(sample, list) or len(sample) < 2 or not all(isinstance(value, (int, float)) for value in sample[:2]):
            return []

        points.append((float(sample[0]), float(sample[1])))

    ratios = []

    for (wall_a, song_a), (wall_b, song_b) in itertools.pairwise(points):
        wall = wall_b - wall_a
        song = song_b - song_a

        if wall >= CLOCK_MIN_INTERVAL_MS and song > 0:
            ratios.append(song / wall)

    if len(ratios) < config["ANTICHEAT_CLOCK_MIN_SAMPLES"] // 2:
        return []

    ratio = statistics.median(ratios)

    if abs(ratio - 1.0) <= config["ANTICHEAT_CLOCK_TOLERANCE"]:
        return []

    return [
        Flag(
            "clock_rate_mismatch",
            REVIEW,
            "The song ran at a different speed than real time during this play",
            {"ratio": round(ratio, 4), "samples": len(points)},
        )
    ]


def _autocorrelation(values: list[float]) -> float:
    mean = statistics.fmean(values)
    variance = sum((value - mean) ** 2 for value in values)

    if variance <= 0:
        return 0.0

    return sum((values[index] - mean) * (values[index + 1] - mean) for index in range(len(values) - 1)) / variance


def fingerprint(simulation: nc.Simulation, inputs: list[tuple[int, int, bool]]) -> dict[str, float] | None:
    played = sorted((note for note in simulation.notes if note.hit_played and note.hit_score != "MISS"), key=lambda note: (note.time, note.lane))

    if len(played) < FINGERPRINT_MIN_NOTES:
        return None

    errors = [float(note.hit_error) for note in played]
    result = {"error_sd": round(_stdev(errors), 3), "error_drift": round(_autocorrelation(errors), 4)}

    durations = press_durations(inputs)
    taps = [durations[key] for note in played if not note.is_hold and (key := (note.lane, note.time + note.hit_error)) in durations]
    taps = [duration for duration in taps if duration < FINGERPRINT_MAX_TAP_MS]

    if len(taps) >= FINGERPRINT_MIN_TAPS:
        result["hold_ms"] = round(float(statistics.median(taps)), 2)

    chords: dict[int, list[int]] = defaultdict(list)

    for note in played:
        chords[note.time].append(note.time + note.hit_error)

    spreads = [max(presses) - min(presses) for presses in chords.values() if len(presses) >= 2]

    if len(spreads) >= FINGERPRINT_MIN_CHORDS:
        result["chord_spread_ms"] = round(float(statistics.median(spreads)), 2)

    return result


def fingerprint_flags(current: dict[str, float], history: list[dict[str, float]], config: Mapping[str, Any]) -> list[Flag]:
    unusual = []

    for metric, (low, high, floor) in FINGERPRINT_RULES.items():
        values = [entry[metric] for entry in history if isinstance(entry, dict) and isinstance(entry.get(metric), (int, float))]

        if metric not in current or len(values) < config["ANTICHEAT_FINGERPRINT_MIN_PLAYS"]:
            continue

        median = statistics.median(values)
        spread = max(1.4826 * statistics.median([abs(value - median) for value in values]), floor)
        z = (current[metric] - median) / spread

        if (low is not None and z < low) or (high is not None and z > high):
            unusual.append({"metric": metric, "value": current[metric], "usual": round(median, 3), "z": round(z, 2), "plays": len(values)})

    if not unusual:
        return []

    return [Flag("fingerprint_mismatch", REVIEW, "This play doesn't match how the player usually plays", {"metrics": unusual})]


def needs_review(flags: list[Flag]) -> bool:
    return any(flag.severity == REVIEW for flag in flags)
