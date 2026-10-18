import redis
from flask import current_app

from .cache import fallback_store, store
from .errors import ApiError


def _hit(name: str, identity: str | int) -> tuple[bool, int]:
    limit, window = current_app.config["RATE_LIMITS"][name]
    key = f"{name}:{identity}"

    try:
        count, retry_after = store().hit(key, window)
    except redis.RedisError:
        current_app.logger.warning("rate limiter unavailable, counting %s in this process", name)
        count, retry_after = fallback_store().hit(key, window)

    return count > limit, retry_after


def over_rate_limit(name: str, identity: str | int) -> bool:
    return _hit(name, identity)[0]


def check_rate_limit(name: str, identity: str | int) -> None:
    limited, retry_after = _hit(name, identity)

    if limited:
        raise ApiError(
            429,
            "rate_limited",
            "Too many requests, try again later",
            {"limit": name, "retry_after": retry_after},
            {"Retry-After": str(max(retry_after, 1))},
        )
