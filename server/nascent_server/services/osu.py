import csv
import hashlib
import io
import json
import re
import threading
import time
import urllib.error
import urllib.request
import zipfile
from dataclasses import dataclass
from urllib.parse import urlparse

HOLD = 1 << 7
MANIA = 3
MAX_DOWNLOAD_BYTES = 256 * 1024 * 1024
DOWNLOAD_TIMEOUT_S = 60
USER_AGENT = "nascent-server"
OSU_HOSTS = {"osu.ppy.sh", "old.ppy.sh"}
OSU_FILE_URL = "https://osu.ppy.sh/osu/{id}"
DEFAULT_MIRROR = "https://catboy.best/d/{id}"
DEFAULT_SEARCH = "https://catboy.best/api/v2/search?mode=3&status=1&limit={limit}&offset={offset}"
DEFAULT_FILE_SOURCE = "https://catboy.best/osu/{id}"
DEFAULT_REQUESTS_PER_MINUTE = 900
SEARCH_PAGE_SIZE = 1000
RETRIES = 4
RANKED = "ranked"
DIFFICULTY_PATTERNS = (re.compile(r"#\w+/(\d+)"), re.compile(r"/beatmaps/(\d+)"), re.compile(r"/b/(\d+)"))
SET_PATTERNS = (re.compile(r"/beatmapsets/(\d+)"), re.compile(r"/s/(\d+)"))
FILE_HEADER = "osu file format"


class OsuError(Exception):
    pass


@dataclass(frozen=True)
class RankedBeatmap:
    beatmap_id: int
    checksum: str
    key_count: int
    label: str


class RateLimiter:
    def __init__(self, per_minute: int):
        self.interval = 60.0 / per_minute
        self.lock = threading.Lock()
        self.next_time = time.monotonic()

    def wait(self) -> None:
        with self.lock:
            now = time.monotonic()
            delay = self.next_time - now
            self.next_time = max(now, self.next_time) + self.interval

        if delay > 0:
            time.sleep(delay)

    def pause(self, seconds: float) -> None:
        with self.lock:
            self.next_time = max(self.next_time, time.monotonic() + seconds)


def download(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})

    try:
        with urllib.request.urlopen(request, timeout=DOWNLOAD_TIMEOUT_S) as response:
            data = response.read(MAX_DOWNLOAD_BYTES + 1)
    except urllib.error.HTTPError as error:
        raise OsuError(f"{url} answered with HTTP {error.code}") from None
    except (urllib.error.URLError, TimeoutError) as error:
        raise OsuError(f"could not download {url}: {getattr(error, 'reason', error)}") from None

    if len(data) > MAX_DOWNLOAD_BYTES:
        raise OsuError(f"{url} is larger than {MAX_DOWNLOAD_BYTES // (1024 * 1024)} MB")

    return data


def decode(data: bytes) -> str:
    return data.decode("utf-8-sig", errors="replace")


def archive_files(data: bytes) -> list[tuple[str, str]]:
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        return [(name, decode(archive.read(name))) for name in sorted(archive.namelist()) if name.lower().endswith(".osu")]


def beatmap_files(url: str, mirror: str = DEFAULT_MIRROR) -> list[tuple[str, str]]:
    parsed = urlparse(url)

    if parsed.scheme not in ("http", "https"):
        raise OsuError("the url must start with http:// or https://")

    target = parsed.path + ("#" + parsed.fragment if parsed.fragment else "")

    if parsed.hostname in OSU_HOSTS:
        difficulty = next((match.group(1) for pattern in DIFFICULTY_PATTERNS if (match := pattern.search(target))), None)

        if difficulty is not None:
            text = decode(download(OSU_FILE_URL.format(id=difficulty)))

            if not text.startswith(FILE_HEADER):
                raise OsuError(f"osu! has no beatmap {difficulty}")

            return [(f"beatmap {difficulty}", text)]

        beatmapset = next((match.group(1) for pattern in SET_PATTERNS if (match := pattern.search(target))), None)

        if beatmapset is None:
            raise OsuError("that osu! link is not a beatmap or beatmapset")

        url = mirror.format(id=beatmapset)

    data = download(url)

    if zipfile.is_zipfile(io.BytesIO(data)):
        files = archive_files(data)

        if not files:
            raise OsuError(f"{url} has no .osu files in it")

        return files

    text = decode(data)

    if not text.startswith(FILE_HEADER):
        raise OsuError(f"{url} is not an .osu or .osz file")

    return [(url, text)]


