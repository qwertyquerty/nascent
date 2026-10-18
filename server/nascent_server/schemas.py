import json
import math
import zlib
from typing import Any, Literal, TypeVar

from flask import current_app, request
from pydantic import BaseModel, ConfigDict, EmailStr, Field, field_validator

from .errors import ApiError
from .models import ChartStatus, Role

USERNAME_PATTERN = r"^[A-Za-z0-9_\-]{3,20}$"
HASH_PATTERN = r"^[0-9a-fA-F]{64}$"

Model = TypeVar("Model", bound=BaseModel)


class Body(BaseModel):
    model_config = ConfigDict(extra="forbid", str_strip_whitespace=True)

    @field_validator("*")
    @classmethod
    def no_nul(cls, value: Any) -> Any:
        if isinstance(value, str) and "\x00" in value:
            raise ValueError("must not contain NUL characters")

        return value


class RegisterIn(Body):
    username: str = Field(pattern=USERNAME_PATTERN)
    email: EmailStr
    password: str = Field(min_length=8, max_length=256)

    @field_validator("username")
    @classmethod
    def has_letter(cls, value: str) -> str:
        if not any(character.isalpha() for character in value):
            raise ValueError("username must contain a letter")

        return value


class LoginIn(Body):
    login: str = Field(min_length=1, max_length=254)
    password: str = Field(min_length=1, max_length=256)


class RefreshIn(Body):
    refresh_token: str = Field(min_length=1, max_length=200)


class PasswordResetIn(Body):
    login: str = Field(min_length=1, max_length=254)


class PasswordResetConfirmIn(Body):
    token: str = Field(min_length=1, max_length=200)
    new_password: str = Field(min_length=8, max_length=256)


class PasswordChangeIn(Body):
    current_password: str = Field(min_length=1, max_length=256)
    new_password: str = Field(min_length=8, max_length=256)


class TicketIn(Body):
    chart_hash: str = Field(pattern=HASH_PATTERN)
    rate: float = Field(gt=0, lt=10)


class SubmissionIn(Body):
    chart_hash: str = Field(pattern=HASH_PATTERN)
    replay: dict[str, Any]
    chart: dict[str, Any] | None = None
    ticket: str | None = Field(default=None, max_length=200)


class ChartStatusIn(Body):
    status: ChartStatus


class ReviewIn(Body):
    decision: Literal["approve", "reject"]
    note: str = Field(default="", max_length=500)


class RestrictIn(Body):
    restricted: bool


class RoleIn(Body):
    role: Role


def _too_large() -> ApiError:
    return ApiError(413, "body_too_large", "The request body is too large")


def _gunzip(data: bytes, limit: int) -> bytes:
    decompressor = zlib.decompressobj(16 + zlib.MAX_WBITS)

    try:
        output = decompressor.decompress(data, limit + 1)
    except zlib.error:
        raise ApiError(400, "invalid_body", "The request body could not be decompressed") from None

    if len(output) > limit or decompressor.unconsumed_tail:
        raise ApiError(413, "body_too_large", "The decompressed request body is too large")

    return output


def _json_nodes(data: bytes) -> int:
    return data.count(b",") + data.count(b":") + data.count(b"[") + data.count(b"{")


def json_body(large: bool = False) -> Any:
    config = current_app.config
    limit = config["MAX_DECOMPRESSED_BODY"] if large else config["MAX_SMALL_BODY"]

    if (request.content_length or 0) > limit:
        raise _too_large()

    data = request.get_data(cache=False)

    if len(data) > limit:
        raise _too_large()

    encoding = request.headers.get("Content-Encoding", "identity").lower()

    if encoding == "gzip":
        data = _gunzip(data, limit)
    elif encoding not in ("", "identity"):
        raise ApiError(415, "unsupported_encoding", f"Content-Encoding {encoding} is not supported")

    if large and _json_nodes(data) > config["MAX_JSON_NODES"]:
        raise ApiError(413, "body_too_complex", "The request body has too many values")

    try:
        return json.loads(data)
    except (ValueError, UnicodeDecodeError):
        raise ApiError(400, "invalid_json", "The request body must be JSON") from None
    except RecursionError:
        raise ApiError(400, "invalid_json", "The request body is nested too deeply") from None


def parse_body(model: type[Model], large: bool = False) -> Model:
    body = json_body(large)

    if not isinstance(body, dict):
        raise ApiError(400, "invalid_json", "The request body must be a JSON object")

    return model.model_validate(body)


def query_int(name: str, default: int, minimum: int, maximum: int) -> int:
    raw = request.args.get(name)

    if raw is None:
        return default

    try:
        value = int(raw)
    except ValueError:
        raise ApiError(400, "invalid_query", f"{name} must be an integer") from None

    if not minimum <= value <= maximum:
        raise ApiError(400, "invalid_query", f"{name} must be between {minimum} and {maximum}")

    return value


def query_float(name: str) -> float | None:
    raw = request.args.get(name)

    if raw is None:
        return None

    try:
        value = float(raw)
    except ValueError:
        raise ApiError(400, "invalid_query", f"{name} must be a number") from None

    if not math.isfinite(value):
        raise ApiError(400, "invalid_query", f"{name} must be a finite number")

    return value


def pagination(default_limit: int = 50, max_limit: int = 100, max_offset: int = 100_000) -> tuple[int, int]:
    return query_int("limit", default_limit, 1, max_limit), query_int("offset", 0, 0, max_offset)
