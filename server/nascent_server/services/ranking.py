from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from functools import cache

import redis
from flask import current_app
from sqlalchemy import case, func, select, update

from ..cache import store
from ..errors import ApiError
from ..extensions import db
from ..models import COUNTED_PLAY_STATUSES, Play, User, UserKeyStats
from ..queries import count_rows

BOARD_PREFIX = "performance"


@dataclass
class Totals:
    play_count: int = 0
    ranked_play_count: int = 0
    total_hits: int = 0
    ranked_playtime_ms: int = 0
    total_playtime_ms: int = 0
    max_combo: int = 0
    last_played_at: datetime | None = None


@cache
def profile_weights(count: int, decay: float) -> tuple[float, ...]:
    return tuple(decay**index for index in range(count))


def current_weights() -> tuple[float, ...]:
    return profile_weights(current_app.config["PROFILE_TOP_PLAYS"], current_app.config["PROFILE_DECAY"])


def profile_performance(performances: list[float], weights: tuple[float, ...]) -> float:
    ordered = sorted(performances, reverse=True)[: len(weights)]
    return sum(weight * value for weight, value in zip(weights, ordered)) / sum(weights)


def profile_accuracy(accuracies: list[float], weights: tuple[float, ...]) -> float:
    if not accuracies:
        return 0.0

    used = weights[: len(accuracies)]
    return sum(weight * value for weight, value in zip(used, accuracies)) / sum(used)


def ranked_key_counts() -> tuple[int, ...]:
    return tuple(current_app.config["RANKED_KEY_COUNTS"])


def check_key_count(key_count: int | None) -> int:
    counts = ranked_key_counts()

    if key_count not in counts:
        raise ApiError(400, "invalid_key_count", f"key_count must be one of {counts[0]}K to {counts[-1]}K")

    return key_count


def board_name(key_count: int) -> str:
    return f"{BOARD_PREFIX}:{key_count}k"


def visible_play_filter():
    return (Play.status.in_(COUNTED_PLAY_STATUSES), Play.eligible.is_(True))


def refresh_chart_best(user_id: int, chart_id: int, rate_key: int) -> None:
    group = (Play.user_id == user_id, Play.chart_id == chart_id, Play.rate_key == rate_key)
    db.session.execute(update(Play).where(*group, Play.is_chart_best.is_(True)).values(is_chart_best=False))
    best_id = db.session.scalar(
        select(Play.id).where(*group, *visible_play_filter()).order_by(Play.accuracy.desc(), Play.max_combo.desc(), Play.id.asc()).limit(1)
    )

    if best_id is not None:
        db.session.execute(update(Play).where(Play.id == best_id).values(is_chart_best=True))


def refresh_profile_best(user_id: int, chart_id: int) -> None:
    group = (Play.user_id == user_id, Play.chart_id == chart_id)
    db.session.execute(update(Play).where(*group, Play.is_profile_best.is_(True)).values(is_profile_best=False))
    best_id = db.session.scalar(
        select(Play.id).where(*group, Play.ranked.is_(True)).order_by(Play.performance.desc(), Play.accuracy.desc(), Play.id.asc()).limit(1)
    )

    if best_id is not None:
        db.session.execute(update(Play).where(Play.id == best_id).values(is_profile_best=True))


def _top_plays(user_id: int, limit: int) -> dict[int, list]:
    best = (Play.user_id == user_id, Play.is_profile_best.is_(True))
    position = func.row_number().over(partition_by=Play.key_count, order_by=(Play.performance.desc(), Play.id.asc())).label("position")
    ranked = select(Play.key_count, Play.performance, Play.accuracy, position).where(*best).subquery()
    per_key: dict[int, list] = defaultdict(list)

    for row in db.session.execute(
        select(ranked.c.key_count, ranked.c.performance, ranked.c.accuracy).where(ranked.c.position <= limit).order_by(ranked.c.key_count, ranked.c.position)
    ):
        per_key[row.key_count].append(row)

    return per_key


def _totals(user_id: int) -> dict[int, Totals]:
    verified = Play.status.in_(COUNTED_PLAY_STATUSES)
    ranked = Play.ranked.is_(True)
    rows = db.session.execute(
        select(
            Play.key_count,
            func.coalesce(func.sum(case((verified, 1), else_=0)), 0),
            func.coalesce(func.sum(case((ranked, 1), else_=0)), 0),
            func.coalesce(func.sum(case((verified, Play.hit_count), else_=0)), 0),
            func.coalesce(func.sum(case((ranked, Play.duration_ms), else_=0)), 0),
            func.coalesce(func.sum(case((verified, Play.duration_ms), else_=0)), 0),
            func.coalesce(func.max(case((verified, Play.max_combo), else_=0)), 0),
            func.max(Play.submitted_at),
        )
        .where(Play.user_id == user_id)
        .group_by(Play.key_count)
    ).all()

    return {row[0]: Totals(int(row[1]), int(row[2]), int(row[3]), int(row[4]), int(row[5]), int(row[6]), row[7]) for row in rows}