def ranked_mania_beatmaps(search: str = DEFAULT_SEARCH) -> list[RankedBeatmap]:
    beatmaps = {}
    offset = 0

    while True:
        try:
            page = json.loads(download(search.format(limit=SEARCH_PAGE_SIZE, offset=offset)))
        except ValueError:
            raise OsuError(f"the beatmap search at offset {offset} did not answer with JSON") from None

        for beatmapset in page:
            for beatmap in beatmapset.get("beatmaps", []):
                if beatmap.get("mode_int") == MANIA and beatmap.get("status") == RANKED and beatmap.get("checksum"):
                    label = f"{beatmapset.get('artist')} - {beatmapset.get('title')} [{beatmap.get('version')}] ({beatmap['id']})"
                    beatmaps[beatmap["id"]] = RankedBeatmap(beatmap["id"], beatmap["checksum"].lower(), int(beatmap.get("cs") or 0), label)

        offset += len(page)

        if len(page) < SEARCH_PAGE_SIZE:
            return sorted(beatmaps.values(), key=lambda beatmap: beatmap.beatmap_id)


def download_beatmap(beatmap: RankedBeatmap, source: str, limiter: RateLimiter) -> str:
    for attempt in range(RETRIES):
        limiter.wait()

        try:
            data = download(source.format(id=beatmap.beatmap_id))
            break
        except OsuError:
            if attempt + 1 == RETRIES:
                raise

            limiter.pause(2 ** (attempt + 2))

    if hashlib.md5(data).hexdigest() != beatmap.checksum:
        raise OsuError("the downloaded file does not match the ranked version")

    return decode(data)


def read_sections(text: str) -> dict[str, list[str]]:
    sections: dict[str, list[str]] = {}
    current = None

    for line in text.splitlines():
        line = line.strip()

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


def convert(text: str) -> dict | None:
    sections = read_sections(text)
    general = key_values(sections.get("General", []))

    if int(general.get("Mode", 0)) != MANIA:
        return None

    metadata = key_values(sections.get("Metadata", []))
    difficulty = key_values(sections.get("Difficulty", []))
    key_count = int(float(difficulty["CircleSize"]))
    background = next((row[2].strip('"') for row in rows(sections.get("Events", [])) if len(row) > 2 and row[0] == "0"), "")
    hit_objects = []

    for row in rows(sections.get("HitObjects", [])):
        hold = int(row[3]) & HOLD
        hit_objects.append(
            {
                "key": min(int(float(row[0])) * key_count // 512, key_count - 1),
                "time": int(float(row[2])),
                "hit_type": 1 if hold else 0,
                "end_time": int(float(row[5].split(":")[0])) if hold else 0,
            }
        )

    return {
        "info": {
            "title": metadata.get("Title", ""),
            "title_unicode": metadata.get("TitleUnicode", metadata.get("Title", "")),
            "artist": metadata.get("Artist", ""),
            "artist_unicode": metadata.get("ArtistUnicode", metadata.get("Artist", "")),
            "mapper": metadata.get("Creator", ""),
            "difficulty_name": metadata.get("Version", ""),
            "audio_file": general.get("AudioFilename", ""),
            "audio_lead_in": int(general.get("AudioLeadIn", 0)),
            "preview_time": int(general.get("PreviewTime", 0)),
            "hit_accuracy": max(round(float(difficulty.get("OverallDifficulty", 0))) - 4, 0),
            "key_count": key_count,
            "damage": round(float(difficulty.get("HPDrainRate", 0))),
            "image_file": background,
        },
        "timing_points": [
            {"time": int(float(row[0])), "beat_length": float(row[1]), "meter": int(row[2])}
            for row in rows(sections.get("TimingPoints", []))
            if len(row) <= 6 or int(row[6])
        ],
        "hit_objects": sorted(hit_objects, key=lambda hit: (hit["time"], hit["key"])),
    }
