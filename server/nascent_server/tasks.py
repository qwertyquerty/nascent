from collections.abc import Callable, Iterable
from functools import wraps

import nascent_core as nc
import redis
from flask import Flask, current_app, has_app_context
from sqlalchemy import and_, false, select, update

from .extensions import db
from .models import COUNTED_PLAY_STATUSES, Chart, ChartStatus, Play, User
from .storage import get_json_gz

_worker_app: Flask | None = None


def set_worker_app(app: Flask | None) -> None:
    global _worker_app
    _worker_app = app


def _app() -> Flask:
    global _worker_app

    if _worker_app is None:
        from . import create_app

        _worker_app = create_app()

    return _worker_app


def task(func: Callable) -> Callable:
    @wraps(func)
    def wrapper(*args, **kwargs):
        if _worker_app is None and has_app_context():
            return func(*args, **kwargs)

        with _app().app_context():
            return func(*args, **kwargs)

    return wrapper


def queue():
    if current_app.config["TASKS_INLINE"]:
        return None

    queues = current_app.extensions.setdefault("nascent_queues", {})
    name = current_app.config["TASK_QUEUE"]

    if name not in queues:
        from rq import Queue

        queues[name] = Queue(name, connection=redis.Redis.from_url(current_app.config["REDIS_URL"]))

    return queues[name]


def enqueue(func: Callable, *args, depends_on: list[str] | None = None) -> str | None:
    target = queue()

    if target is None:
        func(*args)
        return None

    from rq import Retry

    retries = current_app.config["TASK_RETRIES"]
    job = target.enqueue(func, *args, depends_on=depends_on or None, retry=Retry(max=len(retries), interval=list(retries)), failure_ttl=7 * 24 * 3600)
    return job.id


def _batches(values: list[int], size: int) -> Iterable[list[int]]:
    for start in range(0, len(values), size):
        yield values[start : start + size]


def enqueue_user_refresh(user_ids: Iterable[int], chart_ids: list[int]) -> list[str]:
    ordered = sorted(set(user_ids))
    jobs = [enqueue(refresh_users, batch, chart_ids) for batch in _batches(ordered, current_app.config["TASK_BATCH_SIZE"])]
    return [job for job in jobs if job is not None]


@task
def refresh_users(user_ids: list[int], chart_ids: list[int]) -> None:
    from .services import ranking

    for user_id in user_ids:
        user = db.session.execute(select(User).where(User.id == user_id).with_for_update()).scalar_one_or_none()

        if user is None:
            db.session.rollback()
            continue

        for chart_id in chart_ids:
            ranking.refresh_profile_best(user_id, chart_id)

        ranking.refresh_user_stats(user)
        db.session.commit()
        ranking.sync_leaderboard(user)


@task
def apply_chart_status(chart_id: int) -> None:
    chart = db.session.get(Chart, chart_id)

    if chart is None:
        return

    ranked = and_(Play.eligible, Play.status.in_(COUNTED_PLAY_STATUSES)) if chart.status == ChartStatus.RANKED else false()
    db.session.execute(update(Play).where(Play.chart_id == chart.id).values(ranked=ranked))
    db.session.commit()

    user_ids = list(db.session.scalars(select(Play.user_id).where(Play.chart_id == chart.id).distinct()))
    enqueue_user_refresh(user_ids, [chart.id])


@task
def fill_chart_difficulties(chart_id: int) -> None:
    from .services.calculator import RATE_KEYS
    from .services.charts import chart_difficulty

    chart = db.session.get(Chart, chart_id)

    if chart is None:
        return

    for rate_key in RATE_KEYS:
        chart_difficulty(chart, rate_key)

    db.session.commit()


@task
def recalculate_chart(chart_id: int, resimulate: bool) -> None:
    from .services import ranking
    from .services.calculator import RATE_KEYS, calculator_version, performance
    from .services.charts import chart_difficulty, native_chart
    from .services.plays import judgement_fields

    chart = db.session.get(Chart, chart_id)

    if chart is None:
        return

    version = calculator_version()
    native = native_chart(chart)

    for rate_key in RATE_KEYS:
        chart_difficulty(chart, rate_key)

    plays = db.session.scalars(select(Play).where(Play.chart_id == chart.id)).all()

    for play in plays:
        if resimulate:
            simulation = nc.simulate(native, nc.Replay.from_json(get_json_gz(play.replay_key)))

            for column, value in judgement_fields(simulation).items():
                setattr(play, column, value)

        play.difficulty = chart_difficulty(chart, play.rate_key)
        play.performance = performance(play.difficulty, play.accuracy)
        play.ranked = play.eligible and play.status in COUNTED_PLAY_STATUSES and chart.status == ChartStatus.RANKED
        play.calculator_version = version

    db.session.flush()

    for user_id, rate_key in {(play.user_id, play.rate_key) for play in plays}:
        ranking.refresh_chart_best(user_id, chart.id, rate_key)

    for user_id in {play.user_id for play in plays}:
        ranking.refresh_profile_best(user_id, chart.id)

    db.session.commit()


@task
def refresh_all_users() -> None:
    enqueue_user_refresh(db.session.scalars(select(User.id)).all(), [])
