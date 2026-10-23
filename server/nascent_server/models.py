from datetime import UTC, date, datetime
from enum import StrEnum

from sqlalchemy import (
    DDL,
    JSON,
    BigInteger,
    Boolean,
    Date,
    DateTime,
    Float,
    ForeignKey,
    Index,
    Integer,
    SmallInteger,
    String,
    TypeDecorator,
    UniqueConstraint,
    event,
    false,
    text,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from . import timeutil
from .extensions import db


class UTCDateTime(TypeDecorator):
    impl = DateTime(timezone=True)
    cache_ok = True

    def process_bind_param(self, value, dialect):
        if value is None:
            return None

        if value.tzinfo is None:
            value = value.replace(tzinfo=UTC)

        return value.astimezone(UTC)

    def process_result_value(self, value, dialect):
        if value is None:
            return None

        if value.tzinfo is None:
            return value.replace(tzinfo=UTC)

        return value.astimezone(UTC)


BigId = BigInteger().with_variant(Integer, "sqlite")


def in_dialect(dialect: str):
    def include(item, name, type_, reflected, compare_to):
        return reflected or dialect in item.info.get("dialects", (dialect,))

    return include


class Role(StrEnum):
    USER = "user"
    MODERATOR = "moderator"
    ADMIN = "admin"


ROLE_LEVELS = {Role.USER: 0, Role.MODERATOR: 1, Role.ADMIN: 2}


class ChartStatus(StrEnum):
    UNRANKED = "unranked"
    RANKED = "ranked"


class PlayStatus(StrEnum):
    VERIFIED = "verified"
    PENDING_REVIEW = "pending_review"
    REJECTED = "rejected"


COUNTED_PLAY_STATUSES = (PlayStatus.VERIFIED, PlayStatus.PENDING_REVIEW)


class ReportKind(StrEnum):
    PLAY = "play"
    PLAYER = "player"
    CHART = "chart"


class ReportStatus(StrEnum):
    OPEN = "open"
    RESOLVED = "resolved"
    DISMISSED = "dismissed"


REPORT_REASONS = {
    ReportKind.PLAY: ("cheating", "other"),
    ReportKind.PLAYER: ("cheating", "offensive_name", "harassment", "other"),
    ReportKind.CHART: ("broken_timing", "wrong_audio", "offensive", "other"),
}


class User(db.Model):
    __tablename__ = "users"

    id: Mapped[int] = mapped_column(BigId, primary_key=True)
    username: Mapped[str] = mapped_column(String(32))
    username_key: Mapped[str] = mapped_column(String(32), unique=True)
    email: Mapped[str] = mapped_column(String(254))
    email_key: Mapped[str] = mapped_column(String(254), unique=True)
    password_hash: Mapped[str] = mapped_column(String(255))
    role: Mapped[str] = mapped_column(String(16), default=Role.USER)
    is_banned: Mapped[bool] = mapped_column(Boolean, default=False, server_default=false())
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=timeutil.utcnow)
    email_verified_at: Mapped[datetime | None] = mapped_column(UTCDateTime, nullable=True)

    play_count: Mapped[int] = mapped_column(Integer, default=0)
    ranked_play_count: Mapped[int] = mapped_column(Integer, default=0)
    total_hits: Mapped[int] = mapped_column(BigInteger, default=0)
    ranked_playtime_ms: Mapped[int] = mapped_column(BigInteger, default=0)
    total_playtime_ms: Mapped[int] = mapped_column(BigInteger, default=0)
    max_combo: Mapped[int] = mapped_column(Integer, default=0)
    last_played_at: Mapped[datetime | None] = mapped_column(UTCDateTime, nullable=True)

    key_stats: Mapped[list["UserKeyStats"]] = relationship(back_populates="user", cascade="all, delete-orphan", order_by="UserKeyStats.key_count")

    def has_role(self, role: Role) -> bool:
        return ROLE_LEVELS.get(Role(self.role), 0) >= ROLE_LEVELS[role]


