from flask import Blueprint, request
from sqlalchemy import func, or_, select
from sqlalchemy.orm import joinedload, selectinload

from ..errors import ApiError
from ..extensions import db
from ..models import Chart, ChartSource, ChartStatus, Play, User
from ..queries import count_rows
from ..ratelimit import check_rate_limit
from ..schemas import json_body, pagination, query_float, query_int
from ..security import authenticate, current_user, require_not_banned
from ..serializers import chart_detail, chart_summary, play_out
from ..services.calculator import DEFAULT_RATE_KEY, require_rate_key
from ..services.charts import get_chart_or_404, parse_chart, register_chart
from ..services.plays import chart_performance_rank, chart_rank
from ..storage import json_gz_response

blueprint = Blueprint("charts", __name__, url_prefix="/charts")

SORTS = {
    "difficulty": Chart.difficulty,
    "title": Chart.title,
    "artist": Chart.artist,
    "created_at": Chart.created_at,
    "ranked_at": Chart.ranked_at,
    "play_count": Chart.play_count,
    "length": Chart.duration_ms,
    "mapper": Chart.mapper,
    "key_count": Chart.key_count,
}


@blueprint.get("")
def list_charts():
    limit, offset = pagination()
    query = select(Chart)
    status = request.args.get("status")

    if status is not None:
        if status not in {item.value for item in ChartStatus}:
            raise ApiError(400, "invalid_query", "Unknown chart status")

        query = query.where(Chart.status == status)

    if "key_count" in request.args:
        query = query.where(Chart.key_count == query_int("key_count", 4, 1, 32))

    for column, minimum, maximum in (
        (Chart.difficulty, query_float("min_difficulty"), query_float("max_difficulty")),
        (Chart.duration_ms, query_float("min_duration"), query_float("max_duration")),
        (Chart.note_count, query_float("min_notes"), query_float("max_notes")),
    ):
        scale = 1000 if column is Chart.duration_ms else 1

        if minimum is not None:
            query = query.where(column >= minimum * scale)

        if maximum is not None:
            query = query.where(column <= maximum * scale)

    source = (request.args.get("source") or "").replace("\x00", "")

    if source:
        query = query.where(Chart.sources.any(ChartSource.source == source[:16]))

    search = (request.args.get("q") or "").replace("\x00", "").strip()[:100]

    if search:
        pattern = "%" + search.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_") + "%"
        columns = (Chart.title, Chart.title_unicode, Chart.artist, Chart.artist_unicode, Chart.mapper, Chart.difficulty_name)
        query = query.where(or_(*(column.ilike(pattern, escape="\\") for column in columns)))

    sort = request.args.get("sort", "difficulty")

    if sort not in SORTS:
        raise ApiError(400, "invalid_query", f"sort must be one of {', '.join(SORTS)}")

    order = request.args.get("order", "asc")
    column = SORTS[sort].desc() if order == "desc" else SORTS[sort].asc()
    total = count_rows(query)
    charts = db.session.scalars(query.options(selectinload(Chart.sources)).order_by(column, Chart.id.asc()).offset(offset).limit(limit)).all()
    return {"total": total, "charts": [chart_summary(chart, include_sources=True) for chart in charts]}


@blueprint.post("")
def upload_chart():
    user = current_user()
    require_not_banned(user)
    check_rate_limit("chart_upload", user.id)
    body = json_body(large=True)

    if not isinstance(body, dict):
        raise ApiError(400, "invalid_json", "The request body must be a chart object")

    chart, created = register_chart(parse_chart(body), user)
    db.session.commit()
    return {"chart": chart_detail(chart), "created": created}, 201 if created else 200


@blueprint.get("/<chart_hash>")
def get_chart(chart_hash: str):
    return {"chart": chart_detail(get_chart_or_404(chart_hash))}


@blueprint.get("/<chart_hash>/data")
def chart_data(chart_hash: str):
    chart = get_chart_or_404(chart_hash)
    return json_gz_response(chart.data_key, f"{chart.hash}.json")


@blueprint.get("/<chart_hash>/leaderboard")
def leaderboard(chart_hash: str):
    chart = get_chart_or_404(chart_hash)
    limit, offset = pagination(default_limit=50, max_limit=100, max_offset=10_000)
    rate = query_float("rate")
    rate_key = DEFAULT_RATE_KEY if rate is None else require_rate_key(rate)

    query = (
        select(Play)
        .join(User, User.id == Play.user_id)
        .where(Play.chart_id == chart.id, Play.rate_key == rate_key, Play.is_chart_best.is_(True), User.is_banned.is_(False))
    )
    total = count_rows(query)
    plays = db.session.scalars(query.options(joinedload(Play.user)).order_by(Play.accuracy.desc(), Play.max_combo.desc(), Play.id.asc()).offset(offset).limit(limit)).all()

    body = {
        "chart": chart_summary(chart),
        "rate": rate_key / 100,
        "total": total,
        "entries": [{"rank": offset + index + 1, "play": play_out(play, include_chart=False)} for index, play in enumerate(plays)],
    }

    viewer = authenticate()

    if viewer is not None:
        mine = db.session.scalar(
            select(Play).where(Play.chart_id == chart.id, Play.rate_key == rate_key, Play.user_id == viewer.id, Play.is_chart_best.is_(True))
        )
        body["viewer"] = {"rank": chart_rank(mine), "play": play_out(mine, include_chart=False)} if mine is not None and not viewer.is_banned else None

    return body


@blueprint.get("/<chart_hash>/leaderboard/performance")
def performance_leaderboard(chart_hash: str):
    chart = get_chart_or_404(chart_hash)
    limit, offset = pagination(default_limit=50, max_limit=100, max_offset=10_000)

    query = (
        select(Play)
        .join(User, User.id == Play.user_id)
        .where(Play.chart_id == chart.id, Play.is_profile_best.is_(True), User.is_banned.is_(False))
    )
    total = count_rows(query)
    plays = db.session.scalars(query.options(joinedload(Play.user)).order_by(Play.performance.desc(), Play.accuracy.desc(), Play.id.asc()).offset(offset).limit(limit)).all()

    body = {
        "chart": chart_summary(chart),
        "total": total,
        "entries": [{"rank": offset + index + 1, "play": play_out(play, include_chart=False)} for index, play in enumerate(plays)],
    }

    viewer = authenticate()

    if viewer is not None:
        mine = db.session.scalar(select(Play).where(Play.chart_id == chart.id, Play.user_id == viewer.id, Play.is_profile_best.is_(True)))
        body["viewer"] = {"rank": chart_performance_rank(mine), "play": play_out(mine, include_chart=False)} if mine is not None and not viewer.is_banned else None

    return body


@blueprint.get("/<chart_hash>/rates")
def rates(chart_hash: str):
    chart = get_chart_or_404(chart_hash)
    counts = dict(
        db.session.execute(select(Play.rate_key, func.count()).where(Play.chart_id == chart.id, Play.is_chart_best.is_(True)).group_by(Play.rate_key)).all()
    )
    return {
        "chart_hash": chart.hash,
        "rates": [{"rate": entry.rate_key / 100, "difficulty": entry.difficulty, "players": counts.get(entry.rate_key, 0)} for entry in chart.difficulties],
    }
