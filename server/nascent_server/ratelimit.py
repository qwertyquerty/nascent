import redis
from flask import current_app

from .cache import store
from .errors import ApiError


def check_rate_limit(name: str, identity: str | int) -> None:
    limit, window = current_app.config["RATE_LIMITS"][name]

    try:
        count, retry_after = store().hit(f"{name}:{identity}", window)
    except redis.RedisError:
        current_app.logger.warning("rate limiter unavailable, allowing %s", name)
        return

    if count > limit:
        raise ApiError(
            429,
            "rate_limited",
            "Too many requests, try again later",
            {"limit": name, "retry_after": retry_after},
            {"Retry-After": str(max(retry_after, 1))},
        )