class UserKeyStats(db.Model):
    __tablename__ = "user_key_stats"

    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), primary_key=True)
    key_count: Mapped[int] = mapped_column(SmallInteger, primary_key=True)
    performance: Mapped[float] = mapped_column(Float, default=0.0)
    accuracy: Mapped[float] = mapped_column(Float, default=0.0)
    play_count: Mapped[int] = mapped_column(Integer, default=0)
    ranked_play_count: Mapped[int] = mapped_column(Integer, default=0)
    total_hits: Mapped[int] = mapped_column(BigInteger, default=0)
    ranked_playtime_ms: Mapped[int] = mapped_column(BigInteger, default=0)
    total_playtime_ms: Mapped[int] = mapped_column(BigInteger, default=0)
    max_combo: Mapped[int] = mapped_column(Integer, default=0)

    user: Mapped[User] = relationship(back_populates="key_stats")

    __table_args__ = (Index("ix_user_key_stats_performance", "key_count", "performance", "user_id"),)


class RankSnapshot(db.Model):
    __tablename__ = "rank_snapshots"

    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), primary_key=True)
    key_count: Mapped[int] = mapped_column(SmallInteger, primary_key=True)
    day: Mapped[date] = mapped_column(Date, primary_key=True)
    performance: Mapped[float] = mapped_column(Float)
    rank: Mapped[int | None] = mapped_column(Integer, nullable=True)


class AuthSession(db.Model):
    __tablename__ = "auth_sessions"

    id: Mapped[int] = mapped_column(BigId, primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), index=True)
    access_hash: Mapped[str] = mapped_column(String(64), unique=True)
    access_expires_at: Mapped[datetime] = mapped_column(UTCDateTime)
    refresh_hash: Mapped[str] = mapped_column(String(64), unique=True)
    previous_refresh_hash: Mapped[str | None] = mapped_column(String(64), nullable=True, index=True)
    refresh_expires_at: Mapped[datetime] = mapped_column(UTCDateTime)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=timeutil.utcnow)
    last_used_at: Mapped[datetime] = mapped_column(UTCDateTime, default=timeutil.utcnow)
    client: Mapped[str] = mapped_column(String(200), default="")
    ip: Mapped[str] = mapped_column(String(45), default="")
    revoked_at: Mapped[datetime | None] = mapped_column(UTCDateTime, nullable=True)

    user: Mapped[User] = relationship()


class PasswordReset(db.Model):
    __tablename__ = "password_resets"

    id: Mapped[int] = mapped_column(BigId, primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), index=True)
    token_hash: Mapped[str] = mapped_column(String(64), unique=True)
    password_check: Mapped[str] = mapped_column(String(64))
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=timeutil.utcnow)
    expires_at: Mapped[datetime] = mapped_column(UTCDateTime)
    used_at: Mapped[datetime | None] = mapped_column(UTCDateTime, nullable=True)
    ip: Mapped[str] = mapped_column(String(45), default="")

    user: Mapped[User] = relationship()


class EmailVerification(db.Model):
    __tablename__ = "email_verifications"

    id: Mapped[int] = mapped_column(BigId, primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), index=True)
    token_hash: Mapped[str] = mapped_column(String(64), unique=True)
    email_key: Mapped[str] = mapped_column(String(254))
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=timeutil.utcnow)
    expires_at: Mapped[datetime] = mapped_column(UTCDateTime)
    used_at: Mapped[datetime | None] = mapped_column(UTCDateTime, nullable=True)

    user: Mapped[User] = relationship()


CHART_SORT_COLUMNS = ("title", "artist", "mapper", "difficulty", "duration_ms", "play_count", "key_count")
CHART_SEARCH_COLUMNS = ("title", "title_unicode", "artist", "artist_unicode", "mapper", "difficulty_name")


