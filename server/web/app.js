const API = document.querySelector('meta[name="nascent-api"]').content;
const RELEASES = "https://api.github.com/repos/qwertyquerty/nascent/releases/latest";
const RELEASES_PAGE = "https://github.com/qwertyquerty/nascent/releases/latest";
const PAGE_SIZE = 50;
const SEARCH_DELAY_MS = 180;

const query = (params = {}) => new URLSearchParams(Object.entries(params).filter(([, value]) => value != null && value !== "")).toString();
const pageOf = (params) => Math.max(1, parseInt(params.page, 10) || 1);
const paged = (params) => ({limit: PAGE_SIZE, offset: (pageOf(params) - 1) * PAGE_SIZE});

async function api(path, params) {
    const search = query(params);
    const response = await fetch(`${API}${path}${search ? `?${search}` : ""}`);
    const body = await response.json().catch(() => null);

    if (!response.ok) {
        const error = new Error(response.status === 404 ? "That doesn't exist." : body?.error?.message || `The server answered ${response.status}`);
        error.status = response.status;
        throw error;
    }

    return body;
}

async function notFoundAs(promise, message) {
    try {
        return await promise;
    }
    catch (error) {
        throw error.status === 404 ? new Error(message) : error;
    }
}

const link = {
    to: (path, params) => `${path}${query(params) ? `?${query(params)}` : ""}`,
    user: (name, params) => link.to(`/users/${encodeURIComponent(name)}`, params),
    chart: (hash, rate) => link.to(`/charts/${encodeURIComponent(hash)}`, {rate: rate === 1 ? null : rate}),
    chartPerformance: (hash) => link.to(`/charts/${encodeURIComponent(hash)}`, {board: "performance"}),
    play: (id) => `/plays/${id}`,
};

const relative = new Intl.RelativeTimeFormat(undefined, {numeric: "auto"});
const UNIT_SECONDS = [["second", 1, 60], ["minute", 60, 3600], ["hour", 3600, 86400], ["day", 86400, 86400 * 7]];

const format = {
    number: (value) => Number(value ?? 0).toLocaleString(),
    performance: (value) => Number(value ?? 0).toFixed(2),
    accuracy: (value) => `${(Number(value ?? 0) * 100).toFixed(2)}%`,
    rank: (value) => (value ? `#${value.toLocaleString()}` : "—"),
    rate: (value) => `${Number(value).toFixed(2)}x`,
    length: (ms) => `${Math.floor(ms / 60000)}:${String(Math.round(ms / 1000) % 60).padStart(2, "0")}`,
    playtime: (ms) => `${Math.floor(ms / 3600000).toLocaleString()}h ${Math.floor(ms / 60000) % 60}m`,
    error: (ms) => `${ms >= 0 ? "+" : ""}${Number(ms).toFixed(1)} ms`,
    top(rank, players) {
        if (!rank || !players) {
            return "";
        }

        const percent = (rank / players) * 100;
        return `top ${percent < 1 ? Math.max(0.1, Math.ceil(percent * 10) / 10) : Math.ceil(percent)}%`;
    },
    fullDate: (value) => (value ? new Date(value).toLocaleString(undefined, {dateStyle: "medium", timeStyle: "short"}) : ""),
    when(value) {
        if (!value) {
            return "never";
        }

        const seconds = (new Date(value).getTime() - Date.now()) / 1000;

        if (Math.abs(seconds) < 45) {
            return "just now";
        }

        const unit = UNIT_SECONDS.find(([, , limit]) => Math.abs(seconds) < limit);
        return unit ? relative.format(Math.round(seconds / unit[1]), unit[0]) : format.fullDate(value);
    },
};

function chartTitles(chart) {
    const main = `${chart.artist_unicode || chart.artist} - ${chart.title_unicode || chart.title}`;
    const romanized = `${chart.artist} - ${chart.title}`;
    return {main, sub: romanized === main ? null : romanized};
}

function chartName(chart) {
    return `${chart.artist} - ${chart.title} [${chart.difficulty_name}]`;
}

