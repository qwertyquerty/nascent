from sqlalchemy import Select, func

from .extensions import db


def count_rows(query: Select) -> int:
    return int(db.session.scalar(query.with_only_columns(func.count(), maintain_column_froms=True).order_by(None)) or 0)