class Chart(db.Model):
    __tablename__ = "charts"

    id: Mapped[int] = mapped_column(BigId, primary_key=True)
    hash: Mapped[str] = mapped_column(String(64), unique=True)
    status: Mapped[str] = mapped_column(String(16), default=ChartStatus.UNRANKED)
    key_count: Mapped[int] = mapped_column(SmallInteger)
    hit_accuracy: Mapped[int] = mapped_column(SmallInteger)
    damage: Mapped[int] = mapped_column(SmallInteger)
    title: Mapped[str] = mapped_column(String(255))
    title_unicode: Mapped[str] = mapped_column(String(255))
    artist: Mapped[str] = mapped_column(String(255))
    artist_unicode: Mapped[str] = mapped_column(String(255))
    mapper: Mapped[str] = mapped_column(String(255))
    difficulty_name: Mapped[str] = mapped_column(String(255))
    note_count: Mapped[int] = mapped_column(Integer)
    hold_count: Mapped[int] = mapped_column(Integer)
    duration_ms: Mapped[int] = mapped_column(Integer)
    difficulty: Mapped[float] = mapped_column(Float)
    calculator_version: Mapped[str] = mapped_column(String(16))
    data_key: Mapped[str] = mapped_column(String(128))
    play_count: Mapped[int] = mapped_column(Integer, default=0)
    modified: Mapped[bool] = mapped_column(Boolean, default=False, server_default=false())
    downloadable: Mapped[bool] = mapped_column(Boolean, default=False, server_default=false())
    uploaded_by_id: Mapped[int | None] = mapped_column(ForeignKey("users.id", ondelete="SET NULL"), nullable=True)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=timeutil.utcnow)
    ranked_at: Mapped[datetime | None] = mapped_column(UTCDateTime, nullable=True)
    ranked_by_id: Mapped[int | None] = mapped_column(ForeignKey("users.id", ondelete="SET NULL"), nullable=True)

    difficulties: Mapped[list["ChartDifficulty"]] = relationship(back_populates="chart", cascade="all, delete-orphan", order_by="ChartDifficulty.rate_key")
    sources: Mapped[list["ChartSource"]] = relationship(back_populates="chart", cascade="all, delete-orphan", order_by="ChartSource.id")

    __table_args__ = (
        Index("ix_charts_status_difficulty", "status", "difficulty"),
        Index("ix_charts_created_at", "created_at"),
        *(Index(f"ix_charts_{column}_id", column, "id") for column in CHART_SORT_COLUMNS),
        Index(
            "ix_charts_search",
            *CHART_SEARCH_COLUMNS,
            postgresql_using="gin",
            postgresql_ops={column: "gin_trgm_ops" for column in CHART_SEARCH_COLUMNS},
            info={"dialects": ("postgresql",)},
        ).ddl_if(dialect="postgresql"),
    )


event.listen(Chart.__table__, "before_create", DDL("CREATE EXTENSION IF NOT EXISTS pg_trgm").execute_if(dialect="postgresql"))


class RetiredChart(db.Model):
    __tablename__ = "retired_charts"

    hash: Mapped[str] = mapped_column(String(64), primary_key=True)
    replaced_by_id: Mapped[int | None] = mapped_column(ForeignKey("charts.id", ondelete="SET NULL"), nullable=True, index=True)
    retired_at: Mapped[datetime] = mapped_column(UTCDateTime, default=timeutil.utcnow)

    replaced_by: Mapped[Chart | None] = relationship()


class ChartDifficulty(db.Model):
    __tablename__ = "chart_difficulties"

    chart_id: Mapped[int] = mapped_column(ForeignKey("charts.id", ondelete="CASCADE"), primary_key=True)
    rate_key: Mapped[int] = mapped_column(SmallInteger, primary_key=True)
    difficulty: Mapped[float] = mapped_column(Float)
    calculator_version: Mapped[str] = mapped_column(String(16))

    chart: Mapped[Chart] = relationship(back_populates="difficulties")


