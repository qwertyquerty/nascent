import json

import nascent_core as nc

from .helpers import CHART, HASH, make_replay, rank_chart, ticket_for

CHART_LENGTH_MINUTES = nc.Chart.from_json(json.dumps(CHART)).duration_ms_at(1.0) / 60000


def submit_after(api, clock, username, minutes, seed, include_chart=False):
    ticket = ticket_for(api, login(api, username))
    clock.advance(minutes=minutes)
    token = login(api, username)
    body = {"chart_hash": HASH, "replay": make_replay(CHART, seed=seed), "ticket": ticket}

    if include_chart:
        body["chart"] = CHART

    response = api.post("/api/v1/plays", body, token)
    assert response.status_code == 201, response.get_json()
    return response.get_json()["play"]


def login(api, username):
    response = api.post("/api/v1/auth/login", {"login": username, "password": "correct horse battery"})
    return response.get_json()["tokens"]["access_token"]


def ticket_reasons(play):
    return [flag["data"]["reason"] for flag in play["flags"] if flag["code"] == "ticket_rejected"]


def test_tickets_last_one_hour_past_the_chart(api, clock):
    rank_chart(api)
    api.register("alice")

    in_time = submit_after(api, clock, "alice", CHART_LENGTH_MINUTES + 59, seed=1)
    assert in_time["ranked"] is True
    assert ticket_reasons(in_time) == []

    too_late = submit_after(api, clock, "alice", CHART_LENGTH_MINUTES + 61, seed=2)
    assert too_late["ranked"] is False
    assert ticket_reasons(too_late) == ["expired"]


def test_tickets_for_new_charts_expire_with_the_chart_length(api, clock):
    api.register("alice")

    too_late = submit_after(api, clock, "alice", CHART_LENGTH_MINUTES + 61, seed=1, include_chart=True)
    assert ticket_reasons(too_late) == ["expired"]


def test_restarting_many_times_keeps_tickets_coming(api):
    token = api.token("alice")

    for _ in range(200):
        ticket_for(api, token)
