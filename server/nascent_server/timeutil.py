from datetime import UTC, datetime


def utcnow() -> datetime:
    return datetime.now(UTC)


def isoformat(value: datetime | None) -> str | None:
    if value is None:
        return None

    return value.astimezone(UTC).isoformat().replace("+00:00", "Z")
