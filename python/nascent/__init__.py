import io
import os
import re
import zipfile
from dataclasses import dataclass
from importlib.metadata import PackageNotFoundError, version
from pathlib import PurePosixPath
from typing import Optional, Union

from ._core import (
    CALCULATOR_NAME,
    HIT_SCORE_ACCURACY,
    HIT_SCORES,
    MAX_HIT_ACCURACY,
    RANKED_HIT_ACCURACY,
    RATE_MAX,
    RATE_MIN,
    RATE_STEP,
    Attempt,
    Chart,
    JudgedNote,
    Note,
    Replay,
    Simulation,
    TimingPoint,
    convert_osu,
    convert_stepmania,
    difficulty_fingerprint,
    grade,
    judge,
    judgement_windows,
    performance,
    simulate,
)

try:
    __version__ = version("nascent")
except PackageNotFoundError:
    __version__ = "0.0.0"

__all__ = [
    "CALCULATOR_NAME",
    "FORMATS",
    "HIT_SCORE_ACCURACY",
    "HIT_SCORES",
    "MAX_HIT_ACCURACY",
    "RANKED_HIT_ACCURACY",
    "RATES",
    "RATE_MAX",
    "RATE_MIN",
    "RATE_STEP",
    "Attempt",
    "Chart",
    "JudgedNote",
    "Note",
    "Rating",
    "Replay",
    "Simulation",
    "TimingPoint",
    "convert_osu",
    "convert_stepmania",
    "difficulty",
    "difficulty_fingerprint",
    "difficulty_rates",
    "grade",
    "judge",
    "judgement_windows",
    "load",
    "performance",
    "simulate",
]

Source = Union[str, os.PathLike, bytes, bytearray]

FORMATS = ("osu", "stepmania", "nascent", "zip")
RATES = tuple(round(RATE_MIN + step * RATE_STEP, 4) for step in range(round((RATE_MAX - RATE_MIN) / RATE_STEP) + 1))

_EXTENSIONS = {".osu": "osu", ".sm": "stepmania", ".ssc": "stepmania", ".json": "nascent", ".osz": "zip", ".zip": "zip"}
_ARCHIVE_CHARTS = (".osu", ".sm", ".ssc")
_STEPMANIA_TAG = re.compile(rb"#(NOTES|NOTEDATA)\s*:", re.IGNORECASE)


@dataclass(frozen=True)
class Rating:
    chart: Chart
    rate: float
    difficulty: float


def _read(source: Source) -> tuple[bytes, Optional[str]]:
    if isinstance(source, (bytes, bytearray)):
        return bytes(source), None

    path = os.fspath(source)

    with open(path, "rb") as file:
        return file.read(), os.path.splitext(path)[1].lower()


def _detect(data: bytes, extension: Optional[str]) -> str:
    if extension in _EXTENSIONS:
        return _EXTENSIONS[extension]

    if data.startswith(b"PK\x03\x04"):
        return "zip"

    start = data.lstrip(b"\xef\xbb\xbf \t\r\n")

    if start.startswith(b"osu file format"):
        return "osu"

    if start.startswith(b"{"):
        return "nascent"

    if _STEPMANIA_TAG.search(data):
        return "stepmania"

    raise ValueError("could not tell what format this chart is in; pass format= one of " + ", ".join(FORMATS))


def _stepmania_charts(data: bytes) -> list[Chart]:
    entries = convert_stepmania(data)
    charts = [Chart.from_json(entry["chart"]) for entry in entries if entry["chart"] is not None]

    if not charts:
        raise ValueError("no playable charts: " + "; ".join(sorted({entry["error"] for entry in entries})))

    return charts


def _archive_charts(data: bytes) -> list[Chart]:
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = [name for name in archive.namelist() if PurePosixPath(name).suffix.lower() in _ARCHIVE_CHARTS]
        ssc = {PurePosixPath(name).with_suffix("") for name in names if PurePosixPath(name).suffix.lower() == ".ssc"}
        names = sorted(name for name in names if not (PurePosixPath(name).suffix.lower() == ".sm" and PurePosixPath(name).with_suffix("") in ssc))
        charts = []
        errors = []

        for name in names:
            try:
                charts.extend(load(archive.read(name), format=_EXTENSIONS[PurePosixPath(name).suffix.lower()]))
            except ValueError as error:
                errors.append(f"{name}: {error}")

    if not charts:
        raise ValueError("no playable charts in the archive" + (": " + "; ".join(errors) if errors else ""))

    return charts


def load(source: Source, format: Optional[str] = None) -> list[Chart]:
    data, extension = _read(source)
    kind = format or _detect(data, extension)

    if kind == "osu":
        return [Chart.from_json(convert_osu(data))]

    if kind == "stepmania":
        return _stepmania_charts(data)

    if kind == "nascent":
        return [Chart.from_json(data)]

    if kind == "zip":
        return _archive_charts(data)

    raise ValueError(f"unknown format {kind!r}; use one of " + ", ".join(FORMATS))


def difficulty(source: Union[Source, Chart], rate: float = 1.0, format: Optional[str] = None) -> list[Rating]:
    charts = [source] if isinstance(source, Chart) else load(source, format)
    return [Rating(chart, rate, chart.difficulty(rate)) for chart in charts]


def difficulty_rates(source: Union[Source, Chart], format: Optional[str] = None) -> list[dict[float, float]]:
    charts = [source] if isinstance(source, Chart) else load(source, format)
    return [{value: chart.difficulty(value) for value in RATES} for chart in charts]