class ChartSource(db.Model):
    __tablename__ = "chart_sources"

    id: Mapped[int] = mapped_column(BigId, primary_key=True)
    chart_id: Mapped[int] = mapped_column(ForeignKey("charts.id", ondelete="CASCADE"), index=True)
    source: Mapped[str] = mapped_column(String(16))
    checksum: Mapped[str] = mapped_column(String(64))
    source_id: Mapped[int | None] = mapped_column(BigInteger, nullable=True)
    source_set_id: Mapped[int | None] = mapped_column(BigInteger, nullable=True)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=timeutil.utcnow)

    chart: Mapped[Chart] = relationship(back_populates="sources")

    __table_args__ = (UniqueConstraint("source", "checksum", name="uq_chart_sources_source_checksum"),)


class PlayTicket(db.Model):
    __tablename__ = "play_tickets"

    id: Mapped[int] = mapped_column(BigId, primary_key=True)
    token_hash: Mapped[str] = mapped_column(String(64), unique=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"))
    chart_hash: Mapped[str] = mapped_column(String(64))
    rate_key: Mapped[int] = mapped_column(SmallInteger)
    issued_at: Mapped[datetime] = mapped_column(UTCDateTime, default=timeutil.utcnow)
    expires_at: Mapped[datetime] = mapped_column(UTCDateTime)
    used_at: Mapped[datetime | None] = mapped_column(UTCDateTime, nullable=True)
    superseded_at: Mapped[datetime | None] = mapped_column(UTCDateTime, nullable=True)

    __table_args__ = (
        Index("ix_play_tickets_user_issued", "user_id", "issued_at"),
        Index("ix_play_tickets_expires_at", "expires_at"),
    )


class Play(db.Model):
    __tablename__ = "plays"

    id: Mapped[int] = mapped_column(BigId, primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"))
    chart_id: Mapped[int] = mapped_column(ForeignKey("charts.id", ondelete="CASCADE"))
    rate_key: Mapped[int] = mapped_column(SmallInteger)
    key_count: Mapped[int] = mapped_column(SmallInteger)
    status: Mapped[str] = mapped_column(String(16))
    eligible: Mapped[bool] = mapped_column(Boolean)
    ranked: Mapped[bool] = mapped_column(Boolean, default=False)

    accuracy: Mapped[float] = mapped_column(Float)
    grade: Mapped[str] = mapped_column(String(2))
    max_combo: Mapped[int] = mapped_column(Integer)
    count_perfect: Mapped[int] = mapped_column(Integer)
    count_amazing: Mapped[int] = mapped_column(Integer)
    count_great: Mapped[int] = mapped_column(Integer)
    count_good: Mapped[int] = mapped_column(Integer)
    count_bad: Mapped[int] = mapped_column(Integer)
    count_miss: Mapped[int] = mapped_column(Integer)
    average_error: Mapped[float] = mapped_column(Float)
    hit_count: Mapped[int] = mapped_column(Integer)
    duration_ms: Mapped[int] = mapped_column(Integer)

    difficulty: Mapped[float] = mapped_column(Float)
    performance: Mapped[float] = mapped_column(Float)
    calculator_version: Mapped[str] = mapped_column(String(16))

    is_chart_best: Mapped[bool] = mapped_column(Boolean, default=False)
    is_profile_best: Mapped[bool] = mapped_column(Boolean, default=False)

    replay_hash: Mapped[str] = mapped_column(String(64), unique=True)
    replay_key: Mapped[str] = mapped_column(String(128))
    settings: Mapped[dict] = mapped_column(JSON, default=dict)
    flags: Mapped[list] = mapped_column(JSON, default=list)
    fingerprint: Mapped[dict | None] = mapped_column(JSON, nullable=True)
    submitted_at: Mapped[datetime] = mapped_column(UTCDateTime, default=timeutil.utcnow)
    ticket_id: Mapped[int | None] = mapped_column(ForeignKey("play_tickets.id", ondelete="SET NULL"), nullable=True)
    reviewed_by_id: Mapped[int | None] = mapped_column(ForeignKey("users.id", ondelete="SET NULL"), nullable=True)
    reviewed_at: Mapped[datetime | None] = mapped_column(UTCDateTime, nullable=True)
    review_note: Mapped[str] = mapped_column(String(500), default="")

    user: Mapped[User] = relationship(foreign_keys=[user_id])
    chart: Mapped[Chart] = relationship()

    SCORE_COLUMNS = {
        "PERFECT": "count_perfect",
        "AMAZING": "count_amazing",
        "GREAT": "count_great",
        "GOOD": "count_good",
        "BAD": "count_bad",
        "MISS": "count_miss",
    }

    @property
    def score_counts(self) -> dict[str, int]:
        return {name: getattr(self, column) for name, column in self.SCORE_COLUMNS.items()}

    @property
    def rate(self) -> float:
        return self.rate_key / 100


Index(
    "ix_plays_chart_leaderboard",
    Play.chart_id,
    Play.rate_key,
    Play.accuracy.desc(),
    Play.max_combo.desc(),
    Play.id,
    postgresql_where=text("is_chart_best"),
    sqlite_where=text("is_chart_best"),
)
Index(
    "ix_plays_chart_performance",
    Play.chart_id,
    Play.performance.desc(),
    Play.accuracy.desc(),
    Play.id,
    postgresql_where=text("is_profile_best"),
    sqlite_where=text("is_profile_best"),
)
Index("ix_plays_profile_best", Play.user_id, Play.performance.desc(), postgresql_where=text("is_profile_best"), sqlite_where=text("is_profile_best"))
Index(
    "ix_plays_profile_best_keys",
    Play.user_id,
    Play.key_count,
    Play.performance.desc(),
    postgresql_where=text("is_profile_best"),
    sqlite_where=text("is_profile_best"),
)
Index("ix_plays_user_recent", Play.user_id, Play.id)
Index("ix_plays_ranked_performance", Play.performance.desc(), Play.id, postgresql_where=text("ranked"), sqlite_where=text("ranked"))
Index("ix_plays_user_chart", Play.user_id, Play.chart_id, Play.rate_key)
Index("ix_plays_chart_user", Play.chart_id, Play.user_id)
Index("ix_plays_status", Play.status, Play.id)
Index("ix_plays_submitted_at", Play.submitted_at)
Index("ix_plays_user_submitted_at", Play.user_id, Play.submitted_at)


class Report(db.Model):
    __tablename__ = "reports"

    id: Mapped[int] = mapped_column(BigId, primary_key=True)
    kind: Mapped[str] = mapped_column(String(16))
    reason: Mapped[str] = mapped_column(String(32))
    details: Mapped[str] = mapped_column(String(1000), default="")
    status: Mapped[str] = mapped_column(String(16), default=ReportStatus.OPEN)
    reporter_id: Mapped[int | None] = mapped_column(ForeignKey("users.id", ondelete="SET NULL"), nullable=True, index=True)
    user_id: Mapped[int | None] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), nullable=True, index=True)
    play_id: Mapped[int | None] = mapped_column(ForeignKey("plays.id", ondelete="CASCADE"), nullable=True, index=True)
    chart_id: Mapped[int | None] = mapped_column(ForeignKey("charts.id", ondelete="CASCADE"), nullable=True, index=True)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=timeutil.utcnow)
    resolved_by_id: Mapped[int | None] = mapped_column(ForeignKey("users.id", ondelete="SET NULL"), nullable=True)
    resolved_at: Mapped[datetime | None] = mapped_column(UTCDateTime, nullable=True)
    resolution_note: Mapped[str] = mapped_column(String(500), default="")

    reporter: Mapped[User | None] = relationship(foreign_keys=[reporter_id])
    user: Mapped[User | None] = relationship(foreign_keys=[user_id])
    play: Mapped[Play | None] = relationship()
    chart: Mapped[Chart | None] = relationship()
    resolved_by: Mapped[User | None] = relationship(foreign_keys=[resolved_by_id])

    __table_args__ = (Index("ix_reports_status", "status", "id"),)
