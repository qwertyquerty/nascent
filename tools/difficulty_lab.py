import argparse
import glob
import itertools
import json
import math
import os
import pickle
import random
import re
from pathlib import Path

import numpy as np
from scipy.optimize import differential_evolution
from scipy.signal import lfilter

REPO = Path(__file__).resolve().parent.parent
SONGS = REPO / "assets" / "songs"
PARAMS_PATH = Path(__file__).resolve().parent / "difficulty_params.json"
CACHE_PATH = REPO / ".cache" / "difficulty_dataset.pkl"

PACKS = ["Tech - v1", "Speed - v2", "Stamina - v1", "Jack - v1", "LN - v2"]
REFERENCE_PACK = "Zeta - v1"
REFERENCE_RATING = 100.0
ALIGNMENT_WEIGHT = 4.0
RANKS = {f"{n}{s}": n for n, s in [(1, "st"), (2, "nd"), (3, "rd")] + [(n, "th") for n in range(4, 11)]}
RANKS.update({"Alpha": 11, "Beta": 12, "Gamma": 13, "Delta": 14, "Epsilon": 15})

WINDOW = 1000.0
STEP = 250.0

DEFAULTS = {
    "finger_power": 3.0,
    "hand_power": 3.0,
    "finger_weight": 1.0,
    "hand_weight": 1.0,
    "onset_weight": 0.5,
    "chord_bonus": 0.4,
    "release_weight": 0.5,
    "hold_busy_weight": 0.5,
    "rhythm_weight": 0.3,
    "fatigue_time": 30.0,
    "fatigue_blend": 0.3,
    "aggregate_power": 6.0,
    "hand_speed_weight": 0.0,
    "hand_burst_weight": 0.0,
    "finger_speed_weight": 0.0,
    "snap_weight": 0.0,
    "burst_weight": 0.0,
    "variety_weight": 0.0,
    "tempo_weight": 0.0,
    "hold_coverage_weight": 0.0,
    "rating_scale": 1.0,
    "strictness_reference": 5.0,
    "strictness_exponent": 1.0,
}

BOUNDS = {
    "finger_power": (1.0, 8.0),
    "hand_power": (1.0, 8.0),
    "finger_weight": (0.0, 3.0),
    "hand_weight": (0.0, 3.0),
    "onset_weight": (0.0, 3.0),
    "chord_bonus": (0.0, 1.0),
    "release_weight": (0.0, 1.0),
    "hold_busy_weight": (0.0, 2.0),
    "rhythm_weight": (0.0, 2.0),
    "fatigue_time": (2.0, 120.0),
    "fatigue_blend": (0.0, 0.9),
    "aggregate_power": (1.0, 16.0),
    "hand_speed_weight": (0.0, 3.0),
    "hand_burst_weight": (0.0, 3.0),
    "finger_speed_weight": (0.0, 3.0),
    "snap_weight": (0.0, 2.0),
    "burst_weight": (0.0, 3.0),
    "variety_weight": (0.0, 1.0),
    "tempo_weight": (0.0, 1.0),
    "hold_coverage_weight": (0.0, 3.0),
}

SNAP_COMPLEXITY = {1: 0.0, 2: 0.0, 4: 0.1, 8: 0.3, 3: 0.5, 6: 0.6, 12: 0.8, 16: 0.7, 24: 0.9, 32: 0.9}


def snap_complexity(time, timing_points):
    point = timing_points[0]
    for tp in timing_points:
        if tp["time"] <= time + 1:
            point = tp
        else:
            break
    beats = (time - point["time"]) / point["beat_length"]
    fraction = beats - math.floor(beats)
    tolerance = 3.0 / point["beat_length"]
    for divisor in (1, 2, 4, 3, 8, 6, 12, 16, 24, 32):
        position = fraction * divisor
        if abs(position - round(position)) * (1.0 / divisor) < tolerance:
            return SNAP_COMPLEXITY[divisor]
    return 1.0


