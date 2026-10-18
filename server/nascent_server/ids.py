from werkzeug.routing import IntegerConverter, ValidationError

MAX_ID = 2**63 - 1


def parse_id(value: str | None) -> int | None:
    if not value or not value.isascii() or not value.isdigit() or len(value) > len(str(MAX_ID)):
        return None

    number = int(value)
    return number if number <= MAX_ID else None


class IdConverter(IntegerConverter):
    def to_python(self, value: str) -> int:
        number = parse_id(value)

        if number is None:
            raise ValidationError()

        return number