function seconds(length) {
    if (!length) {
        return null;
    }

    const [minutes, rest] = String(length).trim().split(":");
    const total = rest === undefined ? Number(minutes) * 60 : Number(minutes) * 60 + Number(rest);
    return Number.isFinite(total) ? total : null;
}

function parseRoute() {
    const path = location.pathname.replace(/\/+$/, "") || "/";
    const [section = "home", id] = path.split("/").filter(Boolean).map(decodeURIComponent);
    return {path, name: id ? section.replace(/s$/, "") : section, id, params: Object.fromEntries(new URLSearchParams(location.search))};
}

function detectPlatform() {
    const platform = `${navigator.userAgentData?.platform || ""} ${navigator.platform || ""} ${navigator.userAgent}`.toLowerCase();

    if (platform.includes("win")) {
        return {os: "win", label: "Windows"};
    }

    if (platform.includes("mac")) {
        return {os: "mac", label: "macOS"};
    }

    if (platform.includes("linux") || platform.includes("x11")) {
        return {os: "linux", label: "Linux"};
    }

    return null;
}

async function latestRelease() {
    const platform = detectPlatform();

    try {
        const response = await fetch(RELEASES, {headers: {Accept: "application/vnd.github+json"}});

        if (!response.ok) {
            throw new Error(`GitHub answered ${response.status}`);
        }

        const release = await response.json();
        const matching = platform ? release.assets.filter((asset) => asset.name.endsWith(".zip") && asset.name.includes(`-${platform.os}-`)) : [];
        const asset = matching.find((candidate) => candidate.name.includes(platform.os === "mac" ? "arm64" : "x64")) ?? matching[0];
        return {version: release.tag_name, page: release.html_url, url: asset?.browser_download_url ?? null, label: asset ? platform.label : null};
    }
    catch {
        return {version: null, page: RELEASES_PAGE, url: null, label: null};
    }
}

const loaders = {
    async home({params}, keyCounts) {
        const keyCount = params.key_count ?? keyCounts[0];
        const playsKeyCount = params.plays_key_count ?? keyCounts[0];
        const [stats, board, {plays}] = await Promise.all([
            api("/stats"),
            api("/leaderboards/performance", {key_count: keyCount, limit: 10}),
            api("/plays", {sort: "performance", key_count: playsKeyCount, limit: 10}),
        ]);
        return {title: null, stats, board, keyCount, playsKeyCount, topPlays: plays.map((play, index) => ({rank: index + 1, play}))};
    },

    async leaderboard({params}, keyCounts) {
        const keyCount = params.key_count ?? keyCounts[0];
        return {title: `${keyCount}K leaderboard`, keyCount, ...(await api("/leaderboards/performance", {key_count: keyCount, sort: params.sort, ...paged(params)}))};
    },

    async plays({params}, keyCounts) {
        const keyCount = params.key_count ?? keyCounts[0];
        const performance = params.sort === "performance";
        const page = paged(params);
        const {plays, total} = await api("/plays", {sort: params.sort, key_count: keyCount, ...page});
        return {title: "Plays", total, keyCount, performance, plays: performance ? plays.map((play, index) => ({rank: page.offset + index + 1, play})) : plays};
    },

    async play({id}) {
        const path = `/plays/${encodeURIComponent(id)}`;
        const [{play}, timeline] = await Promise.all([notFoundAs(api(path), "This play doesn't exist or isn't public."), api(`${path}/timeline`).catch(() => null)]);
        const title = `${play.user.username} · ${play.grade} ${format.accuracy(play.accuracy)} on ${chartName(play.chart)}`;
        return {title, play, timeline, titles: chartTitles(play.chart)};
    },

    async user({id, params}, keyCounts) {
        const base = `/users/${encodeURIComponent(id)}`;
        const {user} = await notFoundAs(api(base), `No player named "${id}".`);
        const modes = user.key_modes.filter((mode) => keyCounts.includes(mode.key_count) && mode.ranked_play_count > 0);
        const recent = params.tab === "recent" || modes.length === 0;
        const strongest = [...modes].sort((a, b) => b.performance - a.performance)[0];
        const keyCount = Number(params.key_count ?? strongest?.key_count);
        const {plays, total} = await api(`${base}/plays/${recent ? "recent" : "best"}`, {key_count: recent ? null : keyCount, ...paged(params)});
        return {title: user.username, user, modes, strongest, recent, keyCount, plays, total};
    },

    async charts({params}) {
        const result = await api("/charts", {
            q: params.q,
            key_count: params.key_count,
            status: params.status === "all" ? "" : "ranked",
            min_difficulty: params.min_difficulty,
            max_difficulty: params.max_difficulty,
            min_duration: seconds(params.min_length),
            max_duration: seconds(params.max_length),
            sort: params.sort || "play_count",
            order: params.order || "desc",
            ...paged(params),
        });
        const filtered = ["q", "key_count", "status", "min_difficulty", "max_difficulty", "min_length", "max_length", "sort", "order"].some((name) => params[name]);
        return {title: "Charts", filtered, ...result};
    },

    async chart({id, params}) {
        const base = `/charts/${encodeURIComponent(id)}`;
        const performance = params.board === "performance";
        const board = performance ? api(`${base}/leaderboard/performance`, paged(params)) : api(`${base}/leaderboard`, {rate: params.rate, ...paged(params)});
        const [{chart}, entries, {rates}] = await Promise.all([notFoundAs(api(base), "This chart isn't on the server."), board, api(`${base}/rates`).catch(() => ({rates: []}))]);
        const rate = performance ? 1 : entries.rate;
        const players = Object.fromEntries(rates.map((entry) => [entry.rate, entry.players]));
        const difficulty = chart.difficulties.find((entry) => entry.rate === rate)?.difficulty ?? chart.difficulty;
        return {title: chartName(chart), chart, board: entries, performance, rate, players, difficulty, titles: chartTitles(chart)};
    },
};

