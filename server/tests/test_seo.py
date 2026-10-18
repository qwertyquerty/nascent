import json
import re

from .helpers import CHART, HASH, chart_hash, make_chart, make_replay, rank_chart, submit

SITE = "https://nascent.test"


def page(api, path: str) -> tuple[str, str, str, list[dict]]:
    html = api.get(path).get_data(as_text=True)
    canonical = re.search(r'<link rel="canonical" href="([^"]+)">', html).group(1)
    robots = re.search(r'<meta name="robots" content="([^"]+)">', html).group(1)
    data = [json.loads(block) for block in re.findall(r'<script type="application/ld\+json">(.*?)</script>', html)]
    return html, canonical, robots, data


def test_pages_have_content_canonicals_and_structured_data(api, clock):
    rank_chart(api)
    play = submit(api, clock, api.token("alice"), make_replay(CHART, seed=1)).get_json()["play"]

    html, canonical, robots, data = page(api, "/")
    assert (canonical, robots.split(",")[0]) == (f"{SITE}/", "index")
    assert '<a href="/users/alice">alice</a>' in html
    assert {"WebSite", "VideoGame"} <= {block["@type"] for block in data}

    html, canonical, _, data = page(api, f"/users/{play['user']['id']}")
    assert canonical == page(api, "/users/ALICE")[1] == f"{SITE}/users/alice"
    assert any(block["@type"] == "ProfilePage" and block["mainEntity"]["name"] == "alice" for block in data)

    html, canonical, _, _ = page(api, f"/charts/{HASH.upper()}")
    assert canonical == f"{SITE}/charts/{HASH}"
    assert f'<a href="/plays/{play["id"]}">' in html
    assert f'<a href="/charts/{HASH}">' in page(api, f"/plays/{play['id']}")[0]


def test_indexing_rules(api):
    for path in ("/users/nobody", f"/charts/{'0' * 64}", "/plays/abc", "/nonsense", "/leaderboard?page=50"):
        assert api.get(path).status_code == 404, path
        assert page(api, path)[2].startswith("noindex"), path

    for path in ("/login", "/account", "/staff", "/charts?q=test"):
        assert page(api, path)[2] == "noindex, follow", path

    assert page(api, "/leaderboard?key_count=7")[1] == f"{SITE}/leaderboard?key_count=7"
    assert page(api, "/leaderboard?key_count=4&sort=accuracy")[1] == f"{SITE}/leaderboard"


def test_untrusted_text_is_escaped(api):
    hostile = make_chart(title='</script><img src=x onerror="alert(1)">')
    rank_chart(api, hostile)
    html = page(api, f"/charts/{chart_hash(hostile)}")[0]

    assert "<img src=x" not in html
    assert "&lt;/script&gt;&lt;img src=x" in html


def test_robots_and_sitemaps(api, clock):
    rank_chart(api)
    submit(api, clock, api.token("alice"), make_replay(CHART, seed=1))
    api.token("never_played")

    def urls(path):
        return re.findall(r"<loc>([^<]+)</loc>", api.get(path).get_data(as_text=True))

    assert f"Sitemap: {SITE}/sitemap.xml" in api.get("/robots.txt").get_data(as_text=True)
    assert urls("/sitemap.xml") == [f"{SITE}/sitemaps/{name}.xml" for name in ("pages", "charts-1", "users-1")]
    assert f"{SITE}/leaderboard?key_count=7" in urls("/sitemaps/pages.xml")
    assert urls("/sitemaps/charts-1.xml") == [f"{SITE}/charts/{HASH}"]
    assert urls("/sitemaps/users-1.xml") == [f"{SITE}/users/alice"]
    assert api.get("/sitemaps/users-2.xml").status_code == 404
