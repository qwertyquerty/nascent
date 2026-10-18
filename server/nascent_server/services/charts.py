import hashlib
import json
import re
from collections import OrderedDict
from collections.abc import Iterable
from threading import Lock

import nascent_core as nc
from flask import current_app
from sqlalchemy import select
from sqlalchemy.exc import IntegrityError

from .. import timeutil
from ..errors import ApiError
from ..extensions import db
from ..models import Chart, ChartDifficulty, ChartSource, ChartStatus, User
from ..storage import get_json_gz, put_json_gz, sharded_key
from .calculator import DEFAULT_RATE_KEY, calculator_version, rate_for_key

MAX_NOTES = 200_000
MAX_DURATION_MS = 3 * 60 * 60 * 1000
MAX_TIMING_POINTS = MAX_NOTES
CHART_TEXT_LIMIT = 1024
HASH_PATTERN = re.compile(r"[0-9a-fA-F]{64}")
NATIVE_CACHE_SIZE = 64
OSU_SOURCE = "osu"
OSU_ID_PATTERNS = {
    "source_id": re.compile(r"^\s*BeatmapID\s*:\s*(\d+)\s*$", re.MULTILINE),
    "source_set_id": re.compile(r"^\s*BeatmapSetID\s*:\s*(\d+)\s*$", re.MULTILINE),
}

_native_cache: OrderedDict[str, nc.Chart] = OrderedDict()
_native_lock = Lock()


def _bounded_chart(data: dict) -> dict:
    timing_points = data.get("timing_points")

    if isinstance(timing_points, list) and len(timing_points) > MAX_TIMING_POINTS:
        raise ApiError(400, "invalid_chart", f"The chart has more than {MAX_TIMING_POINTS:,} timing points")

    info = data.get("info")

    if not isinstance(info, dict):
        return data

    return {**data, "info": {key: value[:CHART_TEXT_LIMIT] if isinstance(value, str) else value for key, value in info.items()}}


def parse_chart(data: dict) -> nc.Chart:
    try:
        chart = nc.Chart.from_json(json.dumps(_bounded_chart(data)))
    except ValueError as error:
        raise ApiError(400, "invalid_chart", f"The chart is invalid: {error}") from None

    if chart.note_count > MAX_NOTES or chart.duration_ms > MAX_DURATION_MS or chart.start_ms < 0:
        raise ApiError(400, "invalid_chart", "The chart is too long, has too many notes or starts before 0ms")

    return chart


def native_chart(chart: Chart) -> nc.Chart:
    with _native_lock:
        cached = _native_cache.get(chart.hash)

        if cached is not None:
            _native_cache.move_to_end(chart.hash)
            return cached

    loaded = nc.Chart.from_json(get_json_gz(chart.data_key))

    with _native_lock:
        _native_cache[chart.hash] = loaded

        while len(_native_cache) > NATIVE_CACHE_SIZE:
            _native_cache.popitem(last=False)

    return loaded


def clear_native_cache() -> None:
    with _native_lock:
        _native_cache.clear()


def get_chart(chart_hash: str) -> Chart | None:
    if not HASH_PATTERN.fullmatch(chart_hash):
        return None

    return db.session.scalar(select(Chart).where(Chart.hash == chart_hash.lower()))


def get_chart_or_404(chart_hash: str) -> Chart:
    chart = get_chart(chart_hash)

    if chart is None:
        raise ApiError(404, "chart_not_found", "No chart with that hash exists")

    return chart


def _text(value: str) -> str:
    return value.replace("\x00", "")[:255]


def register_chart(native: nc.Chart, uploader: User | None, rate_keys: Iterable[int] = ()) -> tuple[Chart, bool]:
    existing = get_chart(native.hash)

    if existing is not None:
        return existing, False

    data_key = sharded_key("charts", native.hash, ".json.gz")
    put_json_gz(data_key, native.to_json())

    version = calculator_version()
    difficulties = {rate_key: native.difficulty(rate_for_key(rate_key)) for rate_key in sorted({DEFAULT_RATE_KEY, *rate_keys})}

    chart = Chart(
        hash=native.hash,
        status=ChartStatus.UNRANKED,
        key_count=native.key_count,
        hit_accuracy=native.hit_accuracy,
        damage=native.damage,
        title=_text(native.title),
        title_unicode=_text(native.title_unicode),
        artist=_text(native.artist),
        artist_unicode=_text(native.artist_unicode),
        mapper=_text(native.mapper),
        difficulty_name=_text(native.difficulty_name),
        note_count=native.note_count,
        hold_count=native.hold_count,
        duration_ms=native.duration_ms,
        difficulty=difficulties[DEFAULT_RATE_KEY],
        calculator_version=version,
        data_key=data_key,
        uploaded_by_id=uploader.id if uploader else None,
        difficulties=[ChartDifficulty(rate_key=key, difficulty=value, calculator_version=version) for key, value in difficulties.items()],
    )

    try:
        with db.session.begin_nested():
            db.session.add(chart)
    except IntegrityError:
        existing = get_chart(native.hash)

        if existing is None:
            raise

        return existing, False

    return chart, True


def read_osu_file(raw: bytes) -> tuple[dict, dict]:
    text = raw.decode("utf-8", errors="replace")
    source = {"source": OSU_SOURCE, "checksum": hashlib.md5(raw).hexdigest()}

    for field, pattern in OSU_ID_PATTERNS.items():
        match = pattern.search(text)
        source[field] = int(match.group(1)) if match and int(match.group(1)) > 0 else None

    return json.loads(nc.convert_osu(text)), source


def add_chart_source(chart: Chart, source: str, checksum: str, source_id: int | None = None, source_set_id: int | None = None) -> ChartSource:
    existing = db.session.scalar(select(ChartSource).where(ChartSource.source == source, ChartSource.checksum == checksum))

    if existing is None:
        existing = ChartSource(chart=chart, source=source, checksum=checksum)
        db.session.add(existing)

    existing.chart = chart
    existing.source_id = source_id if source_id is not None else existing.source_id
    existing.source_set_id = source_set_id if source_set_id is not None else existing.source_set_id
    return existing


def chart_difficulty(chart: Chart, rate_key: int) -> float:
    version = calculator_version()

    for entry in chart.difficulties:
        if entry.rate_key == rate_key and entry.calculator_version == version:
            return entry.difficulty

    value = native_chart(chart).difficulty(rate_for_key(rate_key))
    entry = next((entry for entry in chart.difficulties if entry.rate_key == rate_key), None)

    if entry is None:
        chart.difficulties.append(ChartDifficulty(rate_key=rate_key, difficulty=value, calculator_version=version))
    else:
        entry.difficulty = value
        entry.calculator_version = version

    if rate_key == DEFAULT_RATE_KEY:
        chart.difficulty = value
        chart.calculator_version = version

    return value


def set_chart_status(chart: Chart, status: ChartStatus, moderator: User | None) -> bool:
    if status == ChartStatus.RANKED and chart.key_count not in current_app.config["RANKED_KEY_COUNTS"]:
        counts = current_app.config["RANKED_KEY_COUNTS"]
        raise ApiError(400, "unrankable_key_count", f"Only {counts[0]}K to {counts[-1]}K charts can be ranked")

    if chart.status == status:
        return False

    chart.status = status
    chart.ranked_at = timeutil.utcnow() if status == ChartStatus.RANKED else None
    chart.ranked_by_id = moderator.id if moderator and status == ChartStatus.RANKED else None
    return True
