from .helpers import make_chart, make_replay, rank_chart, submit

FEW_NOTES = make_chart(note_count=10, title="Few Notes")
SHORT = make_chart(note_count=60, title="Short Song")
LONG_ENOUGH = make_chart(note_count=140, title="Long Enough")


def test_short_or_sparse_plays_are_unranked(api, clock):
    token = api.token("sprinter")

    for chart in (FEW_NOTES, SHORT, LONG_ENOUGH):
        rank_chart(api, chart)

    few = submit(api, clock, token, make_replay(FEW_NOTES, seed=1), chart=FEW_NOTES).get_json()["play"]
    short = submit(api, clock, token, make_replay(SHORT, seed=2), chart=SHORT).get_json()["play"]
    enough = submit(api, clock, token, make_replay(LONG_ENOUGH, seed=3), chart=LONG_ENOUGH).get_json()["play"]
    sped_up = submit(api, clock, token, make_replay(LONG_ENOUGH, rate=1.5, seed=4), chart=LONG_ENOUGH).get_json()["play"]

    assert few["ranked"] is False
    assert short["ranked"] is False
    assert enough["ranked"] is True
    assert sped_up["ranked"] is True

    profile = api.get("/api/v1/users/sprinter").get_json()["user"]
    assert sum(profile["stats"]["grade_counts"].values()) == 1
