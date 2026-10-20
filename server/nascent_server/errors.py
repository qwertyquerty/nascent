from typing import Any

from flask import Flask, jsonify
from pydantic import ValidationError
from werkzeug.exceptions import HTTPException


class ApiError(Exception):
    def __init__(self, status: int, code: str, message: str, details: Any = None, headers: dict[str, str] | None = None):
        super().__init__(message)
        self.status = status
        self.code = code
        self.message = message
        self.details = details
        self.headers = headers or {}


def validation_message(item: dict) -> str:
    reason = item.get("ctx", {}).get("error")
    return str(reason) if isinstance(reason, ValueError) else item["msg"]


def error_response(status: int, code: str, message: str, details: Any = None, headers: dict[str, str] | None = None):
    body: dict[str, Any] = {"code": code, "message": message}

    if details is not None:
        body["details"] = details

    response = jsonify({"error": body})
    response.status_code = status

    for name, value in (headers or {}).items():
        response.headers[name] = value

    return response


def register_error_handlers(app: Flask) -> None:
    @app.errorhandler(ApiError)
    def handle_api_error(error: ApiError):
        return error_response(error.status, error.code, error.message, error.details, error.headers)

    @app.errorhandler(ValidationError)
    def handle_validation_error(error: ValidationError):
        details = [{"field": ".".join(str(part) for part in item["loc"]), "message": validation_message(item)} for item in error.errors()]
        return error_response(400, "validation_error", details[0]["message"] if details else "The request body is invalid", details)

    @app.errorhandler(HTTPException)
    def handle_http_exception(error: HTTPException):
        code = (error.name or "error").lower().replace(" ", "_")
        return error_response(error.code or 500, code, error.description or error.name)

    @app.errorhandler(Exception)
    def handle_unexpected(error: Exception):
        app.logger.exception("unhandled error")
        return error_response(500, "internal_error", "An unexpected error occurred")
