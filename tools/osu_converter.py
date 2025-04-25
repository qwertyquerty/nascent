import osu_parser
import sys
import json
import os

if os.path.isdir(sys.argv[1]):
    files = [os.path.join(sys.argv[1], fn) for fn in os.listdir(sys.argv[1])]
else:
    files = [sys.argv[1]]

for in_file in files:
    if not in_file.endswith(".osu"):
        continue

    osu_map = osu_parser.Map(in_file)

    map_json = {"info": {}, "timing_points": [], "hit_objects": []}

    map_json["info"] = {
        "title": osu_map.metadata["Title"],
        "title_unicode": osu_map.metadata["TitleUnicode"],
        "artist": osu_map.metadata["Artist"],
        "artist_unicode": osu_map.metadata["ArtistUnicode"],
        "mapper": osu_map.metadata["Creator"],
        "difficulty_name": osu_map.metadata["Version"],
        "audio_file": osu_map.general["AudioFilename"],
        "audio_lead_in": int(osu_map.general["AudioLeadIn"]),
        "preview_time": int(osu_map.general["PreviewTime"]),
        "hit_accuracy": max(int(round((float(osu_map.difficulty["OverallDifficulty"])), 0)) - 4, 0),
        "key_count": int(osu_map.difficulty["CircleSize"]),
        "damage": int(round((float(osu_map.difficulty["HPDrainRate"])), 0))
    }

    for timing_point in osu_map.timing_points:
        if timing_point.uninherited:
            map_json["timing_points"].append({
                "time": timing_point.time,
                "beat_length": timing_point.beat_length,
                "meter": timing_point.meter
            })

    for hit_object in osu_map.hit_objects:
        map_json["hit_objects"].append({
            "key": hit_object.key,
            "time": hit_object.time,
            "hit_type": int(bool(hit_object.hit_type & osu_parser.HIT_TYPE_HOLD)),
            "end_time": hit_object.end_time or 0
        })

    map_json["hit_objects"] = sorted(map_json["hit_objects"], key=lambda l: (l["time"], l["key"]))

    out_file = in_file + ".json"

    with open(out_file, "w") as f:
        json.dump(map_json, f, indent=2)
