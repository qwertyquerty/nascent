import csv
import json
import os
import sys

HOLD = 1 << 7
MANIA = 3


def read_sections(path: str) -> dict[str, list[str]]:
    sections: dict[str, list[str]] = {}
    current = None

    with open(path, encoding="utf-8") as file:
        for line in file.read().splitlines():
            if not line or line.startswith("//"):
                continue

            if line.startswith("[") and line.endswith("]"):
                current = sections.setdefault(line[1:-1], [])
            elif current is not None:
                current.append(line)

    return sections


def key_values(lines: list[str]) -> dict[str, str]:
    return {key.strip(): value.strip() for key, _, value in (line.partition(":") for line in lines)}


def rows(lines: list[str]) -> list[list[str]]:
    return list(csv.reader(lines))


def convert(path: str) -> dict | None:
    sections = read_sections(path)
    general = key_values(sections.get("General", []))

    if int(general.get("Mode", 0)) != MANIA:
        return None

    metadata = key_values(sections.get("Metadata", []))
    difficulty = key_values(sections.get("Difficulty", []))
    key_count = int(difficulty["CircleSize"])
    background = next((row[2] for row in rows(sections.get("Events", [])) if row[0] == "0"), "")

    hit_objects = []

    for row in rows(sections.get("HitObjects", [])):
        hold = int(row[3]) & HOLD
        hit_objects.append(
            {
                "key": int(row[0]) * key_count // 512,
                "time": int(row[2]),
                "hit_type": 1 if hold else 0,
                "end_time": int(row[5].split(":")[0]) if hold else 0,
            }
        )

    return {
        "info": {
            "title": metadata["Title"],
            "title_unicode": metadata["TitleUnicode"],
            "artist": metadata["Artist"],
            "artist_unicode": metadata["ArtistUnicode"],
            "mapper": metadata["Creator"],
            "difficulty_name": metadata["Version"],
            "audio_file": general["AudioFilename"],
            "audio_lead_in": int(general["AudioLeadIn"]),
            "preview_time": int(general["PreviewTime"]),
            "hit_accuracy": max(int(round(float(difficulty["OverallDifficulty"]), 0)) - 4, 0),
            "key_count": key_count,
            "damage": int(round(float(difficulty["HPDrainRate"]), 0)),
            "image_file": background,
        },
        "timing_points": [
            {"time": int(float(row[0])), "beat_length": float(row[1]), "meter": int(row[2])}
            for row in rows(sections.get("TimingPoints", []))
            if len(row) <= 6 or int(row[6])
        ],
        "hit_objects": sorted(hit_objects, key=lambda hit: (hit["time"], hit["key"])),
    }


def main() -> None:
    target = sys.argv[1]
    paths = [os.path.join(base, name) for base, _, names in os.walk(target) for name in names] if os.path.isdir(target) else [target]

    for path in paths:
        if not path.endswith(".osu"):
            continue

        chart = convert(path)

        if chart is None:
            print(f"skipped {path}: not an osu!mania beatmap")
            continue

        with open(path + ".json", "w") as file:
            json.dump(chart, file, indent=2)


if __name__ == "__main__":
    main()
