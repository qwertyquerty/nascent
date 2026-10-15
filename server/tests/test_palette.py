import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def test_website_palette_matches_the_default_skin():
    skin = json.loads((ROOT / "assets" / "skins" / "default" / "skin.json").read_text(encoding="utf-8"))
    source = (ROOT / "server" / "web" / "palette.js").read_text(encoding="utf-8").strip()
    assert source.startswith("const PALETTE = ") and source.endswith(";"), "palette.js is malformed, run python tools/build_palette.py"
    palette = json.loads(source.removeprefix("const PALETTE = ").removesuffix(";"))
    assert palette == {"colors": skin["colors"], "difficulty_scale": skin["difficulty_scale"]}, "palette.js is out of date, run python tools/build_palette.py"
