import json
import math
import struct
import zlib

from ..models import Chart
from ..storage import get_json_gz, sharded_key, storage

VERSION = 1
WIDTH = 1200
HEIGHT = 375
PADDING = 24
BUCKETS = 120
BAR_INSET = 0.1
BACKGROUND = (18, 20, 28)
GRADIENT = ((0x00, 0xB1, 0xE1), (0x00, 0xF5, 0x9A))


def preview_path(chart_hash: str) -> str:
    return f"/api/v1/charts/{chart_hash}/density.png?v={VERSION}"


def preview_key(chart: Chart) -> str:
    return sharded_key("previews", chart.hash, f"-density-{VERSION}.png")


def density(times: list[float]) -> list[float]:
    if not times:
        return []

    end = max(times) or 1
    size = end / BUCKETS
    buckets = [0] * BUCKETS

    for time in times:
        buckets[min(BUCKETS - 1, max(0, math.floor(time / size)))] += 1

    return [notes / (size / 1000) for notes in buckets]


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def gradient_at(position: float) -> tuple[int, ...]:
    start, end = GRADIENT
    return tuple(round(start[channel] + (end[channel] - start[channel]) * position) for channel in range(3))


def render_density(times: list[float]) -> bytes:
    bars = density(times)
    peak = max(1.0, *bars) if bars else 1.0
    span = WIDTH - PADDING * 2
    slot = span / BUCKETS
    bottom = HEIGHT - PADDING
    reach = HEIGHT - PADDING * 2
    palette = [BACKGROUND] + [gradient_at((index + 0.5) / BUCKETS) for index in range(BUCKETS)]
    row = bytearray(WIDTH)
    tops = []

    for index, value in enumerate(bars):
        left = PADDING + round(index * slot + slot * BAR_INSET)
        right = PADDING + round((index + 1) * slot - slot * BAR_INSET)
        tops.append((bottom - round(value / peak * reach), left, right, index + 1))

    tops.sort()
    pending = 0
    raw = bytearray()

    for y in range(HEIGHT):
        while pending < len(tops) and tops[pending][0] <= y:
            _, left, right, color = tops[pending]
            row[left:right] = bytes([color]) * (right - left)
            pending += 1

        raw.append(0)
        raw += row if y < bottom else bytes(WIDTH)

    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 3, 0, 0, 0)
    colors = b"".join(bytes(color) for color in palette)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"PLTE", colors) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


def density_preview(chart: Chart) -> str:
    key = preview_key(chart)

    if not storage().exists(key):
        hits = json.loads(get_json_gz(chart.data_key)).get("hit_objects", [])
        storage().put(key, render_density([float(hit["time"]) for hit in hits]))

    return key
