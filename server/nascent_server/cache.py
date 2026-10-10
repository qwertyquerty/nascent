import bisect
import threading
import time
from collections.abc import Iterable

import redis
from flask import Flask, current_app


class RedisStore:
    def __init__(self, url: str, prefix: str):
        self.client = redis.Redis.from_url(url)
        self.prefix = prefix

    def hit(self, key: str, window: int) -> tuple[int, int]:
        now = int(time.time())
        bucket = now // window
        name = f"{self.prefix}rl:{key}:{bucket}"
        pipe = self.client.pipeline()
        pipe.incr(name)
        pipe.expire(name, window + 1)
        count = pipe.execute()[0]
        return int(count), (bucket + 1) * window - now

    def _board(self, board: str) -> str:
        return f"{self.prefix}lb:{board}"

    def leaderboard_apply(self, changes: list[tuple[str, int, float | None]]) -> None:
        pipe = self.client.pipeline(transaction=False)

        for board, member, score in changes:
            if score is None:
                pipe.zrem(self._board(board), str(member))
            else:
                pipe.zadd(self._board(board), {str(member): score})

        pipe.execute()

    def leaderboard_rank(self, board: str, member: int) -> int | None:
        rank = self.client.zrevrank(self._board(board), str(member))
        return None if rank is None else int(rank) + 1

    def leaderboard_page(self, board: str, offset: int, limit: int) -> list[tuple[int, float]]:
        rows = self.client.zrevrange(self._board(board), offset, offset + limit - 1, withscores=True)
        return [(int(member), float(score)) for member, score in rows]

    def leaderboard_count(self, board: str) -> int:
        return int(self.client.zcard(self._board(board)))

    def leaderboard_replace(self, board: str, entries: Iterable[tuple[int, float]]) -> None:
        temporary = self._board(board) + ":rebuild"
        self.client.delete(temporary)
        batch: dict[str, float] = {}

        for member, score in entries:
            batch[str(member)] = score

            if len(batch) >= 5000:
                self.client.zadd(temporary, batch)
                batch = {}

        if batch:
            self.client.zadd(temporary, batch)

        if self.client.exists(temporary):
            self.client.rename(temporary, self._board(board))
        else:
            self.client.delete(self._board(board))

    def ping(self) -> bool:
        try:
            return bool(self.client.ping())
        except redis.RedisError:
            return False


class MemoryStore:
    def __init__(self):
        self.lock = threading.Lock()
        self.counters: dict[str, tuple[int, float]] = {}
        self.boards: dict[str, tuple[dict[int, float], list[tuple[float, int]]]] = {}

    def hit(self, key: str, window: int) -> tuple[int, int]:
        now = time.time()
        bucket = int(now // window)
        name = f"{key}:{bucket}"

        with self.lock:
            count, expires = self.counters.get(name, (0, (bucket + 1) * window))
            self.counters[name] = (count + 1, expires)

            if len(self.counters) > 10000:
                self.counters = {k: v for k, v in self.counters.items() if v[1] > now}

        return count + 1, int((bucket + 1) * window - now)

    def _get(self, board: str):
        return self.boards.setdefault(board, ({}, []))

    def leaderboard_apply(self, changes: list[tuple[str, int, float | None]]) -> None:
        with self.lock:
            for board, member, score in changes:
                scores, ordered = self._get(board)

                if member in scores:
                    ordered.remove((-scores.pop(member), member))

                if score is not None:
                    scores[member] = score
                    bisect.insort(ordered, (-score, member))

    def leaderboard_rank(self, board: str, member: int) -> int | None:
        with self.lock:
            scores, ordered = self._get(board)

            if member not in scores:
                return None

            return bisect.bisect_left(ordered, (-scores[member], member)) + 1

    def leaderboard_page(self, board: str, offset: int, limit: int) -> list[tuple[int, float]]:
        with self.lock:
            _, ordered = self._get(board)
            return [(member, -negative) for negative, member in ordered[offset : offset + limit]]

    def leaderboard_count(self, board: str) -> int:
        with self.lock:
            return len(self._get(board)[0])

    def leaderboard_replace(self, board: str, entries: Iterable[tuple[int, float]]) -> None:
        scores = dict(entries)

        with self.lock:
            self.boards[board] = (scores, sorted((-score, member) for member, score in scores.items()))

    def ping(self) -> bool:
        return True


def init_store(app: Flask) -> None:
    url = app.config.get("REDIS_URL")
    app.extensions["nascent_store"] = RedisStore(url, app.config["REDIS_PREFIX"]) if url else MemoryStore()


def store() -> RedisStore | MemoryStore:
    return current_app.extensions["nascent_store"]
