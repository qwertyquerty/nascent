const API = document.querySelector('meta[name="nascent-api"]').content;

for (const [name, value] of Object.entries(PALETTE.colors)) {
    document.documentElement.style.setProperty(`--${name.replaceAll("_", "-")}`, value);
}

function difficultyColor(value) {
    const stops = PALETTE.difficulty_scale;
    const rgb = (hex) => [1, 3, 5].map((offset) => parseInt(hex.slice(offset, offset + 2), 16));
    const number = Number(value ?? 0);

    if (number <= stops[0][0]) {
        return stops[0][1];
    }

    for (let index = 1; index < stops.length; index++) {
        const [high, highColor] = stops[index];

        if (number <= high) {
            const [low, lowColor] = stops[index - 1];
            const t = (number - low) / Math.max(1e-9, high - low);
            const from = rgb(lowColor);
            const to = rgb(highColor);
            return `rgb(${from.map((channel, part) => Math.round(channel + (to[part] - channel) * t)).join(", ")})`;
        }
    }

    return stops[stops.length - 1][1];
}
const RELEASES = "https://api.github.com/repos/qwertyquerty/nascent/releases/latest";
const RELEASES_PAGE = "https://github.com/qwertyquerty/nascent/releases/latest";
const PAGE_SIZE = 50;
const SEARCH_DELAY_MS = 180;
const DAY_MS = 86400000;
const HISTORY_WIDTH = 600;
const HISTORY_HEIGHT = 140;
const HEAT_CELL = 11;
const HEAT_STEP = 13;
const HEAT_LEFT = 30;
const HEAT_TOP = 16;
const HEAT_LEVELS = 4;
const SCATTER_WIDTH = 600;
const SCATTER_HEIGHT = 280;
const SCATTER_PAD = {left: 46, right: 12, top: 12, bottom: 30};
const SCATTER_PLAYS = 100;
const DAILY_HEIGHT = 120;
const GRADE_COLORS = Object.fromEntries([..."XSABCDEF"].map((grade) => [grade, PALETTE.colors[`grade_${grade.toLowerCase()}`]]));
const LEADERBOARD_SORT = {sort: "performance", order: "desc"};
const CHART_SORT = {sort: "play_count", order: "desc"};

const query = (params) => new URLSearchParams(Object.entries(params ?? {}).filter(([, value]) => value != null && value !== "")).toString();
const pageOf = (params) => Math.max(1, parseInt(params.page, 10) || 1);
const paged = (params) => ({limit: PAGE_SIZE, offset: (pageOf(params) - 1) * PAGE_SIZE});

const STAFF_SESSION = "nascent-staff-session";
const STAFF_ROLES = ["moderator", "admin"];

const staffSession = {
    get() {
        try {
            return JSON.parse(sessionStorage.getItem(STAFF_SESSION));
        }
        catch {
            return null;
        }
    },

    set(tokens) {
        try {
            sessionStorage.setItem(STAFF_SESSION, JSON.stringify({access: tokens.access_token, refresh: tokens.refresh_token}));
        }
        catch {
            return;
        }
    },

    clear() {
        try {
            sessionStorage.removeItem(STAFF_SESSION);
        }
        catch {
            return;
        }
    },
};

async function refreshStaffSession(session) {
    const response = await fetch(`${API}/auth/refresh`, {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify({refresh_token: session.refresh})}).catch(() => null);
    const body = response?.ok ? await response.json().catch(() => null) : null;

    if (!body?.tokens) {
        staffSession.clear();
        return;
    }

    staffSession.set(body.tokens);
}

async function api(path, params, options = {}) {
    const search = query(params);
    const send = () => {
        const session = options.anonymous ? null : staffSession.get();
        const headers = {};

        if (options.body !== undefined) {
            headers["Content-Type"] = "application/json";
        }

        if (session) {
            headers.Authorization = `Bearer ${session.access}`;
        }

        return fetch(`${API}${path}${search ? `?${search}` : ""}`, {method: options.method || "GET", headers, body: options.body === undefined ? undefined : JSON.stringify(options.body)});
    };

    let response = await send();
    const session = options.anonymous ? null : staffSession.get();

    if (response.status === 401 && session) {
        await refreshStaffSession(session);
        response = await send();
    }

    const body = await response.json().catch(() => null);

    if (!response.ok) {
        const error = new Error(response.status === 404 ? "That doesn't exist." : body?.error?.message || `The server answered ${response.status}`);
        error.status = response.status;
        throw error;
    }

    return body;
}

const chartDataCache = new Map();

