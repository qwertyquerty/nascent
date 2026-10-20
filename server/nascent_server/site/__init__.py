import math
import time
from pathlib import Path

from flask import Blueprint, Response, current_app, render_template, request
from sqlalchemy import func, or_, select

from ..errors import ApiError
from ..extensions import db
from ..ids import parse_id
from ..models import Chart, ChartStatus, User
from .pages import LEGAL, NAV, build_page, chart_path, key_counts, legal_html, site_url, user_path

blueprint = Blueprint("site", __name__, template_folder="templates")

TITLE_TAG = "<title>Nascent</title>"
PRERENDER_MARKER = "<!--prerender-->"
SITEMAP_CHUNK = 10_000
_cache: dict[str, tuple[float, object]] = {}


def cached(key: str, seconds: int, compute):
    hit = _cache.get(key)

    if seconds > 0 and hit is not None and time.monotonic() - hit[0] < seconds:
        return hit[1]

    if len(_cache) > 5000:
        _cache.clear()

    value = compute()
    _cache[key] = (time.monotonic(), value)
    return value


def xml(entries: list[tuple[str, str | None]], root: str, item: str) -> Response:
    seconds = current_app.config["SITEMAP_CACHE_SECONDS"]
    response = Response(render_template("seo/sitemap.xml", entries=entries, root=root, item=item), mimetype="application/xml")
    response.headers["Cache-Control"] = f"public, max-age={seconds}"
    return response


def listed(kind: str):
    if kind == "charts":
        return select(Chart.hash, Chart.created_at, Chart.ranked_at).where(or_(Chart.status == ChartStatus.RANKED, Chart.play_count > 0)).order_by(Chart.id)

    return select(User.username, User.last_played_at).where(User.is_banned.is_(False), User.play_count > 0).order_by(User.id)


def sitemap_entries(name: str) -> list[tuple[str, str | None]] | None:
    if name == "pages":
        paths = ["/", "/leaderboard", *(f"/leaderboard?key_count={count}" for count in key_counts()[1:]), "/charts", "/charts?status=all", "/stats", "/difficulty", "/privacy", "/terms"]
        return [(site_url(path), None) for path in paths]

    kind, _, number = name.partition("-")
    index = parse_id(number)

    if kind not in ("charts", "users") or not index:
        return None

    rows = db.session.execute(listed(kind).offset((index - 1) * SITEMAP_CHUNK).limit(SITEMAP_CHUNK)).all()

    if kind == "charts":
        return [(site_url(chart_path(row.hash)), (row.ranked_at or row.created_at).date().isoformat()) for row in rows] or None

    return [(site_url(user_path(row.username)), row.last_played_at.date().isoformat() if row.last_played_at else None) for row in rows] or None


@blueprint.get("/fragments/<name>")
def fragment(name: str) -> Response:
    if name not in LEGAL:
        raise ApiError(404, "not_found", "No such page")

    response = Response(legal_html(name), mimetype="text/html")
    response.headers["Cache-Control"] = "public, max-age=3600"
    return response


@blueprint.get("/robots.txt")
def robots() -> Response:
    return Response(f"User-agent: *\nAllow: /\n\nSitemap: {site_url('/sitemap.xml')}\n", mimetype="text/plain")


@blueprint.get("/sitemap.xml")
def sitemap_index() -> Response:
    def names():
        counts = {kind: db.session.scalar(select(func.count()).select_from(listed(kind).subquery())) for kind in ("charts", "users")}
        return ["pages"] + [f"{kind}-{index + 1}" for kind, count in counts.items() for index in range(math.ceil(count / SITEMAP_CHUNK))]

    parts = cached("sitemap", current_app.config["SITEMAP_CACHE_SECONDS"], names)
    return xml([(site_url(f"/sitemaps/{name}.xml"), None) for name in parts], "sitemapindex", "sitemap")


@blueprint.get("/sitemaps/<name>.xml")
def sitemap_part(name: str) -> Response:
    entries = cached(f"sitemap:{name}", current_app.config["SITEMAP_CACHE_SECONDS"], lambda: sitemap_entries(name))

    if entries is None:
        raise ApiError(404, "not_found", "No such sitemap")

    return xml(entries, "urlset", "url")


def render(path: str) -> tuple[str, str, int]:
    page = build_page(path)
    head = render_template("seo/head.html", page=page, canonical=site_url(page.path), image=site_url("/og.png") if page.image else None)
    body = render_template("seo/body.html", page=page, nav=NAV) if page.sections is not None else ""
    return head, body, page.status


@blueprint.get("/")
@blueprint.get("/<path:path>")
def page(path: str = "") -> Response:
    if path.startswith("api/"):
        raise ApiError(404, "not_found", "No such endpoint")

    shell = (Path(current_app.config["WEB_DIR"]) / "index.html").read_text(encoding="utf-8")
    head, body, status = cached(f"page:{request.full_path}", current_app.config["SITE_PAGE_CACHE_SECONDS"], lambda: render(path))
    response = Response(shell.replace(TITLE_TAG, head, 1).replace(PRERENDER_MARKER, body, 1), status=status, mimetype="text/html")
    response.headers["Cache-Control"] = "no-cache"
    return response