const app = Vue.createApp({
    data: () => ({route: parseRoute(), view: null, error: null, loading: true, keyCounts: [], menuOpen: false, release: null}),

    async created() {
        if (location.hash.startsWith("#/")) {
            history.replaceState(null, "", location.hash.slice(1));
        }

        addEventListener("popstate", () => this.load());
        document.addEventListener("click", (event) => this.intercept(event));
        latestRelease().then((release) => (this.release = release));
        this.keyCounts = (await api("/info").catch(() => ({ranked_key_counts: [4, 5, 6, 7, 8, 9, 10]}))).ranked_key_counts;
        this.load();
    },

    methods: {
        intercept(event) {
            if (event.defaultPrevented || event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) {
                return;
            }

            const anchor = event.target.closest("a[href]");

            if (!anchor || anchor.target || anchor.hasAttribute("download")) {
                return;
            }

            const url = new URL(anchor.href, location.href);

            if (url.origin !== location.origin || url.pathname.startsWith("/api/") || /\.[a-z0-9]+$/i.test(url.pathname)) {
                return;
            }

            if (url.hash && url.pathname === location.pathname && url.search === location.search) {
                return;
            }

            event.preventDefault();
            this.go(url.pathname + url.search);
        },

        go(href) {
            if (href !== location.pathname + location.search) {
                history.pushState(null, "", href);
            }

            this.load();
        },

        async load() {
            const key = location.pathname + location.search;
            const current = () => key === location.pathname + location.search;
            const route = parseRoute();
            const samePage = this.view !== null && route.path === this.route.path;
            Object.assign(this, {route, error: null, loading: true, menuOpen: false});

            if (!samePage) {
                this.view = null;
                scrollTo(0, 0);
            }

            try {
                const loader = loaders[route.name];

                if (!loader) {
                    throw new Error("That page doesn't exist.");
                }

                const view = await loader(route, this.keyCounts);

                if (current()) {
                    this.view = view;
                    document.title = view.title ? `${view.title} · Nascent` : "Nascent";
                }
            }
            catch (error) {
                if (current()) {
                    this.view = null;
                    this.error = error.message;
                    document.title = "Nascent";
                }
            }
            finally {
                if (current()) {
                    this.loading = false;
                    this.$nextTick(() => document.getElementById("content")?.focus({preventScroll: true}));
                }
            }
        },

        submit(event, path) {
            this.go(link.to(path, Object.fromEntries(new FormData(event.target))));
        },
    },
});