function chartData(hash) {
    if (!chartDataCache.has(hash)) {
        chartDataCache.set(hash, api(`/charts/${encodeURIComponent(hash)}/data`).catch(() => {
            chartDataCache.delete(hash);
            return null;
        }));
    }

    return chartDataCache.get(hash);
}

function uniqueBy(items, key) {
    return [...new Map(items.map((item) => [item[key], item])).values()];
}

const utcDay = (day) => Date.parse(`${day}T00:00:00Z`);
const isoDay = (time) => new Date(time).toISOString().slice(0, 10);

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
    flag(flag) {
        const data = flag.data || {};

        if (Array.isArray(data.metrics)) {
            return data.metrics.map((metric) => `${metric.metric.replaceAll("_", " ")} ${metric.value} (usual ${metric.usual}, z ${metric.z})`).join(" · ");
        }

        return Object.entries(data)
            .filter(([, value]) => value !== "" && value != null && !(Array.isArray(value) && !value.length))
            .map(([key, value]) => `${key.replaceAll("_", " ")} ${Array.isArray(value) ? value.join(", ") : value}`)
            .join(" · ");
    },
    number: (value) => Number(value ?? 0).toLocaleString(),
    performance: (value) => Number(value ?? 0).toFixed(2),
    accuracy: (value) => `${(Number(value ?? 0) * 100).toFixed(2)}%`,
    rank: (value) => (value ? `#${value.toLocaleString()}` : "—"),
    rate: (value) => `${Number(value).toFixed(2)}x`,
    length: (ms) => `${Math.floor(ms / 60000)}:${String(Math.round(ms / 1000) % 60).padStart(2, "0")}`,
    playtime: (ms) => `${Math.floor(ms / 3600000).toLocaleString()}h ${Math.floor(ms / 60000) % 60}m`,
    error: (ms) => `${ms >= 0 ? "+" : ""}${Number(ms).toFixed(1)} ms`,
    difficultyColor,
    standing(rank, players) {
        if (!rank || !players) {
            return "";
        }

        const rounded = (percent) => (percent < 1 ? Math.max(0.1, Math.ceil(percent * 10) / 10) : Math.ceil(percent));
        const top = (rank / players) * 100;
        return top <= 50 ? `top ${rounded(top)}%` : `bottom ${rounded(((players - rank + 1) / players) * 100)}%`;
    },
    day: (value) => new Date(typeof value === "number" ? value : utcDay(value)).toLocaleDateString(undefined, {dateStyle: "medium", timeZone: "UTC"}),
    plural: (count, word) => `${Number(count ?? 0).toLocaleString()} ${word}${count === 1 ? "" : "s"}`,
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

