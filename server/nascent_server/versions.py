from dataclasses import dataclass

MAX_PART = 2**32 - 1
MAX_LABEL_DIGITS = 19


@dataclass(frozen=True, order=True)
class ReleaseVersion:
    major: int
    minor: int
    patch: int
    released: bool
    label: str
    label_number: int


def _number(text: str) -> int | None:
    if not text or not text.isascii() or not text.isdigit() or (len(text) > 1 and text[0] == "0"):
        return None

    value = int(text)
    return value if value <= MAX_PART else None


def parse_release_version(text: str) -> ReleaseVersion | None:
    parts = text.split(".", 3)

    if len(parts) < 3:
        return None

    numbers = [_number(part) for part in parts[:3]]

    if any(number is None for number in numbers):
        return None

    label = parts[3] if len(parts) == 4 else ""

    if len(parts) == 4 and (not label or not label.isascii() or not label.isalnum()):
        return None

    digits = len(label)

    while digits > 0 and label[digits - 1].isdigit():
        digits -= 1

    trailing = label[digits:]

    if len(trailing) > MAX_LABEL_DIGITS:
        return None

    major, minor, patch = numbers
    return ReleaseVersion(major, minor, patch, not label, label[:digits].lower(), int(trailing) if trailing else 0)