app.config.globalProperties.$f = format;
app.config.globalProperties.$link = link;

app.component("replay-viewer", ReplayViewer);
app.component("hit-error-graph", HitErrorGraph);

app.component("logo-loader", {
    template: `<div class="logo-loader" role="status" aria-label="Loading">
        <img v-for="letter in 7" :key="letter" :src="'/letters/' + letter + '.png'" :style="{animationDelay: (letter - 1) * 0.11 + 's'}" alt="">
    </div>`,
});

app.component("rel-time", {
    props: {value: String},
    template: `<time v-if="value" :datetime="value" :title="$f.fullDate(value)">{{ $f.when(value) }}</time><span v-else>never</span>`,
});

app.component("rank-cell", {
    props: {rank: Number},
    template: `<span :class="rank <= 3 ? 'rank-medal rank-' + rank : null">{{ $f.rank(rank) }}</span>`,
});

app.component("player-search", {
    data: () => ({term: "", results: [], open: false, active: -1}),
    methods: {
        input() {
            clearTimeout(this.timer);
            const term = this.term.trim();
            this.active = -1;

            if (!term) {
                this.results = [];
                this.open = false;
                return;
            }

            this.timer = setTimeout(async () => {
                const {users} = await api("/users", {q: term, limit: 8}).catch(() => ({users: []}));

                if (term === this.term.trim()) {
                    this.results = users;
                    this.open = true;
                }
            }, SEARCH_DELAY_MS);
        },
        move(step) {
            if (!this.results.length) {
                return;
            }

            this.open = true;
            this.active = (this.active + step + this.results.length) % this.results.length;
        },
        choose(user) {
            const term = this.term.trim();
            const exact = this.results.find((result) => result.username.toLowerCase() === term.toLowerCase());
            const target = user ?? exact ?? this.results[0];
            this.$root.go(link.user(target ? target.username : term));
            this.term = "";
            this.results = [];
            this.open = false;
        },
        submit() {
            if (this.term.trim()) {
                this.choose(this.active >= 0 ? this.results[this.active] : null);
            }
        },
        close() {
            setTimeout(() => (this.open = false), 120);
        },
    },
    template: `<form class="player-search" role="search" @submit.prevent="submit">
        <label class="is-sr-only" for="player-search">Find a player</label>
        <input id="player-search" class="input is-small" type="search" placeholder="Find a player" autocomplete="off" v-model="term"
            role="combobox" aria-autocomplete="list" aria-controls="player-search-results" :aria-expanded="open && term.trim() !== ''"
            :aria-activedescendant="active >= 0 ? 'player-result-' + active : null"
            @input="input" @keydown.down.prevent="move(1)" @keydown.up.prevent="move(-1)" @keydown.esc="open = false" @focus="open = results.length > 0" @blur="close">
        <ul v-if="open && term.trim()" id="player-search-results" class="search-results box" role="listbox" aria-label="Matching players">
            <li v-for="(user, index) in results" :id="'player-result-' + index" role="option" :aria-selected="index === active"
                :class="{'is-active': index === active}" @mousedown.prevent="choose(user)">
                <span>{{ user.username }}</span><span class="has-text-grey is-size-7">played <rel-time :value="user.last_played_at"></rel-time></span>
            </li>
            <li v-if="!results.length" class="has-text-grey" role="option" aria-disabled="true">No players match</li>
        </ul>
    </form>`,
});

app.component("download-button", {
    props: {release: Object, size: {type: String, default: ""}},
    data: () => ({fallback: RELEASES_PAGE}),
    template: `<div class="download">
        <a class="button is-primary download-button" :class="size" :href="release?.url || release?.page || fallback">
            {{ release?.label ? 'Download for ' + release.label : 'Download Nascent' }}
        </a>
        <p v-if="release?.version" class="is-size-7 mt-2 has-text-grey">{{ release.version }}</p>
    </div>`,
});

