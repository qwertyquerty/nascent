import math
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any
from urllib.parse import quote, urlencode

from flask import current_app, request

from ..ids import parse_id
from ..services import ranking

SITE_NAME = "Nascent"
TAGLINE = "A vertical scrolling rhythm game passion project"
DOWNLOAD_URL = "https://github.com/qwertyquerty/nascent/releases"
SOURCE_URL = "https://github.com/qwertyquerty/nascent"
INDEX = "index, follow, max-image-preview:large"
NOINDEX = "noindex, follow"
PAGE_SIZE = 50
NAV = [("/", "Home"), ("/leaderboard", "Leaderboard"), ("/charts", "Charts"), ("/plays", "Plays"), ("/stats", "Stats")]
PRIVATE = {"staff": "Staff", "login": "Sign in", "forgot": "Forgot password", "reset": "Choose a new password", "account": "Account"}


@dataclass
class Page:
    title: str
    heading: str
    description: str
    path: str
    robots: str = INDEX
    status: int = 200
    intro: list[str] = field(default_factory=list)
    sections: list[tuple[str, list[tuple[str, str, str]]]] | None = None
    structured: list[dict[str, Any]] = field(default_factory=list)


def site_url(path: str) -> str:
    return current_app.config["SITE_URL"] + path


def with_query(path: str, **params: Any) -> str:
    kept = {name: value for name, value in params.items() if value not in (None, "")}
    return f"{path}?{urlencode(kept)}" if kept else path


def user_path(name: str) -> str:
    return f"/users/{quote(name, safe='')}"


def chart_path(chart_hash: str) -> str:
    return f"/charts/{chart_hash}"


def api(path: str, **params: Any) -> dict | None:
    response = current_app.test_client().get(f"/api/v1{path}", query_string={name: value for name, value in params.items() if value not in (None, "")})
    return response.get_json(silent=True) if response.status_code == 200 else None


def chart_name(chart: dict) -> str:
    return f"{chart['artist']} - {chart['title']} [{chart['difficulty_name']}]"


def result(play: dict) -> str:
    performance = f" · {play['performance']:.2f} performance" if play["ranked"] else ""
    return f"{play['grade']} {play['accuracy'] * 100:.2f}%{performance}"


def length(ms: int) -> str:
    seconds = round(ms / 1000)
    return f"{seconds // 60}:{seconds % 60:02d}"


def date(value: str | None) -> str:
    return datetime.fromisoformat(value).strftime("%b %d, %Y") if value else "never"


def key_counts() -> tuple[int, ...]:
    return ranking.ranked_key_counts()


def requested_page() -> int:
    return min(max(parse_id(request.args.get("page")) or 1, 1), 2000)


def requested_key_count() -> int:
    requested = parse_id(request.args.get("key_count"))
    return requested if requested in key_counts() else key_counts()[0]


def board_path(key_count: int) -> str:
    return with_query("/leaderboard", key_count=None if key_count == key_counts()[0] else key_count)


def more_pages(path: str, page: int, total: int, **params: Any) -> list[tuple[str, str, str]]:
    links = [(with_query(path, **params, page=page - 1 if page > 2 else None), "Previous page", "")] if page > 1 else []
    return links + ([(with_query(path, **params, page=page + 1), "Next page", "")] if page < math.ceil(total / PAGE_SIZE) else [])


def home() -> Page:
    key_count = key_counts()[0]
    players = (api("/leaderboards/performance", key_count=key_count, limit=10) or {}).get("entries", [])
    plays = (api("/plays", sort="performance", key_count=key_count, limit=10) or {}).get("plays", [])
    description = (
        f"Nascent is a vertical scrolling rhythm game passion project for Windows, macOS and Linux. "
        f"Play osu!mania beatmaps from {min(key_counts())}K to {max(key_counts())}K, climb global leaderboards and watch replays."
    )
    sections = [
        ("Get Nascent", [(DOWNLOAD_URL, "Download Nascent for free", "Windows, macOS and Linux"), (SOURCE_URL, "Source code on GitHub", "")]),
        (f"Top {key_count}K players", [(user_path(entry["user"]["username"]), entry["user"]["username"], f"{entry['performance']:.2f} performance") for entry in players]),
        (f"Best {key_count}K plays", [(f"/plays/{play['id']}", f"{play['user']['username']} on {chart_name(play['chart'])}", result(play)) for play in plays]),
        ("Leaderboards", [(board_path(count), f"{count}K leaderboard", "") for count in key_counts()]),
    ]
    game = {
        "@context": "https://schema.org",
        "@type": "VideoGame",
        "name": SITE_NAME,
        "url": site_url("/"),
        "description": description,
        "image": site_url("/og.png"),
        "genre": ["Rhythm game", "Music game"],
        "gamePlatform": ["Windows", "macOS", "Linux"],
        "applicationCategory": "GameApplication",
        "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"},
        "downloadUrl": DOWNLOAD_URL,
        "sameAs": [SOURCE_URL],
    }
    site = {"@context": "https://schema.org", "@type": "WebSite", "name": SITE_NAME, "url": site_url("/")}
    return Page(f"{SITE_NAME} · {TAGLINE}", SITE_NAME, description, "/", intro=[description], sections=sections, structured=[site, game])


