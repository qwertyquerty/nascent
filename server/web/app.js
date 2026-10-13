const API = document.querySelector('meta[name="nascent-api"]').content;
const PAGE_SIZE = 50;

const query = (params = {}) => new URLSearchParams(Object.entries(params).filter(([, value]) => value != null && value !== "")).toString();
const pageOf = (params) => Math.max(1, parseInt(params.page, 10) || 1);
const paged = (params) => ({limit: PAGE_SIZE, offset: (pageOf(params) - 1) * PAGE_SIZE});

async function api(path, params) {
    const response = await fetch(`${API}${path}?${query(params)}`);
    const body = await response.json().catch(() => null);

    if (!response.ok) {
        throw new Error(response.status === 404 ? "That doesn't exist." : body?.error?.message || `The server answered ${response.status}`);
    }

    return body;
}

const link = {
    to: (path, params) => `#${path}?${query(params)}`.replace(/\?$/, ""),
    user: (name, params) => link.to(`/users/${encodeURIComponent(name)}`, params),
    chart: (hash, rate) => link.to(`/charts/${encodeURIComponent(hash)}`, {rate: rate === 1 ? null : rate}),
    chartPerformance: (hash) => link.to(`/charts/${encodeURIComponent(hash)}`, {board: "performance"}),
};

const format = {
    number: (value) => Number(value ?? 0).toLocaleString(),
    performance: (value) => Number(value ?? 0).toFixed(2),
    accuracy: (value) => `${(Number(value ?? 0) * 100).toFixed(2)}%`,
    rank: (value) => (value ? `#${value.toLocaleString()}` : "—"),
    rate: (value) => `${value.toFixed(2)}x`,
    date: (value) => (value ? new Date(value).toLocaleDateString(undefined, {dateStyle: "medium"}) : "—"),
    length: (ms) => `${Math.floor(ms / 60000)}:${String(Math.round(ms / 1000) % 60).padStart(2, "0")}`,
    playtime: (ms) => `${Math.floor(ms / 3600000).toLocaleString()}h ${Math.floor(ms / 60000) % 60}m`,
};

function parseRoute() {
    const [path, search] = (location.hash.slice(1) || "/").split("?");
    const [section = "home", id] = path.split("/").filter(Boolean).map(decodeURIComponent);
    return {path, name: id ? section.replace(/s$/, "") : section, id, params: Object.fromEntries(new URLSearchParams(search))};
}

const loaders = {
    async home({params}, keyCounts) {
        const keyCount = params.key_count ?? keyCounts[0];
        const [stats, board] = await Promise.all([api("/stats"), api("/leaderboards/performance", {key_count: keyCount, limit: 10})]);
        return {stats, board, keyCount};
    },

    async leaderboard({params}, keyCounts) {
        const keyCount = params.key_count ?? keyCounts[0];
        return {keyCount, ...(await api("/leaderboards/performance", {key_count: keyCount, sort: params.sort, ...paged(params)}))};
    },

    async plays({params}) {
        const performance = params.sort === "performance";
        const page = paged(params);
        const {plays, total} = await api("/plays", {sort: params.sort, key_count: params.key_count, ...page});
        return {total, performance, plays: performance ? plays.map((play, index) => ({rank: page.offset + index + 1, play})) : plays};
    },

    async user({id, params}, keyCounts) {
        const base = `/users/${encodeURIComponent(id)}`;
        const {user} = await api(base);
        const modes = user.key_modes.filter((mode) => keyCounts.includes(mode.key_count) && mode.ranked_play_count > 0);
        const recent = params.tab === "recent" || modes.length === 0;
        const keyCount = Number(params.key_count ?? [...modes].sort((a, b) => b.performance - a.performance)[0]?.key_count);
        const {plays} = await api(`${base}/plays/${recent ? "recent" : "best"}`, {key_count: recent ? null : keyCount, limit: PAGE_SIZE});
        return {user, modes, recent, keyCount, plays};
    },

    charts: ({params}) => api("/charts", {q: params.q, key_count: params.key_count, status: params.status === "all" ? "" : "ranked", sort: "play_count", order: "desc", ...paged(params)}),

    async chart({id, params}) {
        const base = `/charts/${encodeURIComponent(id)}`;
        const performance = params.board === "performance";
        const board = performance ? api(`${base}/leaderboard/performance`, paged(params)) : api(`${base}/leaderboard`, {rate: params.rate, ...paged(params)});
        const [{chart}, entries] = await Promise.all([api(base), board]);
        return {chart, board: entries, performance};
    },
};

const app = Vue.createApp({
    data: () => ({route: parseRoute(), view: null, error: null, keyCounts: [], menuOpen: false}),

    async created() {
        addEventListener("hashchange", () => this.load());
        this.keyCounts = (await api("/info").catch(() => ({ranked_key_counts: []}))).ranked_key_counts;
        this.load();
    },

    methods: {
        async load() {
            const hash = location.hash;
            const route = parseRoute();
            Object.assign(this, {route, view: null, error: null, menuOpen: false});
            scrollTo(0, 0);

            try {
                const loader = loaders[route.name];

                if (!loader) {
                    throw new Error("That doesn't exist.");
                }

                const view = await loader(route, this.keyCounts);

                if (hash === location.hash) {
                    this.view = view;
                }
            }
            catch (error) {
                if (hash === location.hash) {
                    this.error = error.message;
                }
            }
        },

        go(hash) {
            location.hash = hash;
        },

        submit(event, path) {
            const fields = Object.fromEntries(new FormData(event.target));
            location.hash = path ? link.to(path, fields) : link.user(fields.player.trim());
            event.target.reset();
        },
    },
});

