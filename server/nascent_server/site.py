from html import escape
from pathlib import Path

from flask import Blueprint, Response, current_app, request

from .api.lookup import get_user_or_404, get_visible_play_or_404
from .errors import ApiError
from .services import ranking
from .services.charts import get_chart

blueprint = Blueprint("site", __name__)

SITE_NAME = "Nascent"
DEFAULT_DESCRIPTION = "Leaderboards, player profiles, charts and replays for Nascent, a vertical scrolling rhythm game."
TITLE_TAG = "<title>Nascent</title>"


def _length(ms: int) -> str:
    seconds = round(ms / 1000)
    return f"{seconds // 60}:{seconds % 60:02d}"


def _chart_name(chart) -> str:
    return f"{chart.artist} - {chart.title} [{chart.difficulty_name}]"


def _user_meta(reference: str) -> tuple[str, str]:
    user = get_user_or_404(reference)
    modes = [row for row in user.key_stats if row.key_count in ranking.ranked_key_counts() and row.ranked_play_count > 0]

    if not modes:
        return user.username, f"{user.play_count:,} plays on Nascent"

    best = max(modes, key=lambda row: row.performance)
    rank = ranking.global_rank(user, best.key_count)
    players = ranking.ranked_player_count(best.key_count)
    standing = f"#{rank:,} of {players:,}" if rank else "unranked"
    others = ", ".join(f"{row.key_count}K #{ranking.global_rank(user, row.key_count) or '-'}" for row in modes if row is not best)
    description = f"{best.key_count}K {standing} · {best.performance:.2f} performance · {best.accuracy * 100:.2f}% accuracy · {user.ranked_play_count:,} ranked plays"
    return user.username, description + (f" · also {others}" if others else "")


def _chart_meta(chart_hash: str) -> tuple[str, str]:
    chart = get_chart(chart_hash)

    if chart is None:
        raise ApiError(404, "chart_not_found", "No such chart")

    description = f"{chart.key_count}K · {chart.difficulty:.2f} difficulty · {_length(chart.duration_ms)} · {chart.note_count:,} notes · {chart.status} · {chart.play_count:,} plays · mapped by {chart.mapper}"
    return _chart_name(chart), description


def _play_meta(play_id: str) -> tuple[str, str]:
    play = get_visible_play_or_404(int(play_id))
    title = f"{play.user.username} · {play.grade} {play.accuracy * 100:.2f}% on {_chart_name(play.chart)}"
    performance = f"{play.performance:.2f} performance" if play.ranked else "unranked"
    description = f"{play.key_count}K · {play.rate:.2f}x · {performance} · {play.max_combo:,}x combo · {play.submitted_at:%b %d, %Y}"
    return title, description


def _page_meta(path: str) -> tuple[str | None, str]:
    parts = [part for part in path.split("/") if part]

    try:
        if len(parts) == 2 and parts[0] == "users":
            return _user_meta(parts[1])

        if len(parts) == 2 and parts[0] == "charts":
            return _chart_meta(parts[1])

        if len(parts) == 2 and parts[0] == "plays" and parts[1].isdigit():
            return _play_meta(parts[1])
    except ApiError:
        pass

    return None, DEFAULT_DESCRIPTION


def _meta_tags(title: str | None, description: str) -> str:
    full_title = f"{title} · {SITE_NAME}" if title else SITE_NAME
    image = request.url_root + "logo_n.png"
    tags = [
        f"<title>{escape(full_title)}</title>",
        f'<meta name="description" content="{escape(description)}">',
        '<meta property="og:type" content="website">',
        f'<meta property="og:site_name" content="{SITE_NAME}">',
        f'<meta property="og:title" content="{escape(title or SITE_NAME)}">',
        f'<meta property="og:description" content="{escape(description)}">',
        f'<meta property="og:url" content="{escape(request.base_url)}">',
        f'<meta property="og:image" content="{escape(image)}">',
        '<meta name="twitter:card" content="summary">',
    ]
    return "\n    ".join(tags)


@blueprint.get("/")
@blueprint.get("/<path:path>")
def page(path: str = "") -> Response:
    if path.startswith("api/"):
        raise ApiError(404, "not_found", "No such endpoint")

    shell = (Path(current_app.config["WEB_DIR"]) / "index.html").read_text(encoding="utf-8")
    title, description = _page_meta(path)
    response = Response(shell.replace(TITLE_TAG, _meta_tags(title, description), 1), mimetype="text/html")
    response.headers["Cache-Control"] = "no-cache"
    return response