def leaderboard() -> Page:
    key_count, page = requested_key_count(), requested_page()
    board = api("/leaderboards/performance", key_count=key_count, limit=PAGE_SIZE, offset=(page - 1) * PAGE_SIZE) or {}
    entries, total = board.get("entries", []), board.get("total", 0)
    heading = f"{key_count}K leaderboard" + (f" · page {page}" if page > 1 else "")
    description = f"Global {key_count}K rankings for Nascent, a vertical scrolling rhythm game passion project: {total:,} ranked players by performance and accuracy."
    players = [(user_path(entry["user"]["username"]), f"#{entry['rank']} {entry['user']['username']}", f"{entry['performance']:.2f} performance") for entry in entries]
    key_count_param = None if key_count == key_counts()[0] else key_count
    sections = [("Players", players), ("More", more_pages("/leaderboard", page, total, key_count=key_count_param)), ("Key modes", [(board_path(count), f"{count}K", "") for count in key_counts()])]
    missing = page > 1 and not entries
    path = with_query("/leaderboard", key_count=key_count_param, page=page if page > 1 else None)
    return Page(f"{heading} · {SITE_NAME}", heading, description, path, NOINDEX if missing else INDEX, 404 if missing else 200, [description], sections)


def charts() -> Page:
    args, page = request.args, requested_page()
    show_all, key_count = args.get("status") == "all", parse_id(args.get("key_count"))
    query = {"q": args.get("q"), "key_count": key_count, "status": None if show_all else "ranked", "sort": args.get("sort") or "play_count", "order": args.get("order") or "desc"}
    found = api("/charts", **query, limit=PAGE_SIZE, offset=(page - 1) * PAGE_SIZE) or {}
    listed, total = found.get("charts", []), found.get("total", 0)
    heading = ("All" if show_all else "Ranked") + (f" {key_count}K" if key_count else "") + " charts"
    description = f"Browse {total:,} {heading.lower()} on Nascent: osu!mania beatmaps with difficulty ratings, leaderboards and download links."
    items = [(chart_path(chart["hash"]), chart_name(chart), f"{chart['key_count']}K · {chart['difficulty']:.2f} difficulty · {length(chart['duration_ms'])}") for chart in listed]
    params = {"key_count": key_count, "status": "all" if show_all else None}
    sections = [(heading, items), ("More", more_pages("/charts", page, total, **params))]
    filtered = any(args.get(name) for name in ("q", "min_difficulty", "max_difficulty", "min_length", "max_length", "sort", "order"))
    missing = page > 1 and not listed
    path = with_query("/charts", **params, page=page if page > 1 else None)
    return Page(f"{heading} · {SITE_NAME}", heading, description, path, NOINDEX if filtered or missing else INDEX, 404 if missing else 200, [description], sections)


def user(reference: str) -> Page:
    data = api(f"/users/{quote(reference, safe='')}")

    if data is None:
        return not_found()

    profile = data["user"]
    name = profile["username"]
    modes = [mode for mode in profile["key_modes"] if mode["key_count"] in key_counts() and mode["ranked_play_count"] > 0]
    best = max(modes, key=lambda mode: mode["performance"], default=None)
    title, summary = f"{name} · {SITE_NAME} player", f"{profile['stats']['play_count']:,} plays"

    if best is not None:
        rank = f"#{best['global_rank']:,}" if best["global_rank"] else "unranked"
        title = f"{name} · {best['key_count']}K {rank}"
        summary = f"{best['key_count']}K {rank} of {best['ranked_players']:,} · {best['performance']:.2f} performance · {best['accuracy'] * 100:.2f}% accuracy"

    description = f"{name} plays Nascent, a vertical scrolling rhythm game passion project. {summary} · {profile['stats']['ranked_play_count']:,} ranked plays."
    modes_list = [(board_path(mode["key_count"]), f"{mode['key_count']}K", f"#{mode['global_rank'] or '-'} · {mode['performance']:.2f} performance") for mode in modes]
    top = (api(f"/users/{profile['id']}/plays/best", key_count=best["key_count"], limit=20) or {}).get("plays", []) if best else []
    plays = [(chart_path(entry["play"]["chart"]["hash"]), chart_name(entry["play"]["chart"]), result(entry["play"])) for entry in top]
    person = {"@type": "Person", "name": name, "identifier": str(profile["id"]), "url": site_url(user_path(name))}
    structured = [{"@context": "https://schema.org", "@type": "ProfilePage", "dateCreated": profile["created_at"], "mainEntity": person}]
    intro = [description, f"Joined {date(profile['created_at'])} · last played {date(profile['last_played_at'])}"]
    sections = [("Key modes", modes_list), (f"Top {best['key_count']}K plays" if best else "Top plays", plays)]
    return Page(f"{title} · {SITE_NAME}", name, description, user_path(name), intro=intro, sections=sections, structured=structured)


