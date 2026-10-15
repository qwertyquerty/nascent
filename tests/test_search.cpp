#include "ChartSearch.h"
#include "TestSupport.h"

#include <optional>
#include <string>

using namespace nascent;
using namespace nascent::test;

static ChartInfo search_info() {
    ChartInfo info;
    info.title = "Ether Strike";
    info.title_unicode = "Ether Strike";
    info.artist = "Akira Complex";
    info.artist_unicode = "Akira Complex";
    info.mapper = "Someone";
    info.difficulty_name = "Insane";
    info.key_count = 4;
    return info;
}

static bool matches(const std::string& search, std::optional<double> difficulty = 24.0, std::optional<double> best = std::nullopt, bool favorite = false) {
    ChartInfo info = search_info();
    std::string text = ascii_lowercase(std::string("Ether Strike Akira Complex Someone Insane"));
    SearchSubject subject{info, text, 900, 125.0, difficulty, best, 180.0, favorite};
    return matches_search(parse_search(search), subject);
}

NASCENT_TEST(search_parsing) {
    SearchQuery query = parse_search("Ether diff>=20 len<2:30 KEY=4 mood!=sad played=yes");
    CHECK(query.words.size() == 2);
    CHECK(query.words[0] == "ether");
    CHECK(query.words[1] == "mood!=sad");
    CHECK(query.filters.size() == 4);
    CHECK(query.filters[0].field == "difficulty" && query.filters[0].op == ">=" && query.filters[0].value == "20");
    CHECK(query.filters[1].field == "length" && query.filters[1].op == "<" && query.filters[1].value == "2:30");
    CHECK(query.filters[2].field == "keys" && query.filters[2].op == "=");
    CHECK(query.filters_on("played"));
    CHECK(!query.filters_on("grade"));
    CHECK(parse_search("   ").words.empty());
}

NASCENT_TEST(search_matching) {
    CHECK(matches("ether akira"));
    CHECK(!matches("ether missing"));
    CHECK(matches("keys=4"));
    CHECK(!matches("keys=7"));
    CHECK(matches("notes>800 notes<1000"));
    CHECK(matches("length=2:05"));
    CHECK(matches("len<2:10"));
    CHECK(!matches("len>3:00"));
    CHECK(matches("len>124"));
    CHECK(matches("diff=24.4"));
    CHECK(!matches("diff=25"));
    CHECK(!matches("diff>1", std::nullopt));
    CHECK(matches("artist=akira"));
    CHECK(matches("artist!=camellia"));
    CHECK(!matches("title!=ether"));
    CHECK(matches("diff>nonsense"));
    CHECK(matches("bpm=180"));
    CHECK(matches("bpm>=170 bpm<200"));
    CHECK(!matches("bpm>200"));
    CHECK(!matches("fav=yes"));
    CHECK(matches("fav=yes", 24.0, std::nullopt, true));
    CHECK(matches("favorite!=yes"));
    CHECK(!matches("favourite=no", 24.0, std::nullopt, true));
}

NASCENT_TEST(search_local_bests) {
    CHECK(matches("played=no"));
    CHECK(!matches("played=yes"));
    CHECK(matches("played=yes", 24.0, 0.96));
    CHECK(matches("played!=no", 24.0, 0.96));
    CHECK(matches("grade=a", 24.0, 0.96));
    CHECK(matches("grade>=b", 24.0, 0.96));
    CHECK(!matches("grade>=s", 24.0, 0.96));
    CHECK(!matches("grade=a"));
}
