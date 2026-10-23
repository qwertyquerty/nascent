import hashlib
from collections.abc import Iterable

import redis
from flask import current_app
from sqlalchemy import Select, func

from .cache import store
from .extensions import db


def count_rows(query: Select) -> int:
    return int(db.session.scalar(_count_query(query)) or 0)


def _count_query(query: Select) -> Select:
    return query.with_only_columns(func.count(), maintain_column_froms=True).order_by(None)


def _count_key(query: Select) -> str:
    compiled = _count_query(query).compile(dialect=db.session.get_bind().dialect)
    text = compiled.string + repr(sorted(compiled.params.items()))
    return hashlib.sha256(text.encode()).hexdigest()[:32]


def cached_count(query: Select, scope: str | None = None) -> int:
    seconds = current_app.config["COUNT_CACHE_SECONDS"]

    if seconds <= 0:
        return count_rows(query)

    key = _count_key(query)

    try:
        cached = store().count_get(key, scope)
    except redis.RedisError:
        cached = None

    if cached is not None:
        return cached

    value = count_rows(query)

    try:
        store().count_put(key, value, seconds, scope)
    except redis.RedisError:
        current_app.logger.warning("could not cache a row count")

    return value


def forget_counts(scopes: Iterable[str]) -> None:
    if current_app.config["COUNT_CACHE_SECONDS"] <= 0:
        return

    try:
        store().count_forget(list(scopes))
    except redis.RedisError:
        current_app.logger.warning("could not clear cached row counts")


def chart_scope(chart_id: int) -> str:
    return f"chart:{chart_id}"


def user_scope(user_id: int) -> str:
    return f"user:{user_id}"