def chart(chart_hash: str) -> Page:
    data = api(f"/charts/{quote(chart_hash, safe='')}")

    if data is None:
        return not_found()

    found = data["chart"]
    name = chart_name(found)
    board = api(f"/charts/{found['hash']}/leaderboard", limit=PAGE_SIZE) or {}
    description = (
        f"{found['key_count']}K osu!mania chart for {found['artist']} - {found['title']}, mapped by {found['mapper']}: {found['difficulty']:.2f} difficulty, "
        f"{length(found['duration_ms'])} long, {found['note_count']:,} notes, {found['status']}. Leaderboard of {board.get('total', 0):,} players on Nascent."
    )
    sources = [(source["url"], f"View on {source['source']}", "") for source in found.get("sources", []) if (source.get("url") or "").startswith("https://")]
    scores = [(f"/plays/{entry['play']['id']}", f"#{entry['rank']} {entry['play']['user']['username']}", result(entry["play"])) for entry in board.get("entries", [])]
    return Page(f"{name} · {found['key_count']}K chart · {SITE_NAME}", name, description, chart_path(found["hash"]), intro=[description], sections=[("Links", sources), ("Leaderboard", scores)])


def play(reference: str) -> Page:
    play_id = parse_id(reference)
    data = api(f"/plays/{play_id}") if play_id is not None else None

    if data is None:
        return not_found()

    found = data["play"]
    player = found["user"]["username"]
    heading = f"{player} · {found['grade']} {found['accuracy'] * 100:.2f}% on {chart_name(found['chart'])}"
    description = f"{found['key_count']}K · {found['rate']:.2f}x · {result(found)} · {found['max_combo']:,}x combo · {date(found['submitted_at'])}. Watch the replay on Nascent."
    links = [(user_path(player), player, "player"), (chart_path(found["chart"]["hash"]), chart_name(found["chart"]), "chart")]
    return Page(f"{heading} · {SITE_NAME}", heading, description, f"/plays/{found['id']}", intro=[description], sections=[("Details", links)])


def stats() -> Page:
    numbers = api("/stats") or {}
    description = f"Nascent by the numbers: {numbers.get('players', 0):,} players, {numbers.get('plays', 0):,} plays and {numbers.get('ranked_charts', 0):,} ranked charts."
    return Page(f"Stats · {SITE_NAME}", "Nascent stats", description, "/stats")


def plays() -> Page:
    description = "Recent and top plays on Nascent, a vertical scrolling rhythm game passion project, with accuracy, performance and replays."
    return Page(f"Plays · {SITE_NAME}", "Plays", description, "/plays", NOINDEX)


def private(section: str) -> Page:
    return Page(f"{PRIVATE[section]} · {SITE_NAME}", PRIVATE[section], f"{PRIVATE[section]} on Nascent.", f"/{section}", NOINDEX)


def not_found() -> Page:
    return Page(f"Page not found · {SITE_NAME}", "Page not found", "That page doesn't exist on Nascent.", request.path, "noindex", 404, sections=[("Try", [(path, name, "") for path, name in NAV])])


def build_page(path: str) -> Page:
    parts = [part for part in path.split("/") if part]
    sections = {"leaderboard": leaderboard, "charts": charts, "plays": plays, "stats": stats}
    details = {"users": user, "charts": chart, "plays": play}

    if not parts:
        return home()

    if len(parts) == 1 and parts[0] in sections:
        return sections[parts[0]]()

    if len(parts) == 1 and parts[0] in PRIVATE:
        return private(parts[0])

    if len(parts) == 2 and parts[0] in details:
        return details[parts[0]](parts[1])

    return not_found()
