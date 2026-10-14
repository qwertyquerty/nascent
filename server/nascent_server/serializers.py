from datetime import date
from typing import Any

from flask import current_app

from .models import Chart, ChartSource, Play, User, UserKeyStats
from .timeutil import isoformat


def user_summary(user: User) -> dict[str, Any]:
    return {"id": user.id, "username": user.username}


def totals_out(stats: User | UserKeyStats, grade_counts: dict[str, int]) -> dict[str, Any]:
    return {
        "play_count": stats.play_count,
        "ranked_play_count": stats.ranked_play_count,
        "total_hits": stats.total_hits,
        "ranked_playtime_ms": stats.ranked_playtime_ms,
        "total_playtime_ms": stats.total_playtime_ms,
        "max_combo": stats.max_combo,
        "grade_counts": grade_counts,
    }


def key_mode_out(
    stats: UserKeyStats, rank: int | None, ranked_players: int | None, grade_counts: dict[str, int], peak: tuple[int, date] | None = None
) -> dict[str, Any]:
    return {
        "key_count": stats.key_count,
        "global_rank": rank,
        "ranked_players": ranked_players,
        "peak_rank": peak[0] if peak else None,
        "peak_rank_day": peak[1].isoformat() if peak else None,
        "performance": stats.performance,
        "accuracy": stats.accuracy,
        **totals_out(stats, grade_counts),
    }


def user_profile(user: User, stats: dict[str, Any], key_modes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        **user_summary(user),
        "role": user.role,
        "is_restricted": user.is_banned,
        "created_at": isoformat(user.created_at),
        "last_played_at": isoformat(user.last_played_at),
        "stats": stats,
        "key_modes": key_modes,
    }


def private_user(user: User) -> dict[str, Any]:
    return {**user_summary(user), "email": user.email, "role": user.role, "is_restricted": user.is_banned, "created_at": isoformat(user.created_at)}


def chart_summary(chart: Chart, include_sources: bool = False) -> dict[str, Any]:
    body = {
        "hash": chart.hash,
        "status": chart.status,
        "title": chart.title,
        "title_unicode": chart.title_unicode,
        "artist": chart.artist,
        "artist_unicode": chart.artist_unicode,
        "mapper": chart.mapper,
        "difficulty_name": chart.difficulty_name,
        "key_count": chart.key_count,
        "hit_accuracy": chart.hit_accuracy,
        "difficulty": chart.difficulty,
        "duration_ms": chart.duration_ms,
        "note_count": chart.note_count,
        "hold_count": chart.hold_count,
        "play_count": chart.play_count,
    }

    if include_sources:
        body["sources"] = [chart_source_out(source) for source in chart.sources]

    return body


def chart_source_out(source: ChartSource) -> dict[str, Any]:
    body: dict[str, Any] = {"source": source.source, "checksum": source.checksum, "id": source.source_id, "set_id": source.source_set_id, "url": None, "downloads": []}

    if source.source == "osu" and source.source_set_id is not None:
        body["url"] = f"https://osu.ppy.sh/beatmapsets/{source.source_set_id}" + (f"#mania/{source.source_id}" if source.source_id is not None else "")
        body["downloads"] = [{"name": name, "url": template.format(set_id=source.source_set_id)} for name, template in current_app.config["BEATMAP_MIRRORS"]]

    return body


def chart_detail(chart: Chart) -> dict[str, Any]:
    return {
        **chart_summary(chart, include_sources=True),
        "damage": chart.damage,
        "created_at": isoformat(chart.created_at),
        "ranked_at": isoformat(chart.ranked_at),
        "calculator_version": chart.calculator_version,
        "difficulties": [{"rate": entry.rate_key / 100, "difficulty": entry.difficulty} for entry in chart.difficulties],
    }


def play_out(play: Play, include_user: bool = True, include_chart: bool = True, include_flags: bool = False) -> dict[str, Any]:
    body: dict[str, Any] = {
        "id": play.id,
        "rate": play.rate,
        "key_count": play.key_count,
        "status": play.status,
        "ranked": play.ranked,
        "accuracy": play.accuracy,
        "grade": play.grade,
        "max_combo": play.max_combo,
        "score_counts": play.score_counts,
        "average_error": play.average_error,
        "difficulty": play.difficulty,
        "performance": play.performance,
        "duration_ms": play.duration_ms,
        "submitted_at": isoformat(play.submitted_at),
        "is_chart_best": play.is_chart_best,
        "is_profile_best": play.is_profile_best,
    }

    if include_user:
        body["user"] = user_summary(play.user)

    if include_chart:
        body["chart"] = chart_summary(play.chart)

    if include_flags:
        body["flags"] = play.flags
        body["settings"] = play.settings
        body["review_note"] = play.review_note

    return body