function timestamp(value) {
    const text = String(value ?? "").trim().toLowerCase();
    const clock = text.match(/^(?:(\d+):)?(\d+)$/);
    const units = text.match(/^(?:(\d+)m)?(?:(\d+)s)?$/);

    if (clock) {
        return Number(clock[1] ?? 0) * 60 + Number(clock[2]);
    }

    return text && units ? Number(units[1] ?? 0) * 60 + Number(units[2] ?? 0) : null;
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
        const [board, top] = await Promise.all([
            api("/leaderboards/performance", {key_count: keyCount, sort: params.sort, ...paged(params)}),
            api("/leaderboards/performance", {key_count: keyCount, limit: 3}),
        ]);
        return {title: `${keyCount}K leaderboard`, keyCount, podium: top.entries, ...board};
    },

    async plays({params}, keyCounts) {
        const keyCount = params.key_count ?? keyCounts[0];
        const performance = params.sort === "performance";
        const page = paged(params);
        const {plays, total} = await api("/plays", {sort: params.sort, key_count: keyCount, ...page});
        return {title: "Plays", total, keyCount, performance, plays: performance ? plays.map((play, index) => ({rank: page.offset + index + 1, play})) : plays};
    },

    async play({id, params}) {
        const path = `/plays/${encodeURIComponent(id)}`;
        const [{play, leaderboard}, timeline] = await Promise.all([notFoundAs(api(path), "This play doesn't exist or isn't public."), api(`${path}/timeline`).catch(() => null)]);
        const title = `${play.user.username} · ${play.grade} ${format.accuracy(play.accuracy)} on ${chartName(play.chart)}`;
        return {title, play, leaderboard, timeline, titles: chartTitles(play.chart), startAt: timestamp(params.t)};
    },

    async user({id, params}, keyCounts) {
        const base = `/users/${encodeURIComponent(id)}`;
        const {user} = await notFoundAs(api(base), `No player named "${id}".`);
        const modes = user.key_modes.filter((mode) => keyCounts.includes(mode.key_count) && mode.play_count > 0);
        const strongest = modes.filter((mode) => mode.ranked_play_count > 0).sort((a, b) => b.performance - a.performance)[0];
        const busiest = [...modes].sort((a, b) => b.play_count - a.play_count)[0];
        const keyCount = Number(params.key_count ?? (strongest ?? busiest)?.key_count);
        const mode = modes.find((entry) => entry.key_count === keyCount);
        const filtered = keyCounts.includes(keyCount);
        const recent = params.tab === "recent" || !filtered || (params.tab !== "best" && !mode?.ranked_play_count);
        const [{plays, total}, history, activity] = await Promise.all([
            api(`${base}/plays/${recent ? "recent" : "best"}`, {key_count: filtered ? keyCount : null, ...paged(params)}),
            filtered ? api(`${base}/history`, {key_count: keyCount}).catch(() => null) : null,
            api(`${base}/activity`, {key_count: filtered ? keyCount : null}).catch(() => null),
        ]);
        return {title: user.username, user, modes, strongest, recent, keyCount, plays, total, history, activity};
    },

    async stats() {
        const [stats, activity] = await Promise.all([api("/stats"), api("/stats/activity")]);
        const keyPeak = Math.max(1, ...activity.key_modes.map((mode) => mode.plays));
        const gradeTotal = Object.values(activity.grades).reduce((sum, count) => sum + count, 0);
        return {title: "Stats", stats, activity, keyPeak, gradeTotal};
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

    async staff({params}) {
        const status = params.status === "rejected" ? "rejected" : "pending_review";

        if (!staffSession.get()) {
            return {title: "Staff", signedIn: false};
        }

        try {
            const [{user}, {plays}] = await Promise.all([api("/me"), api("/moderation/plays", {status, limit: 100})]);
            return {title: "Review queue", signedIn: true, user, status, plays};
        }
        catch (error) {
            if (error.status === 401 || error.status === 403) {
                staffSession.clear();
                return {title: "Staff", signedIn: false, notice: error.status === 403 ? "That account can't review plays." : "Your session ended, sign in again."};
            }

            throw error;
        }
    },

    async chart({id, params}) {
        const base = `/charts/${encodeURIComponent(id)}`;
        const performance = params.board === "performance";
        const board = performance ? api(`${base}/leaderboard/performance`, paged(params)) : api(`${base}/leaderboard`, {rate: params.rate, ...paged(params)});
        const [{chart}, entries, {rates}] = await Promise.all([notFoundAs(api(base), "This chart isn't on the server."), board, api(`${base}/rates`).catch(() => ({rates: []}))]);
        const rate = performance ? 1 : entries.rate;
        const players = Object.fromEntries(rates.map((entry) => [entry.rate, entry.players]));
        const difficulty = chart.difficulties.find((entry) => entry.rate === rate)?.difficulty ?? chart.difficulty;
        const https = (entry) => entry.url?.startsWith("https://");
        const downloads = uniqueBy(chart.sources.flatMap((source) => source.downloads ?? []).filter(https), "url");
        const pages = uniqueBy(chart.sources.map((source) => ({name: source.source, url: source.url})).filter(https), "url");
        return {title: chartName(chart), chart, board: entries, performance, rate, players, difficulty, downloads, pages, titles: chartTitles(chart)};
    },
};