app.config.globalProperties.$f = format;
app.config.globalProperties.$link = link;

app.component("key-tabs", {
    props: ["current", "path", "params", "counts"],
    template: `<div class="tabs is-boxed"><ul>
        <li v-for="count in counts ?? $root.keyCounts" :class="{'is-active': count == current}">
            <a :href="$link.to(path, {...params, key_count: count})">{{ count }}K</a>
        </li>
    </ul></div>`,
});

app.component("pager", {
    props: ["total"],
    computed: {
        page: (self) => pageOf(self.$root.route.params),
        pages: (self) => Math.ceil(self.total / PAGE_SIZE),
    },
    methods: {
        href(page) {
            return page >= 1 && page <= this.pages ? link.to(this.$root.route.path, {...this.$root.route.params, page}) : undefined;
        },
    },
    template: `<nav v-if="pages > 1" class="pagination is-centered mt-4">
        <a class="pagination-previous" :class="{'is-disabled': !href(page - 1)}" :href="href(page - 1)">Previous</a>
        <a class="pagination-next" :class="{'is-disabled': !href(page + 1)}" :href="href(page + 1)">Next</a>
        <ul class="pagination-list"><li><span class="pagination-ellipsis">Page {{ page }} of {{ pages }}</span></li></ul>
    </nav>`,
});

app.component("player-table", {
    props: {entries: Array, detailed: Boolean},
    template: `<p v-if="!entries.length" class="has-text-grey">Nobody is ranked yet.</p>
    <div v-else class="table-container"><table class="table is-fullwidth is-hoverable is-striped">
        <thead><tr>
            <th class="is-numeric">Rank</th><th>Player</th><th class="is-numeric">Performance</th><th class="is-numeric">Accuracy</th>
            <th v-if="detailed" class="is-numeric">Ranked plays</th>
        </tr></thead>
        <tbody><tr v-for="entry in entries" :key="entry.user.id">
            <td class="is-numeric">{{ $f.rank(entry.rank) }}</td>
            <td><a :href="$link.user(entry.user.username)">{{ entry.user.username }}</a></td>
            <td class="is-numeric">{{ $f.performance(entry.performance) }}</td>
            <td class="is-numeric">{{ $f.accuracy(entry.accuracy) }}</td>
            <td v-if="detailed" class="is-numeric">{{ $f.number(entry.ranked_play_count) }}</td>
        </tr></tbody>
    </table></div>`,
});

app.component("play-table", {
    props: {plays: Array, showUser: Boolean, showChart: {type: Boolean, default: true}, empty: {type: String, default: "No plays yet."}},
    computed: {
        rows: (self) => self.plays.map((entry) => ({...entry, play: entry.play ?? entry})),
        ranked: (self) => self.plays[0]?.rank !== undefined,
        weighted: (self) => self.plays[0]?.weight !== undefined,
    },
    template: `<p v-if="!rows.length" class="has-text-grey">{{ empty }}</p>
    <div v-else class="table-container"><table class="table is-fullwidth is-hoverable is-striped">
        <thead><tr>
            <th v-if="ranked" class="is-numeric">Rank</th><th v-if="showUser">Player</th><th v-if="showChart">Chart</th>
            <th class="is-numeric">Rate</th><th>Grade</th><th class="is-numeric">Accuracy</th><th class="is-numeric">Combo</th>
            <th class="is-numeric">Performance</th><th v-if="weighted" class="is-numeric">Weighted</th><th class="is-numeric">Date</th>
        </tr></thead>
        <tbody><tr v-for="{play, rank, weight} in rows" :key="play.id">
            <td v-if="ranked" class="is-numeric">{{ $f.rank(rank) }}</td>
            <td v-if="showUser"><a :href="$link.user(play.user.username)">{{ play.user.username }}</a></td>
            <td v-if="showChart" class="chart-name">
                <a :href="$link.chart(play.chart.hash, play.rate)">{{ play.chart.artist }} - {{ play.chart.title }}</a> <span class="has-text-grey">[{{ play.chart.difficulty_name }}]</span>
            </td>
            <td class="is-numeric" :class="{'rate-changed': play.rate !== 1}">{{ $f.rate(play.rate) }}</td>
            <td><span class="grade" :class="'grade-' + play.grade">{{ play.grade }}</span></td>
            <td class="is-numeric">{{ $f.accuracy(play.accuracy) }}</td>
            <td class="is-numeric">{{ $f.number(play.max_combo) }}</td>
            <td class="is-numeric">{{ play.ranked ? $f.performance(play.performance) : "unranked" }}</td>
            <td v-if="weighted" class="is-numeric">{{ Math.round(weight * 100) }}%</td>
            <td class="is-numeric">{{ $f.date(play.submitted_at) }}</td>
        </tr></tbody>
    </table></div>`,
});

app.mount("#app");