app.component("density-graph", {
    props: {hash: String, rate: {type: Number, default: 1}},
    data: () => ({times: null}),
    computed: {
        bars() {
            if (!this.times?.length) {
                return [];
            }

            const end = this.times[this.times.length - 1] || 1;
            const count = 120;
            const size = end / count;
            const buckets = new Array(count).fill(0);
            this.times.forEach((time) => buckets[Math.min(count - 1, Math.floor(time / size))]++);
            return buckets.map((notes) => (notes / (size / 1000)) * this.rate);
        },
        peak: (self) => Math.max(1, ...self.bars),
    },
    async mounted() {
        const data = await api(`/charts/${encodeURIComponent(this.hash)}/data`).catch(() => null);
        this.times = (data?.hit_objects ?? []).map((hit) => hit.time).sort((a, b) => a - b);
    },
    template: `<figure v-if="bars.length" class="density-graph">
        <svg viewBox="0 0 120 40" preserveAspectRatio="none" role="img" :aria-label="'Note density, peaking at ' + peak.toFixed(1) + ' notes per second'">
            <defs><linearGradient id="density-fill" x1="0" x2="1"><stop offset="0" stop-color="#00b1e1"></stop><stop offset="1" stop-color="#00f59a"></stop></linearGradient></defs>
            <rect v-for="(value, index) in bars" :key="index" :x="index + 0.1" :y="40 - (value / peak) * 38" width="0.8" :height="(value / peak) * 38" fill="url(#density-fill)"></rect>
        </svg>
        <figcaption class="is-size-7 has-text-grey">Note density at {{ $f.rate(rate) }} &middot; peak {{ peak.toFixed(1) }} notes/s</figcaption>
    </figure>`,
});

app.component("key-tabs", {
    props: {current: [Number, String], path: String, params: Object, counts: Array, param: {type: String, default: "key_count"}, label: {type: String, default: "Key mode"}},
    template: `<div class="tabs is-boxed"><ul :aria-label="label">
        <li v-for="count in counts ?? $root.keyCounts" :key="count" :class="{'is-active': count == current}">
            <a :href="$link.to(path, {...params, [param]: count})" :aria-current="count == current ? 'page' : null">{{ count }}K</a>
        </li>
    </ul></div>`,
});

app.component("pager", {
    props: {total: Number, top: Boolean},
    computed: {
        page: (self) => pageOf(self.$root.route.params),
        pages: (self) => Math.ceil(self.total / PAGE_SIZE),
        numbers() {
            const pages = [...new Set([1, this.page - 1, this.page, this.page + 1, this.pages])].filter((page) => page >= 1 && page <= this.pages).sort((a, b) => a - b);
            return pages.flatMap((page, index) => (index > 0 && page - pages[index - 1] > 1 ? [`gap-${page}`, page] : [page]));
        },
    },
    methods: {
        href(page) {
            return page >= 1 && page <= this.pages ? link.to(this.$root.route.path, {...this.$root.route.params, page: page === 1 ? null : page}) : undefined;
        },
    },
    template: `<nav v-if="pages > 1" class="pagination is-centered is-small" :class="top ? 'mb-4' : 'mt-4'" :aria-label="top ? 'Pages' : 'Pages, bottom'">
        <a class="pagination-previous" :class="{'is-disabled': !href(page - 1)}" :href="href(page - 1)" :aria-disabled="!href(page - 1)">Previous</a>
        <a class="pagination-next" :class="{'is-disabled': !href(page + 1)}" :href="href(page + 1)" :aria-disabled="!href(page + 1)">Next</a>
        <ul class="pagination-list">
            <li v-for="item in numbers" :key="item">
                <span v-if="typeof item === 'string'" class="pagination-ellipsis">&hellip;</span>
                <a v-else class="pagination-link" :class="{'is-current': item === page}" :href="href(item)" :aria-label="'Page ' + item" :aria-current="item === page ? 'page' : null">{{ item }}</a>
            </li>
        </ul>
    </nav>`,
});