const app = Vue.createApp({
    data: () => ({route: parseRoute(), view: null, error: null, loading: true, keyCounts: [], menuOpen: false, release: null, staffUser: null, staffError: null, staffBusy: false, reviewNotes: {}, reviewBusy: null}),

    async created() {
        if (location.hash.startsWith("#/")) {
            history.replaceState(null, "", location.hash.slice(1));
        }

        addEventListener("popstate", () => this.load());
        document.addEventListener("click", (event) => this.intercept(event));
        latestRelease().then((release) => (this.release = release));

        if (staffSession.get()) {
            api("/me").then(({user}) => (this.staffUser = STAFF_ROLES.includes(user.role) ? user : null)).catch(() => null);
        }
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

        async staffLogin(event) {
            const form = new FormData(event.target);
            this.staffError = null;
            this.staffBusy = true;

            try {
                const {user, tokens} = await api("/auth/login", null, {method: "POST", body: {login: form.get("login"), password: form.get("password")}, anonymous: true});

                if (!STAFF_ROLES.includes(user.role)) {
                    await fetch(`${API}/auth/logout`, {method: "POST", headers: {Authorization: `Bearer ${tokens.access_token}`}}).catch(() => null);
                    throw new Error("That account can't review plays.");
                }

                staffSession.set(tokens);
                this.staffUser = user;
                this.load();
            }
            catch (error) {
                this.staffError = error.message;
            }
            finally {
                this.staffBusy = false;
            }
        },

        async staffLogout() {
            await api("/auth/logout", null, {method: "POST"}).catch(() => null);
            staffSession.clear();
            this.staffUser = null;
            this.go("/staff");
        },

        async review(play, decision) {
            this.reviewBusy = play.id;
            this.staffError = null;

            try {
                await api(`/moderation/plays/${play.id}/review`, null, {method: "POST", body: {decision, note: this.reviewNotes[play.id] || ""}});

                if (this.route.name === "staff") {
                    this.view.plays = this.view.plays.filter((entry) => entry.id !== play.id);
                }
                else {
                    this.load();
                }
            }
            catch (error) {
                this.staffError = error.message;
            }
            finally {
                this.reviewBusy = null;
            }
        },

        submit(event, path) {
            this.go(link.to(path, Object.fromEntries(new FormData(event.target))));
        },

        sortState(defaults) {
            return {sort: this.route.params.sort || defaults.sort, order: this.route.params.order || defaults.order};
        },

        sortHref(field, defaults, first = "desc", toggle = true) {
            const {sort, order} = this.sortState(defaults);
            const next = sort === field && toggle ? (order === "asc" ? "desc" : "asc") : first;
            return link.to(this.route.path, {...this.route.params, page: null, sort: field === defaults.sort ? null : field, order: next === defaults.order ? null : next});
        },

        ariaSort(field, defaults) {
            const {sort, order} = this.sortState(defaults);
            return sort === field ? (order === "asc" ? "ascending" : "descending") : null;
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
    template: `<span :class="rank <= 3 ? 'rank-medal rank-' + rank : null" :title="rank <= 3 ? ['', 'First place', 'Second place', 'Third place'][rank] : null">{{ $f.rank(rank) }}</span>`,
});

app.component("difficulty-value", {
    props: {value: Number},
    template: `<span class="difficulty-number" :style="{color: $f.difficultyColor(value)}">{{ $f.performance(value) }}</span>`,
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
        const data = await chartData(this.hash);
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

app.component("chart-preview", {
    props: {hash: String, rate: {type: Number, default: 1}},
    data: () => ({open: false, data: null, failed: false}),
    computed: {
        timeline: (self) => (self.data ? previewTimeline(self.data, self.rate) : null),
    },
    methods: {
        async toggle() {
            this.open = !this.open;

            if (this.open && !this.data) {
                this.data = await chartData(this.hash);
                this.failed = !this.data;
            }
        },
    },
    template: `<div class="chart-preview">
        <replay-viewer v-if="open && timeline" :timeline="timeline" preview></replay-viewer>
        <density-graph v-else :hash="hash" :rate="rate"></density-graph>
        <p v-if="open && failed" class="has-text-grey mt-2">The chart couldn't be loaded for a preview.</p>
        <button type="button" class="button is-small mt-3" :aria-expanded="open" @click="toggle">{{ open ? "Hide preview" : "Preview chart" }}</button>
    </div>`,
});

app.component("history-graph", {
    props: {history: Object},
    data: () => ({metric: "performance", hover: null}),
    computed: {
        points() {
            return (this.history?.[this.metric] ?? []).map(([day, value]) => ({time: utcDay(day), value}));
        },
        span() {
            const today = utcDay(isoDay(Date.now()));
            const start = this.points[0]?.time ?? today;
            return {start, end: Math.max(today, start + DAY_MS)};
        },
        range() {
            const values = this.points.map((point) => point.value);
            let low = Math.min(...values);
            let high = Math.max(...values);

            if (high - low < 1e-6) {
                low -= this.metric === "rank" ? 1 : Math.max(1, low * 0.05);
                high += this.metric === "rank" ? 1 : Math.max(1, high * 0.05);
            }

            const padding = (high - low) * 0.1;
            return {low: low - padding, high: high + padding};
        },
        line() {
            return this.points.map((point, index) => (index === 0 ? `M${this.x(point.time)},${this.y(point.value)}` : `H${this.x(point.time)} V${this.y(point.value)}`)).join(" ") + (this.points.length ? ` H${HISTORY_WIDTH}` : "");
        },
        area() {
            return this.points.length ? `${this.line} V${HISTORY_HEIGHT} H${this.x(this.points[0].time)} Z` : "";
        },
        shown: (self) => self.hover ?? self.points[self.points.length - 1] ?? null,
    },
    methods: {
        x(time) {
            return ((time - this.span.start) / (this.span.end - this.span.start)) * HISTORY_WIDTH;
        },
        y(value) {
            const fraction = (value - this.range.low) / (this.range.high - this.range.low);
            return this.metric === "rank" ? fraction * HISTORY_HEIGHT : HISTORY_HEIGHT - fraction * HISTORY_HEIGHT;
        },
        value(point) {
            return this.metric === "rank" ? format.rank(point.value) : format.performance(point.value);
        },
        track(event) {
            const bounds = event.currentTarget.getBoundingClientRect();
            const time = this.span.start + ((event.clientX - bounds.left) / Math.max(1, bounds.width)) * (this.span.end - this.span.start);
            this.hover = [...this.points].reverse().find((point) => point.time <= time) ?? this.points[0] ?? null;
        },
    },
    template: `<div class="history-graph">
        <div class="history-header">
            <div class="buttons has-addons mb-0" role="group" aria-label="History metric">
                <button v-for="[metric, label] in [['performance', 'Performance'], ['rank', 'Rank']]" type="button" class="button is-small mb-0"
                    :class="{'is-link is-selected': metric === $data.metric}" :aria-pressed="metric === $data.metric" @click="$data.metric = metric; hover = null">{{ label }}</button>
            </div>
            <p v-if="shown" class="history-value"><span class="stat-value">{{ value(shown) }}</span> <span class="has-text-grey is-size-7">{{ hover ? $f.day(shown.time) : 'now' }}</span></p>
        </div>
        <template v-if="points.length">
            <svg :viewBox="'0 0 ' + ${HISTORY_WIDTH} + ' ' + ${HISTORY_HEIGHT}" preserveAspectRatio="none" role="img"
                :aria-label="(metric === 'rank' ? 'Rank' : 'Performance') + ' over time, now ' + value(points[points.length - 1])" @pointermove="track" @pointerleave="hover = null">
                <defs><linearGradient id="history-fill" x1="0" x2="0" y1="0" y2="1">
                    <stop offset="0" stop-color="#00b1e1" stop-opacity="0.35"></stop><stop offset="1" stop-color="#00b1e1" stop-opacity="0"></stop>
                </linearGradient></defs>
                <path :d="area" fill="url(#history-fill)"></path>
                <path :d="line" class="history-line"></path>
                <line v-if="hover" :x1="x(hover.time)" :x2="x(hover.time)" y1="0" :y2="${HISTORY_HEIGHT}" class="history-cursor"></line>
            </svg>
            <div class="history-axis is-size-7 has-text-grey"><span>{{ $f.day(span.start) }}</span><span>Today</span></div>
        </template>
        <p v-else class="has-text-grey history-empty">{{ metric === 'rank' ? 'Rank history is recorded once a day, so it will show up soon.' : 'No ranked plays in this mode yet.' }}</p>
    </div>`,
});

app.component("sort-link", {
    props: {field: String, label: String, defaults: Object, first: {type: String, default: "desc"}, toggle: {type: Boolean, default: true}},
    computed: {
        state: (self) => self.$root.ariaSort(self.field, self.defaults),
    },
    template: `<a class="sort-link" :class="{'is-sorted': state}" :href="$root.sortHref(field, defaults, first, toggle)">{{ label }}<span class="sort-arrow" aria-hidden="true">{{ state === 'ascending' ? '▲' : state === 'descending' ? '▼' : '' }}</span></a>`,
});

app.component("daily-bars", {
    props: {days: Array},
    data: () => ({hover: null}),
    computed: {
        peak: (self) => Math.max(1, ...self.days.map(([, count]) => count)),
        width: (self) => self.days.length * 10,
        total: (self) => self.days.reduce((sum, [, count]) => sum + count, 0),
        shown: (self) => self.hover ?? null,
    },
    template: `<figure class="daily-bars">
        <svg :viewBox="'0 0 ' + width + ' ${DAILY_HEIGHT}'" preserveAspectRatio="none" role="img" :aria-label="$f.plural(total, 'play') + ' over the last ' + days.length + ' days'" @pointerleave="hover = null">
            <defs><linearGradient id="daily-fill" x1="0" x2="1"><stop offset="0" stop-color="#00b1e1"></stop><stop offset="1" stop-color="#00f59a"></stop></linearGradient></defs>
            <rect v-for="([day, count], index) in days" :key="day" :x="index * 10 + 1" :y="${DAILY_HEIGHT} - Math.max(count ? 2 : 0, (count / peak) * ${DAILY_HEIGHT - 4})" width="8"
                :height="Math.max(count ? 2 : 0, (count / peak) * ${DAILY_HEIGHT - 4})" :fill="hover && hover[0] === day ? '#ffffff' : 'url(#daily-fill)'"></rect>
            <rect v-for="([day, count], index) in days" :key="'hit-' + day" :x="index * 10" y="0" width="10" height="${DAILY_HEIGHT}" fill="transparent" @pointerenter="hover = [day, count]">
                <title>{{ $f.plural(count, 'play') }} on {{ $f.day(day) }}</title>
            </rect>
        </svg>
        <figcaption class="daily-caption is-size-7 has-text-grey">
            <span>{{ $f.day(days[0][0]) }}</span>
            <span v-if="shown">{{ $f.plural(shown[1], 'play') }} on {{ $f.day(shown[0]) }}</span>
            <span>Today</span>
        </figcaption>
    </figure>`,
});

app.component("skill-scatter", {
    props: {username: String, keyCount: Number},
    data: () => ({open: false, plays: null, failed: false, hover: null}),
    computed: {
        bounds() {
            const difficulties = this.plays.map((play) => play.difficulty);
            const accuracies = this.plays.map((play) => play.accuracy);
            const low = Math.min(...difficulties);
            const high = Math.max(...difficulties);
            const spread = Math.max(0.5, high - low);
            const floor = Math.min(...accuracies);
            return {low: low - spread * 0.05, high: high + spread * 0.05, floor: Math.max(0, floor - Math.max(0.005, (1 - floor) * 0.08)), ceiling: 1};
        },
        points() {
            return this.plays.map((play) => ({play, x: this.x(play.difficulty), y: this.y(play.accuracy), color: GRADE_COLORS[play.grade] ?? GRADE_COLORS.F}));
        },
        xTicks() {
            const {low, high} = this.bounds;
            const step = this.niceStep((high - low) / 5);
            const ticks = [];

            for (let value = Math.ceil(low / step) * step; value <= high; value += step) {
                ticks.push({value, label: value.toFixed(step < 1 ? 1 : 0), position: this.x(value)});
            }

            return ticks;
        },
        yTicks() {
            const {floor, ceiling} = this.bounds;
            const step = this.niceStep((ceiling - floor) / 4);
            const ticks = [];

            for (let value = Math.ceil(floor / step) * step; value <= ceiling + 1e-9; value += step) {
                ticks.push({value, label: `${(value * 100).toFixed(step < 0.01 ? 1 : 0)}%`, position: this.y(value)});
            }

            return ticks;
        },
        hardest() {
            const strong = this.plays.filter((play) => play.grade === "X" || play.grade === "S");
            return strong.length ? strong.reduce((best, play) => (play.difficulty > best.difficulty ? play : best)) : null;
        },
        average: (self) => self.plays.reduce((sum, play) => sum + play.accuracy, 0) / Math.max(1, self.plays.length),
    },
    methods: {
        async toggle() {
            this.open = !this.open;

            if (this.open && this.plays === null) {
                try {
                    const {plays} = await api(`/users/${encodeURIComponent(this.username)}/plays/best`, {key_count: this.keyCount, limit: SCATTER_PLAYS});
                    this.plays = plays.map((entry) => entry.play);
                }
                catch {
                    this.failed = true;
                }
            }
        },
        niceStep(raw) {
            const magnitude = 10 ** Math.floor(Math.log10(Math.max(raw, 1e-9)));
            return [1, 2, 2.5, 5, 10].map((factor) => factor * magnitude).find((step) => step >= raw) ?? magnitude * 10;
        },
        x(difficulty) {
            const {low, high} = this.bounds;
            return SCATTER_PAD.left + ((difficulty - low) / Math.max(1e-9, high - low)) * (SCATTER_WIDTH - SCATTER_PAD.left - SCATTER_PAD.right);
        },
        y(accuracy) {
            const {floor, ceiling} = this.bounds;
            return SCATTER_PAD.top + (1 - (accuracy - floor) / Math.max(1e-9, ceiling - floor)) * (SCATTER_HEIGHT - SCATTER_PAD.top - SCATTER_PAD.bottom);
        },
        visit(play) {
            this.$root.go(link.play(play.id));
        },
    },
    template: `<div class="skill-scatter">
        <button type="button" class="button is-small" :aria-expanded="open" @click="toggle">{{ open ? 'Hide' : 'Show' }} {{ keyCount }}K skill breakdown</button>
        <div v-if="open" class="skill-body mt-4">
            <p v-if="failed" class="has-text-grey">The best plays couldn't be loaded.</p>
            <logo-loader v-else-if="plays === null"></logo-loader>
            <p v-else-if="!plays.length" class="has-text-grey">No ranked {{ keyCount }}K plays yet.</p>
            <template v-else>
                <p class="skill-summary is-size-7 has-text-grey">
                    Accuracy against difficulty for the top {{ plays.length }} {{ keyCount }}K plays &middot; average {{ $f.accuracy(average) }}
                    <template v-if="hardest"> &middot; hardest S: <difficulty-value :value="hardest.difficulty"></difficulty-value></template>
                </p>
                <svg :viewBox="'0 0 ${SCATTER_WIDTH} ${SCATTER_HEIGHT}'" role="img" :aria-label="'Accuracy against difficulty for ' + plays.length + ' best plays'">
                    <line v-for="tick in yTicks" :key="'y' + tick.value" :x1="${SCATTER_PAD.left}" :x2="${SCATTER_WIDTH - SCATTER_PAD.right}" :y1="tick.position" :y2="tick.position" class="scatter-grid"></line>
                    <line v-for="tick in xTicks" :key="'x' + tick.value" :x1="tick.position" :x2="tick.position" :y1="${SCATTER_PAD.top}" :y2="${SCATTER_HEIGHT - SCATTER_PAD.bottom}" class="scatter-grid"></line>
                    <text v-for="tick in yTicks" :key="'yl' + tick.value" :x="${SCATTER_PAD.left - 6}" :y="tick.position + 4" class="scatter-label" text-anchor="end">{{ tick.label }}</text>
                    <text v-for="tick in xTicks" :key="'xl' + tick.value" :x="tick.position" :y="${SCATTER_HEIGHT - 10}" class="scatter-label" text-anchor="middle">{{ tick.label }}</text>
                    <circle v-for="point in points" :key="point.play.id" :cx="point.x" :cy="point.y" :r="hover === point ? 6 : 4" :fill="point.color"
                        class="scatter-point" tabindex="0" role="link" :aria-label="point.play.chart.title + ', ' + $f.accuracy(point.play.accuracy)"
                        @pointerenter="hover = point" @pointerleave="hover = null" @focus="hover = point" @blur="hover = null" @click="visit(point.play)" @keydown.enter="visit(point.play)"></circle>
                </svg>
                <p class="skill-detail is-size-7">
                    <template v-if="hover">
                        <span class="grade" :class="'grade-' + hover.play.grade">{{ hover.play.grade }}</span>
                        {{ hover.play.chart.artist }} - {{ hover.play.chart.title }} [{{ hover.play.chart.difficulty_name }}] &middot; {{ $f.rate(hover.play.rate) }}
                        &middot; {{ $f.performance(hover.play.difficulty) }} difficulty &middot; {{ $f.accuracy(hover.play.accuracy) }} &middot; {{ $f.performance(hover.play.performance) }} performance
                    </template>
                    <span v-else class="has-text-grey">Difficulty across, accuracy up. Point at a dot for details, click to open the play.</span>
                </p>
            </template>
        </div>
    </div>`,
});

app.component("activity-heatmap", {
    props: {activity: Object},
    computed: {
        counts: (self) => new Map(self.activity.days),
        cells() {
            const first = utcDay(this.activity.first_day);
            const last = utcDay(this.activity.last_day);
            const start = first - new Date(first).getUTCDay() * DAY_MS;
            const cells = [];

            for (let time = start; time <= last; time += DAY_MS) {
                const index = Math.round((time - start) / DAY_MS);

                if (time >= first) {
                    const day = isoDay(time);
                    cells.push({day, time, column: Math.floor(index / 7), row: index % 7, count: this.counts.get(day) ?? 0});
                }
            }

            return cells;
        },
        columns: (self) => (self.cells.length ? self.cells[self.cells.length - 1].column + 1 : 0),
        peak: (self) => Math.max(1, ...self.activity.days.map(([, count]) => count)),
        total: (self) => self.activity.days.reduce((sum, [, count]) => sum + count, 0),
        months() {
            const labels = [];

            for (const cell of this.cells) {
                const month = new Date(cell.time).toLocaleString(undefined, {month: "short", timeZone: "UTC"});

                if (new Date(cell.time).getUTCDate() <= 7 && cell.row === 0 && labels[labels.length - 1]?.month !== month) {
                    labels.push({month, column: cell.column});
                }
            }

            return labels.filter((label, index) => index === 0 || label.column - labels[index - 1].column >= 3);
        },
        width: (self) => HEAT_LEFT + self.columns * HEAT_STEP,
        height: () => HEAT_TOP + 7 * HEAT_STEP,
    },
    methods: {
        level(count) {
            return count ? Math.min(HEAT_LEVELS, Math.ceil((count / this.peak) * HEAT_LEVELS)) : 0;
        },
    },
    template: `<figure class="activity-heatmap">
        <div class="activity-scroll">
            <svg :width="width" :height="height" :viewBox="'0 0 ' + width + ' ' + height" :style="{maxWidth: width + 'px'}" role="img" :aria-label="$f.plural(total, 'play') + ' in the last year'">
                <text v-for="label in months" :key="label.column + label.month" :x="${HEAT_LEFT} + label.column * ${HEAT_STEP}" y="10" class="heat-label">{{ label.month }}</text>
                <text v-for="[row, name] in [[1, 'Mon'], [3, 'Wed'], [5, 'Fri']]" :key="name" x="0" :y="${HEAT_TOP} + row * ${HEAT_STEP} + 9" class="heat-label">{{ name }}</text>
                <rect v-for="cell in cells" :key="cell.day" :x="${HEAT_LEFT} + cell.column * ${HEAT_STEP}" :y="${HEAT_TOP} + cell.row * ${HEAT_STEP}"
                    width="${HEAT_CELL}" height="${HEAT_CELL}" rx="2" :class="'heat-' + level(cell.count)">
                    <title>{{ $f.plural(cell.count, 'play') }} on {{ $f.day(cell.day) }}</title>
                </rect>
            </svg>
        </div>
        <figcaption class="is-size-7 has-text-grey">{{ $f.plural(total, 'play') }} in the last year</figcaption>
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

app.component("podium", {
    props: {entries: Array},
    computed: {
        places: (self) => [1, 0, 2].map((index) => self.entries[index]).filter(Boolean),
    },
    template: `<div v-if="entries.length" class="podium" aria-label="Top three players">
        <a v-for="entry in places" :key="entry.user.id" class="podium-place" :class="'rank-' + entry.rank" :href="$link.user(entry.user.username)">
            <span class="podium-name">{{ entry.user.username }}</span>
            <span class="podium-performance">{{ $f.performance(entry.performance) }}</span>
            <span class="podium-accuracy">{{ $f.accuracy(entry.accuracy) }}</span>
            <span class="podium-block" :title="['', 'First place', 'Second place', 'Third place'][entry.rank]">{{ entry.rank }}</span>
        </a>
    </div>`,
});

app.component("player-table", {
    props: {entries: Array, detailed: Boolean, caption: String, sortable: Boolean},
    data: () => ({defaults: LEADERBOARD_SORT}),
    computed: {
        playsClass: (self) => (self.sortable && self.$root.ariaSort("ranked_plays", self.defaults) ? "" : "is-hidden-mobile"),
    },
    template: `<p v-if="!entries.length" class="has-text-grey">Nobody is ranked yet.</p>
    <div v-else class="table-container"><table class="table is-fullwidth is-hoverable">
        <caption class="is-sr-only">{{ caption }}</caption>
        <thead><tr>
            <th scope="col" class="is-numeric">Rank</th><th scope="col">Player</th>
            <th v-for="[field, label, extra] in [['performance', 'Performance', ''], ['accuracy', 'Accuracy', ''], ...(detailed ? [['ranked_plays', 'Ranked plays', playsClass]] : [])]" :key="field"
                scope="col" class="is-numeric" :class="extra" :aria-sort="sortable ? $root.ariaSort(field, defaults) : null">
                <sort-link v-if="sortable" :field="field" :label="label" :defaults="defaults" :toggle="false"></sort-link><template v-else>{{ label }}</template>
            </th>
        </tr></thead>
        <tbody><tr v-for="entry in entries" :key="entry.user.id">
            <td class="is-numeric"><rank-cell :rank="entry.rank"></rank-cell></td>
            <td><a :href="$link.user(entry.user.username)">{{ entry.user.username }}</a></td>
            <td class="is-numeric">{{ $f.performance(entry.performance) }}</td>
            <td class="is-numeric">{{ $f.accuracy(entry.accuracy) }}</td>
            <td v-if="detailed" class="is-numeric" :class="playsClass">{{ $f.number(entry.ranked_play_count) }}</td>
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
