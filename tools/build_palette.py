import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIN = ROOT / "assets" / "skins" / "default" / "skin.json"
PALETTE = ROOT / "server" / "web" / "palette.js"


def palette_source() -> str:
    skin = json.loads(SKIN.read_text(encoding="utf-8"))
    palette = {"colors": skin["colors"], "difficulty_scale": skin["difficulty_scale"]}
    return f"const PALETTE = {json.dumps(palette, indent=4)};\n"


if __name__ == "__main__":
    PALETTE.write_text(palette_source(), encoding="utf-8", newline="\n")
    print(f"wrote {PALETTE.relative_to(ROOT)}")