app.component("player-table", {
    props: {entries: Array, detailed: Boolean, caption: String},
    template: `<p v-if="!entries.length" class="has-text-grey">Nobody is ranked yet.</p>
    <div v-else class="table-container"><table class="table is-fullwidth is-hoverable">
        <caption class="is-sr-only">{{ caption }}</caption>
        <thead><tr>
            <th scope="col" class="is-numeric">Rank</th><th scope="col">Player</th><th scope="col" class="is-numeric">Performance</th>
            <th scope="col" class="is-numeric">Accuracy</th><th v-if="detailed" scope="col" class="is-numeric is-hidden-mobile">Ranked plays</th>
        </tr></thead>
        <tbody><tr v-for="entry in entries" :key="entry.user.id">
            <td class="is-numeric"><rank-cell :rank="entry.rank"></rank-cell></td>
            <td><a :href="$link.user(entry.user.username)">{{ entry.user.username }}</a></td>
            <td class="is-numeric">{{ $f.performance(entry.performance) }}</td>
            <td class="is-numeric">{{ $f.accuracy(entry.accuracy) }}</td>
            <td v-if="detailed" class="is-numeric is-hidden-mobile">{{ $f.number(entry.ranked_play_count) }}</td>
        </tr></tbody>
    </table></div>`,
});

app.component("play-table", {
    props: {plays: Array, showUser: Boolean, showChart: {type: Boolean, default: true}, empty: {type: String, default: "No plays yet."}, caption: String},
    computed: {
        rows: (self) => self.plays.map((entry) => ({...entry, play: entry.play ?? entry})),
        ranked: (self) => self.plays[0]?.rank !== undefined,
        weighted: (self) => self.plays[0]?.weight !== undefined,
    },
    template: `<p v-if="!rows.length" class="has-text-grey">{{ empty }}</p>
    <div v-else class="table-container"><table class="table is-fullwidth is-hoverable play-table">
        <caption class="is-sr-only">{{ caption }}</caption>
        <thead><tr>
            <th v-if="ranked" scope="col" class="is-numeric">Rank</th><th v-if="showUser" scope="col">Player</th><th v-if="showChart" scope="col">Chart</th>
            <th scope="col" class="is-numeric is-hidden-mobile">Rate</th><th scope="col">Grade</th><th scope="col" class="is-numeric">Accuracy</th>
            <th scope="col" class="is-numeric is-hidden-mobile">Combo</th><th scope="col" class="is-numeric">Performance</th>
            <th v-if="weighted" scope="col" class="is-numeric is-hidden-mobile">Weighted</th><th scope="col" class="is-numeric">Played</th>
        </tr></thead>
        <tbody><tr v-for="{play, rank, weight} in rows" :key="play.id">
            <td v-if="ranked" class="is-numeric"><rank-cell :rank="rank"></rank-cell></td>
            <td v-if="showUser"><a :href="$link.user(play.user.username)">{{ play.user.username }}</a></td>
            <td v-if="showChart" class="chart-name">
                <a :href="$link.chart(play.chart.hash, play.rate)">{{ play.chart.artist }} - {{ play.chart.title }}</a> <span class="has-text-grey">[{{ play.chart.difficulty_name }}]</span><span v-if="play.rate !== 1" class="rate-changed is-hidden-tablet"> {{ $f.rate(play.rate) }}</span>
            </td>
            <td class="is-numeric is-hidden-mobile" :class="{'rate-changed': play.rate !== 1}">{{ $f.rate(play.rate) }}</td>
            <td><a :href="$link.play(play.id)" class="grade" :class="'grade-' + play.grade" :aria-label="'Grade ' + play.grade + ', view this play'">{{ play.grade }}</a></td>
            <td class="is-numeric"><a :href="$link.play(play.id)">{{ $f.accuracy(play.accuracy) }}</a></td>
            <td class="is-numeric is-hidden-mobile">{{ $f.number(play.max_combo) }}</td>
            <td class="is-numeric">{{ play.ranked ? $f.performance(play.performance) : "unranked" }}</td>
            <td v-if="weighted" class="is-numeric is-hidden-mobile">{{ Math.round(weight * 100) }}%</td>
            <td class="is-numeric"><rel-time :value="play.submitted_at"></rel-time></td>
        </tr></tbody>
    </table></div>`,
});

app.mount("#app");