def _apply_totals(target: User | UserKeyStats, totals: Totals) -> None:
    target.play_count = totals.play_count
    target.ranked_play_count = totals.ranked_play_count
    target.total_hits = totals.total_hits
    target.ranked_playtime_ms = totals.ranked_playtime_ms
    target.total_playtime_ms = totals.total_playtime_ms
    target.max_combo = totals.max_combo


def refresh_user_stats(user: User) -> None:
    weights = current_weights()
    per_key = _top_plays(user.id, len(weights))
    by_key = _totals(user.id)
    overall = Totals()

    for totals in by_key.values():
        overall.play_count += totals.play_count
        overall.ranked_play_count += totals.ranked_play_count
        overall.total_hits += totals.total_hits
        overall.ranked_playtime_ms += totals.ranked_playtime_ms
        overall.total_playtime_ms += totals.total_playtime_ms
        overall.max_combo = max(overall.max_combo, totals.max_combo)

        if totals.last_played_at is not None and (overall.last_played_at is None or totals.last_played_at > overall.last_played_at):
            overall.last_played_at = totals.last_played_at

    _apply_totals(user, overall)
    user.last_played_at = overall.last_played_at

    existing = {row.key_count: row for row in user.key_stats}

    for key_count in sorted(set(by_key) | set(existing)):
        row = existing.get(key_count)

        if row is None:
            row = UserKeyStats(key_count=key_count)
            user.key_stats.append(row)

        top = per_key.get(key_count, [])
        row.performance = profile_performance([entry.performance for entry in top], weights)
        row.accuracy = profile_accuracy([entry.accuracy for entry in top], weights)
        _apply_totals(row, by_key.get(key_count, Totals()))


def apply_play_change(user: User, chart_id: int, rate_key: int) -> None:
    refresh_chart_best(user.id, chart_id, rate_key)
    refresh_profile_best(user.id, chart_id)
    refresh_user_stats(user)


def _key_stats(user: User, key_count: int) -> UserKeyStats | None:
    return next((row for row in user.key_stats if row.key_count == key_count), None)


def standing(user: User, key_count: int) -> tuple[float, int]:
    row = _key_stats(user, key_count)
    return (row.performance, row.ranked_play_count) if row is not None else (0.0, 0)


def on_leaderboard(user: User, key_count: int) -> bool:
    return not user.is_banned and standing(user, key_count)[1] > 0


def sync_leaderboard(user: User) -> None:
    changes = []

    for key_count in ranked_key_counts():
        score = standing(user, key_count)[0] if on_leaderboard(user, key_count) else None
        changes.append((board_name(key_count), user.id, score))

    try:
        store().leaderboard_apply(changes)
    except redis.RedisError:
        current_app.logger.warning("could not update the leaderboards for user %s", user.id)


def leaderboard_users_query(key_count: int):
    return (
        select(UserKeyStats.user_id.label("user_id"), UserKeyStats.performance.label("performance"))
        .join(User, User.id == UserKeyStats.user_id)
        .where(UserKeyStats.key_count == key_count, UserKeyStats.ranked_play_count > 0, User.is_banned.is_(False))
    )


def rebuild_leaderboard() -> dict[str, int]:
    sizes = {}

    for key_count in ranked_key_counts():
        entries = [(row.user_id, row.performance) for row in db.session.execute(leaderboard_users_query(key_count))]
        store().leaderboard_replace(board_name(key_count), entries)
        sizes[board_name(key_count)] = len(entries)

    return sizes


def global_rank(user: User, key_count: int) -> int | None:
    if not on_leaderboard(user, key_count):
        return None

    try:
        rank = store().leaderboard_rank(board_name(key_count), user.id)

        if rank is not None:
            return rank
    except redis.RedisError:
        current_app.logger.warning("leaderboard unavailable, ranking user %s from the database", user.id)

    return count_rows(leaderboard_users_query(key_count).where(UserKeyStats.performance > standing(user, key_count)[0])) + 1


def ranked_player_count(key_count: int) -> int:
    try:
        return store().leaderboard_count(board_name(key_count))
    except redis.RedisError:
        return count_rows(leaderboard_users_query(key_count))


def leaderboard_page(offset: int, limit: int, key_count: int) -> tuple[list[tuple[int, int, float]], int]:
    try:
        rows = store().leaderboard_page(board_name(key_count), offset, limit)
        total = store().leaderboard_count(board_name(key_count))
        return [(offset + index + 1, user_id, score) for index, (user_id, score) in enumerate(rows)], total
    except redis.RedisError:
        current_app.logger.warning("leaderboard unavailable, paging from the database")

    total = count_rows(leaderboard_users_query(key_count))
    query = leaderboard_users_query(key_count).subquery()
    rows = db.session.execute(
        select(query.c.user_id, query.c.performance).order_by(query.c.performance.desc(), query.c.user_id.asc()).offset(offset).limit(limit)
    ).all()
    return [(offset + index + 1, row.user_id, row.performance) for index, row in enumerate(rows)], total