def hand_of(column, keys):
    half = keys // 2
    if keys % 2 == 1 and column == half:
        return 2
    return 0 if column < half else 1


def parse_chart(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    hits = sorted(data["hit_objects"], key=lambda h: (h["time"], h["key"]))
    timing_points = sorted(data["timing_points"], key=lambda tp: tp["time"])
    return data["info"], hits, timing_points


def strictness_factor(hit_accuracy, p):
    accuracy = min(max(hit_accuracy, 0), 10)
    reference = min(max(p.get("strictness_reference", 5.0), 0), 10)
    return ((20.0 - reference) / (20.0 - accuracy)) ** p.get("strictness_exponent", 1.0)


def extract_features(hits, keys, timing_points, hit_accuracy):
    hand_count = 3 if keys % 2 == 1 else 2
    notes = [(h["time"], h["key"], h["end_time"] if h["hit_type"] == 1 and h["end_time"] > h["time"] else None) for h in hits]
    notes.sort(key=lambda n: (n[0], n[1]))

    holds = [(time, end, column) for time, column, end in notes if end is not None]
    releases = sorted((end, column) for _, end, column in holds)
    busy = []
    for time, column, _ in notes:
        hand = hand_of(column, keys)
        busy.append(any(start < time < end and other != column and hand_of(other, keys) == hand for start, end, other in holds))

    start = notes[0][0]
    end = max(n[0] for n in notes)
    window_starts = np.arange(start, end + 1e-9, STEP)
    count = len(window_starts)

    columns = np.zeros((count, keys))
    busy_columns = np.zeros((count, keys))
    release_columns = np.zeros((count, keys))
    hand_actions = np.zeros((count, hand_count))
    hand_chord_extra = np.zeros((count, hand_count))
    onsets = np.zeros(count)
    irregularity = np.zeros(count)
    hand_speed = np.zeros(count)
    hand_burst = np.zeros(count)
    finger_speed = np.zeros(count)
    snap = np.zeros(count)
    burst = np.zeros(count)
    variety = np.zeros(count)
    tempo = np.zeros(count)
    coverage = np.zeros(count)

    hand_rate = []
    hand_last = [None] * hand_count
    finger_rate = []
    finger_last = [None] * keys
    note_snap = []
    for time, column, _ in notes:
        hand = hand_of(column, keys)
        if hand_last[hand] is not None and time > hand_last[hand]:
            hand_rate.append(1000.0 / max(time - hand_last[hand], 30.0))
        elif hand_last[hand] is not None and time == hand_last[hand]:
            hand_rate.append(None)
        else:
            hand_rate.append(1.0)
        hand_last[hand] = time
        finger_rate.append(1000.0 / max(time - finger_last[column], 30.0) if finger_last[column] is not None else 1.0)
        finger_last[column] = time
        note_snap.append(snap_complexity(time, timing_points) if timing_points else 0.0)

    if holds:
        hold_array = np.array([(s, e) for s, e, _ in holds])
        for w in range(count):
            overlap = np.minimum(hold_array[:, 1], window_starts[w] + WINDOW) - np.maximum(hold_array[:, 0], window_starts[w])
            coverage[w] = float(np.sum(np.maximum(overlap, 0.0))) / WINDOW

    tp_times = np.array([tp["time"] for tp in timing_points]) if timing_points else np.zeros(0)
    times = np.array([n[0] for n in notes])
    note_first = np.searchsorted(times, window_starts, side="left")
    note_last = np.searchsorted(times, window_starts + WINDOW, side="left")
    release_times = np.array([r[0] for r in releases]) if releases else np.zeros(0)
    release_first = np.searchsorted(release_times, window_starts, side="left")
    release_last = np.searchsorted(release_times, window_starts + WINDOW, side="left")

    for w in range(count):
        a, b = note_first[w], note_last[w]
        groups = {}
        onset_times = []
        for i in range(a, b):
            time, column, _ = notes[i]
            columns[w, column] += 1
            if busy[i]:
                busy_columns[w, column] += 1
            key = (time, hand_of(column, keys))
            groups[key] = groups.get(key, 0) + 1
            if not onset_times or onset_times[-1] != time:
                onset_times.append(time)
        for (_, hand), size in groups.items():
            hand_actions[w, hand] += 1
            hand_chord_extra[w, hand] += size - 1
        onsets[w] = len(onset_times)
        if len(onset_times) >= 2:
            gaps_all = np.maximum(np.diff(onset_times), 25.0)
            burst[w] = 1000.0 / float(np.median(np.sort(gaps_all)[:3]))
            variety[w] = len(np.unique(np.round(gaps_all / 10.0)))
        tempo[w] = int(np.searchsorted(tp_times, window_starts[w] + WINDOW) - np.searchsorted(tp_times, window_starts[w]))
        if len(onset_times) >= 3:
            gaps = np.diff(onset_times)
            gaps = gaps[gaps > 0]
            if len(gaps) >= 2:
                irregularity[w] = min(np.mean(np.abs(np.log2(gaps[1:] / gaps[:-1]))), 2.0)
        rates = [hand_rate[i] for i in range(a, b) if hand_rate[i] is not None]
        if rates:
            hand_speed[w] = float(np.mean(rates))
            hand_burst[w] = float(np.max(rates))
        if b > a:
            finger_speed[w] = float(np.max(finger_rate[a:b]))
            snap[w] = float(np.mean(note_snap[a:b]))
        for i in range(release_first[w], release_last[w]):
            release_columns[w, releases[i][1]] += 1

    return {
        "keys": keys,
        "hands": hand_count,
        "columns": columns,
        "busy_columns": busy_columns,
        "release_columns": release_columns,
        "hand_actions": hand_actions,
        "hand_chord_extra": hand_chord_extra,
        "onsets": onsets,
        "irregularity": irregularity,
        "hand_speed": hand_speed,
        "hand_burst": hand_burst,
        "finger_speed": finger_speed,
        "snap": snap,
        "burst": burst,
        "variety": variety,
        "tempo": tempo,
        "coverage": coverage,
        "hit_accuracy": hit_accuracy,
    }


def power_mean(values, power, axis):
    return np.mean(np.power(values, power), axis=axis) ** (1.0 / power)


def difficulty(features, p):
    f = features
    scale = 1000.0 / WINDOW

    columns = f["columns"] + p["hold_busy_weight"] * f["busy_columns"] + p["release_weight"] * f["release_columns"]
    actions = f["hand_actions"] + p["chord_bonus"] * f["hand_chord_extra"]

    finger = power_mean(columns, p["finger_power"], 1) * f["keys"]
    hand = power_mean(actions, p["hand_power"], 1) * f["hands"]
    load = (p["finger_weight"] * finger + p["hand_weight"] * hand + p["onset_weight"] * f["onsets"]) * scale
    load = load + p["hand_speed_weight"] * f["hand_speed"] + p["hand_burst_weight"] * f["hand_burst"] + p["finger_speed_weight"] * f["finger_speed"]
    load = load + p["burst_weight"] * f["burst"] + p["hold_coverage_weight"] * f["coverage"]
    load = load * (1 + p["rhythm_weight"] * f["irregularity"] + p["snap_weight"] * f["snap"] + p["variety_weight"] * f["variety"] / 10.0 + p["tempo_weight"] * np.minimum(f["tempo"], 4.0))

    alpha = 1 - math.exp(-(STEP / 1000.0) / p["fatigue_time"])
    fatigue = lfilter([alpha], [1.0, alpha - 1.0], load)
    effective = (1 - p["fatigue_blend"]) * load + p["fatigue_blend"] * fatigue
    return float(power_mean(effective, p["aggregate_power"], 0)) * p.get("rating_scale", 1.0) * strictness_factor(f["hit_accuracy"], p)


def chart_rank(name):
    match = re.search(r"~ (\w+) ~", name) or re.search(r"^(\d+(?:st|nd|rd|th)) Dan", name)
    return RANKS[match.group(1)] if match else None


def load_dataset():
    ranked = []
    rate_groups = {}
    for pack in PACKS:
        paths = glob.glob(str(SONGS / pack / "*.json"))
        if not paths:
            print(f"warning: no charts found for pack '{pack}' in {SONGS}")
        for path in paths:
            info, hits, timing_points = parse_chart(path)
            name = info["difficulty_name"]
            base = re.sub(r"\s*\d(\.\d+)?x \(.*\)$", "", name)
            rate_match = re.search(r"(\d(?:\.\d+)?)x \(", name)
            entry = {"pack": pack, "name": name, "path": f"{pack}/{os.path.basename(path)}", "features": extract_features(hits, info["key_count"], timing_points, info["hit_accuracy"])}
            if rate_match:
                rate_groups.setdefault((pack, base), []).append((float(rate_match.group(1)), entry))
                continue
            entry["rank"] = chart_rank(name)
            if entry["rank"] is None:
                print(f"warning: could not read a rank from '{name}', skipping it")
                continue
            ranked.append(entry)
            rate_groups.setdefault((pack, base), []).append((1.0, entry))
    rate_groups = {k: sorted(v, key=lambda x: x[0]) for k, v in rate_groups.items() if len(v) > 1}

    reference = None
    reference_paths = sorted(glob.glob(str(SONGS / REFERENCE_PACK / "*.json")))
    if reference_paths:
        info, hits, timing_points = parse_chart(reference_paths[0])
        reference = {"pack": REFERENCE_PACK, "name": info["difficulty_name"], "path": f"{REFERENCE_PACK}/{os.path.basename(reference_paths[0])}", "features": extract_features(hits, info["key_count"], timing_points, info["hit_accuracy"])}
    else:
        print(f"warning: no reference chart found in {SONGS / REFERENCE_PACK}, ratings will not be normalized")

    return ranked, rate_groups, reference


def load_cached_dataset(rebuild):
    if not rebuild and CACHE_PATH.exists():
        dataset = pickle.loads(CACHE_PATH.read_bytes())
        if len(dataset) == 3 and all("path" in entry and "hit_accuracy" in entry["features"] for entry in dataset[0]) and dataset[2] is not None:
            return dataset
    dataset = load_dataset()
    CACHE_PATH.parent.mkdir(parents=True, exist_ok=True)
    CACHE_PATH.write_bytes(pickle.dumps(dataset))
    return dataset


def score(ranked, p, verbose=False):
    inversions = 0
    pairs = 0
    r2s = []
    for pack in PACKS:
        entries = sorted((e for e in ranked if e["pack"] == pack), key=lambda e: e["rank"])
        if len(entries) < 2:
            continue
        values = [(e["rank"], difficulty(e["features"], p), e["name"]) for e in entries]
        pack_inversions = []
        for i in range(len(values)):
            for j in range(i + 1, len(values)):
                if values[i][0] < values[j][0]:
                    pairs += 1
                    if values[i][1] >= values[j][1]:
                        pack_inversions.append((values[i], values[j]))
        inversions += len(pack_inversions)
        r2 = float(np.corrcoef([v[0] for v in values], [v[1] for v in values])[0, 1] ** 2)
        r2s.append(r2)
        if verbose:
            print(f"== {pack}: {len(pack_inversions)} inversions, linear r^2 {r2:.3f}")
            for rank, value, name in values:
                print(f"   rank {rank:2}  {value:8.3f}  {name}")
            for a, b in pack_inversions:
                print(f"   INVERSION rank {a[0]} ({a[1]:.3f}) >= rank {b[0]} ({b[1]:.3f})")
    return inversions, pairs, min(r2s), float(np.mean(r2s))


def rate_check(rate_groups, p):
    failures = []
    for (pack, base), group in rate_groups.items():
        values = [(rate, difficulty(entry["features"], p)) for rate, entry in group]
        for (r1, d1), (r2, d2) in itertools.pairwise(values):
            if d1 >= d2:
                failures.append((pack, base, r1, d1, r2, d2))
    return failures


def reference_check(ranked, reference, p):
    if reference is None:
        return None, []
    value = difficulty(reference["features"], p)
    above = sorted(((difficulty(e["features"], p), e["pack"], e["name"]) for e in ranked if difficulty(e["features"], p) >= value), reverse=True)
    return value, above


def cross_pack_alignment(ranked, p):
    return alignment_from_values([(entry["pack"], entry["rank"], difficulty(entry["features"], p)) for entry in ranked])


def alignment_from_values(pack_rank_values):
    grouped = {}
    for pack, rank, value in pack_rank_values:
        grouped.setdefault((pack, rank), []).append(value)
    by_rank = {}
    all_ranks = []
    all_values = []
    for (pack, rank), values in grouped.items():
        by_rank.setdefault(rank, {})[pack] = float(np.mean(values))
        all_ranks.append(rank)
        all_values.append(float(np.mean(values)))
    if len(set(all_ranks)) < 2:
        return 0.0, 0.0, by_rank
    slope = float(np.polyfit(all_ranks, all_values, 1)[0])
    if slope <= 0:
        return float("inf"), slope, by_rank
    spreads = [float(np.std(list(values.values()))) / slope for values in by_rank.values() if len(values) >= 2]
    return (float(np.mean(spreads)) if spreads else 0.0), slope, by_rank


def objective(ranked, p, reference=None):
    inversions = 0
    hinge = 0.0
    r2s = []
    spreads = []
    all_diffs = []
    pack_rank_values = []
    for pack in PACKS:
        entries = [e for e in ranked if e["pack"] == pack]
        if len(entries) < 2:
            continue
        ranks = np.array([e["rank"] for e in entries], dtype=float)
        diffs = np.array([difficulty(e["features"], p) for e in entries])
        spread = (diffs.max() - diffs.min()) / max(ranks.max() - ranks.min(), 1.0)
        if spread <= 0:
            return 1e9
        lower = ranks[:, None] < ranks[None, :]
        delta = (diffs[:, None] - diffs[None, :]) / spread
        inversions += int(np.sum(lower & (delta >= 0)))
        hinge += float(np.sum(np.maximum(0.0, delta + 0.5)[lower]))
        r2s.append(np.corrcoef(ranks, diffs)[0, 1] ** 2)
        spreads.append(spread)
        all_diffs.append(diffs)
        pack_rank_values.extend((pack, rank, value) for rank, value in zip(ranks, diffs))
    if reference is not None and all_diffs:
        reference_value = difficulty(reference["features"], p)
        others = np.concatenate(all_diffs)
        delta = (others - reference_value) / float(np.mean(spreads))
        inversions += int(np.sum(delta >= 0))
        hinge += float(np.sum(np.maximum(0.0, delta + 0.5)))
    alignment, _, _ = alignment_from_values(pack_rank_values)
    return inversions + 0.05 * hinge + 4.0 * (1 - min(r2s)) + 4.0 * (1 - float(np.mean(r2s))) + ALIGNMENT_WEIGHT * alignment


def normalize(params, reference):
    if reference is None:
        return params
    raw = difficulty(reference["features"], {**params, "rating_scale": 1.0})
    return {**params, "rating_scale": REFERENCE_RATING / raw}


def local_search(ranked, reference, start, iterations, seed):
    rng = random.Random(seed)
    best = dict(start)
    best_value = objective(ranked, best, reference)
    for i in range(iterations):
        temperature = max(0.05, 0.5 * (1 - i / iterations))
        candidate = dict(best)
        for name in rng.sample(list(BOUNDS), rng.randint(1, 3)):
            low, high = BOUNDS[name]
            candidate[name] = min(high, max(low, candidate[name] + rng.gauss(0, (high - low) * temperature / 3)))
        value = objective(ranked, candidate, reference)
        if value < best_value:
            best, best_value = candidate, value
    return best, best_value


def load_params(use_defaults):
    params = dict(DEFAULTS)
    if not use_defaults and PARAMS_PATH.exists():
        params.update(json.loads(PARAMS_PATH.read_text()))
    return params


def save_params(params):
    PARAMS_PATH.write_text(json.dumps({k: round(float(v), 6) for k, v in params.items()}, indent=4))
    print(f"saved parameters to {PARAMS_PATH}")


def report(ranked, rate_groups, reference, params, verbose):
    inversions, pairs, worst, mean = score(ranked, params, verbose=verbose)
    print(f"TOTAL {inversions}/{pairs} inversions, worst r^2 {worst:.3f}, mean r^2 {mean:.3f}")
    for failure in rate_check(rate_groups, params):
        print("RATE FAILURE", failure)
    alignment, slope, by_rank = cross_pack_alignment(ranked, params)
    print(f"ALIGNMENT mean spread between packs at the same rank: {alignment:.2f} ranks ({slope:.2f} rating per rank)")
    if verbose:
        short = {pack: pack.split(" - ")[0] for pack in PACKS}
        print("   rank  " + "".join(f"{short[pack]:>9}" for pack in PACKS) + "   spread")
        for rank in sorted(by_rank):
            values = by_rank[rank]
            cells = "".join(f"{values[pack]:9.2f}" if pack in values else f"{'':9}" for pack in PACKS)
            spread = float(np.std(list(values.values()))) / slope if len(values) >= 2 and slope > 0 else 0.0
            print(f"   {rank:4}  {cells}   {spread:5.2f}")
    value, above = reference_check(ranked, reference, params)
    if value is not None:
        print(f"REFERENCE {reference['name']}: {value:.3f} (target {REFERENCE_RATING:g}), {len(above)} benchmark charts at or above it")
        for other, pack, name in above:
            print(f"   ABOVE REFERENCE {other:.3f}  {pack}  {name}")


RANK_LABELS = {value: key for key, value in RANKS.items()}
MARKDOWN_START = "<!-- ratings:start -->"
MARKDOWN_END = "<!-- ratings:end -->"


def ratings_markdown(ranked, reference, params):
    inversions, pairs, worst, mean = score(ranked, params)
    alignment, slope, by_rank = cross_pack_alignment(ranked, params)
    present = [pack for pack in PACKS if any(e["pack"] == pack for e in ranked)]
    short = {pack: pack.split(" - ")[0] for pack in present}

    values = {}
    for entry in ranked:
        values.setdefault((entry["pack"], entry["rank"]), []).append((difficulty(entry["features"], params), entry["name"]))

    lines = [MARKDOWN_START, ""]

    if reference is not None:
        lines.append(f"Ratings are normalized so **{reference['name']}** ({REFERENCE_PACK}) is **{difficulty(reference['features'], params):.2f}**.")
        lines.append("")

    lines.append(f"Ordering: {inversions} of {pairs} same pack pairs out of order, linear r² worst {worst:.3f}, mean {mean:.3f}. "
                 f"Packs at the same Dan level are on average {alignment:.2f} Dan levels apart ({slope:.2f} rating per level).")
    lines.append("")
    lines.append("| Dan | " + " | ".join(short[pack] for pack in present) + " |")
    lines.append("| --- | " + " | ".join("---:" for _ in present) + " |")
    for rank in sorted(by_rank):
        cells = []
        for pack in present:
            ratings = sorted(v for v, _ in values.get((pack, rank), []))
            cells.append(" / ".join(f"{v:.2f}" for v in ratings))
        lines.append(f"| {RANK_LABELS[rank]} | " + " | ".join(cells) + " |")

    for pack in present:
        lines.append("")
        lines.append(f"### {pack}")
        lines.append("")
        lines.append("| Dan | Map | Rating |")
        lines.append("| --- | --- | ---: |")
        for entry in sorted((e for e in ranked if e["pack"] == pack), key=lambda e: (e["rank"], difficulty(e["features"], params))):
            name = entry["name"].replace("|", "\\|")
            lines.append(f"| {RANK_LABELS[entry['rank']]} | {name} | {difficulty(entry['features'], params):.2f} |")

    lines.append("")
    lines.append("Generated by `python tools/difficulty_lab.py --markdown tools/README.md` with the parameters in `tools/difficulty_params.json`.")
    lines.append("")
    lines.append(MARKDOWN_END)
    return "\n".join(lines)


def write_markdown(path, text):
    path = Path(path)
    content = path.read_text(encoding="utf-8") if path.exists() else ""
    if MARKDOWN_START in content and MARKDOWN_END in content:
        before = content[:content.index(MARKDOWN_START)]
        after = content[content.index(MARKDOWN_END) + len(MARKDOWN_END):]
        content = before + text + after
    else:
        content = content.rstrip() + ("\n\n" if content.strip() else "") + text + "\n"
    path.write_text(content, encoding="utf-8")
    print(f"wrote ratings to {path}")


def main():
    parser = argparse.ArgumentParser(description="Evaluate and tune the difficulty model against the ranked benchmark packs in assets/songs.")
    parser.add_argument("--defaults", action="store_true", help="use the built in default parameters instead of tools/difficulty_params.json")
    parser.add_argument("--search", type=int, metavar="ITERATIONS", help="run a local random search from the current parameters")
    parser.add_argument("--seeds", type=int, default=4, help="number of independent local searches to run")
    parser.add_argument("--evolve", type=int, metavar="GENERATIONS", help="run a differential evolution global search")
    parser.add_argument("--population", type=int, default=12, help="differential evolution population multiplier")
    parser.add_argument("--rebuild-cache", action="store_true", help="re-extract chart features instead of using .cache/difficulty_dataset.pkl")
    parser.add_argument("--quiet", action="store_true", help="only print totals")
    parser.add_argument("--export", metavar="PATH", help="write every benchmark chart's difficulty to a JSON file, keyed by '<pack>/<file>'")
    parser.add_argument("--markdown", metavar="PATH", help=f"write the benchmark rating tables into a markdown file, between {MARKDOWN_START} and {MARKDOWN_END} if present")
    parser.add_argument("--normalize", action="store_true", help=f"recompute rating_scale so the {REFERENCE_PACK} chart rates {REFERENCE_RATING:g}, and save it")
    args = parser.parse_args()

    ranked, rate_groups, reference = load_cached_dataset(args.rebuild_cache)
    params = load_params(args.defaults)

    if args.search:
        results = []
        for seed in range(args.seeds):
            best, value = local_search(ranked, reference, params, args.search, seed)
            inversions, pairs, worst, mean = score(ranked, best)
            print(f"seed {seed}: objective {value:.3f}, {inversions}/{pairs} inversions, worst r^2 {worst:.3f}, mean r^2 {mean:.3f}")
            results.append((value, best))
        params = normalize(min(results, key=lambda r: r[0])[1], reference)
        save_params(params)
    elif args.evolve:
        names = list(BOUNDS)
        result = differential_evolution(
            lambda x: objective(ranked, {**params, **dict(zip(names, x))}, reference),
            [BOUNDS[n] for n in names],
            x0=[params[n] for n in names],
            maxiter=args.evolve,
            popsize=args.population,
            seed=7,
            polish=False,
            updating="immediate",
            disp=True,
        )
        params = normalize({**params, **dict(zip(names, result.x))}, reference)
        save_params(params)
    elif args.normalize:
        params = normalize(params, reference)
        save_params(params)

    report(ranked, rate_groups, reference, params, verbose=not args.quiet)

    if args.markdown:
        write_markdown(args.markdown, ratings_markdown(ranked, reference, params))

    if args.export:
        entries = {entry["path"]: entry for entry in ranked}
        if reference is not None:
            entries[reference["path"]] = reference
        for group in rate_groups.values():
            for _, entry in group:
                entries[entry["path"]] = entry
        values = {path: difficulty(entry["features"], params) for path, entry in sorted(entries.items())}
        Path(args.export).write_text(json.dumps(values, indent=4))
        print(f"exported {len(values)} difficulties to {args.export}")


if __name__ == "__main__":
    main()
