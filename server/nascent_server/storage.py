import gzip
import os
import tempfile
from pathlib import Path

from flask import Flask, Response, current_app, request, send_file


class FileStorage:
    def __init__(self, root: str):
        self.root = Path(root)

    def path(self, key: str) -> Path:
        path = (self.root / key).resolve()

        if self.root.resolve() not in path.parents:
            raise ValueError(f"storage key escapes the storage root: {key}")

        return path

    def put(self, key: str, data: bytes) -> None:
        path = self.path(key)
        path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary = tempfile.mkstemp(dir=path.parent, prefix=".upload-")

        try:
            with os.fdopen(descriptor, "wb") as file:
                file.write(data)

            os.chmod(temporary, 0o644)
            os.replace(temporary, path)
        except BaseException:
            Path(temporary).unlink(missing_ok=True)
            raise

    def get(self, key: str) -> bytes:
        return self.path(key).read_bytes()


def init_storage(app: Flask) -> None:
    app.extensions["nascent_storage"] = FileStorage(app.config["STORAGE_PATH"])


def storage() -> FileStorage:
    return current_app.extensions["nascent_storage"]


def sharded_key(kind: str, digest: str, suffix: str) -> str:
    return f"{kind}/{digest[:2]}/{digest[2:4]}/{digest}{suffix}"


def put_json_gz(key: str, text: str) -> int:
    data = gzip.compress(text.encode(), compresslevel=6, mtime=0)
    storage().put(key, data)
    return len(data)


def get_json_gz(key: str) -> str:
    return gzip.decompress(storage().get(key)).decode()


def json_gz_response(key: str, download_name: str) -> Response:
    if current_app.config["USE_X_ACCEL_REDIRECT"]:
        response = Response(status=200, mimetype="application/json")
        response.headers["X-Accel-Redirect"] = current_app.config["X_ACCEL_PREFIX"] + key.removesuffix(".gz")
    elif "gzip" in request.headers.get("Accept-Encoding", ""):
        response = send_file(storage().path(key), mimetype="application/json", conditional=True, max_age=31536000)
        response.headers["Content-Encoding"] = "gzip"
    else:
        response = Response(gzip.decompress(storage().get(key)), mimetype="application/json")

    response.headers["Content-Disposition"] = f'attachment; filename="{download_name}"'
    response.headers["Cache-Control"] = "public, max-age=31536000, immutable"
    response.headers["Vary"] = "Accept-Encoding"
    return response
