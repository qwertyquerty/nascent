const PLOTLY_SOURCE = "https://cdn.jsdelivr.net/npm/plotly.js-dist-min@2.35.2/plotly.min.js";
const PLOTLY_INTEGRITY = "sha384-cCVCZkAjYNxaYKbM8lsArLznDF/SvMFr1jcZrvOpSTCa0W40ZAdLzHCEulnUa5i7";
const CHART_INK = {primary: "#ffffff", secondary: "#c3c2b7", muted: "#898781", grid: "#2c2c2a", axis: "#383835", surface: "#1a1a19"};
const SERIES = ["#3987e5", "#d95926", "#199e70"];
const BRAND = "#00b1e1";
const SEQUENTIAL = ["#0d366b", "#104281", "#184f95", "#1c5cab", "#256abf", "#2a78d6", "#3987e5", "#5598e7", "#6da7ec", "#86b6ef", "#9ec5f4", "#b7d3f6", "#cde2fb"];
const LABELLED_COURSES = ["INTRO-1st (1st Pack)", "1st (1st Pack)", "6th (2nd Pack)", "10th (2nd Pack)", "EXTRA-ALPHA (2nd Pack)", "EXTRA-EPSILON (2nd Pack)", "EXTRA-ZETA v2"];
const HIDDEN_COURSES = ["6th (1st Pack)"];
const SCATTER_LABELS = ["INTRO-1st (1st Pack)", "6th (2nd Pack)", "EXTRA-ZETA v2"];
const COURSE_ORDER = ["INTRO-1st", "INTRO-2nd", "INTRO-3rd", "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th", "9th", "10th", "EXTRA-ALPHA", "EXTRA-BETA", "EXTRA-GAMMA", "EXTRA-DELTA", "EXTRA-EPSILON", "EXTRA-ZETA"];

function courseRank(label) {
    const name = label.replace(/ \(.*\)$/, "").replace(" v2", "");
    return COURSE_ORDER.indexOf(name) * 2 + (label.includes("2nd Pack") ? 1 : 0);
}

let plotlyLoading = null;

function loadPlotly() {
    if (window.Plotly) {
        return Promise.resolve(window.Plotly);
    }

    plotlyLoading ??= new Promise((resolve, reject) => {
        const script = document.createElement("script");
        script.src = PLOTLY_SOURCE;
        script.integrity = PLOTLY_INTEGRITY;
        script.crossOrigin = "anonymous";
        script.onload = () => resolve(window.Plotly);
        script.onerror = () => {
            plotlyLoading = null;
            reject(new Error("Couldn't load the charts. Check your connection and try again."));
        };
        document.head.append(script);
    });

    return plotlyLoading;
}

function chartAxis(title, extra = {}) {
    return {title: {text: title, font: {color: CHART_INK.secondary}}, gridcolor: CHART_INK.grid, zerolinecolor: CHART_INK.axis, linecolor: CHART_INK.axis, tickfont: {color: CHART_INK.muted}, automargin: true, ...extra};
}

function chartLayout(layout) {
    return {
        paper_bgcolor: "rgba(0,0,0,0)",
        plot_bgcolor: "rgba(0,0,0,0)",
        font: {family: "system-ui, -apple-system, 'Segoe UI', sans-serif", color: CHART_INK.secondary, size: 13},
        margin: {l: 64, r: 24, t: 24, b: 56},
        hoverlabel: {bgcolor: CHART_INK.surface, bordercolor: CHART_INK.axis, font: {color: CHART_INK.primary}},
        legend: {orientation: "h", x: 0, y: 1.12, font: {color: CHART_INK.secondary}},
        ...layout,
    };
}

function spearman(a, b) {
    const ranks = (values) => {
        const order = values.map((value, index) => [value, index]).sort((x, y) => x[0] - y[0]);
        const result = new Array(values.length);
        order.forEach(([, index], rank) => (result[index] = rank));
        return result;
    };
    const ra = ranks(a);
    const rb = ranks(b);
    const mean = (ra.length - 1) / 2;
    let top = 0;
    let left = 0;
    let right = 0;

    for (let index = 0; index < ra.length; index++) {
        top += (ra[index] - mean) * (rb[index] - mean);
        left += (ra[index] - mean) ** 2;
        right += (rb[index] - mean) ** 2;
    }

    return top / Math.sqrt(left * right);
}

function shortLabel(label) {
    return label.replace(" (1st Pack)", "").replace(" (2nd Pack)", "");
}

function displayCurveFigure(data) {
    const points = [...data.anchors].sort((a, b) => a.raw - b.raw);
    const annotations = points.map((anchor, index) => {
        const above = index % 2 === 0;
        return {
            x: anchor.raw,
            y: anchor.shown,
            text: `${shortLabel(anchor.label)}: ${anchor.shown.toFixed(1)}`,
            showarrow: true,
            arrowhead: 0,
            arrowwidth: 1,
            arrowcolor: CHART_INK.axis,
            ax: above ? -28 : 28,
            ay: above ? -30 : 30,
            xanchor: above ? "right" : "left",
            font: {color: CHART_INK.secondary, size: 11},
            bgcolor: CHART_INK.surface,
            borderpad: 2,
        };
    });
    return {
        data: [
            {x: data.display_curve.raw, y: data.display_curve.shown, mode: "lines", line: {color: BRAND, width: 2}, hovertemplate: "Raw %{x:.2f} is shown as %{y:.1f}<extra></extra>", name: "Scale"},
            {x: points.map((anchor) => anchor.raw), y: points.map((anchor) => anchor.shown), mode: "markers", marker: {color: CHART_INK.primary, size: 8, line: {color: CHART_INK.surface, width: 2}}, customdata: points.map((anchor) => shortLabel(anchor.label)), hovertemplate: "%{customdata}: raw %{x:.2f} is shown as %{y:.1f}<extra></extra>", name: "Dan courses"},
        ],
        layout: chartLayout({showlegend: false, annotations, xaxis: chartAxis("Raw output"), yaxis: chartAxis("Scaled output", {rangemode: "tozero"})}),
    };
}

function ratesFigure(data) {
    const courses = [...data.rates.courses].sort((a, b) => courseRank(a.label) - courseRank(b.label));
    const rates = data.rates.rates.map((rate) => `${rate.toFixed(2)}x`);
    return {
        data: [{
            type: "heatmap",
            x: rates,
            y: courses.map((course) => course.label),
            z: courses.map((course) => course.values),
            text: courses.map((course) => course.values.map((value) => value.toFixed(1))),
            texttemplate: "%{text}",
            textfont: {size: 10, color: "#0b0b0b"},
            colorscale: PALETTE.difficulty_scale.map(([value, color]) => [value / 100, color]),
            zmin: 0,
            zmax: 100,
            xgap: 2,
            ygap: 2,
            colorbar: {title: {text: "Difficulty", font: {color: CHART_INK.secondary}}, tickfont: {color: CHART_INK.muted}, outlinewidth: 0},
            hovertemplate: "%{y} at %{x}: %{z:.1f}<extra></extra>",
        }],
        layout: chartLayout({margin: {l: 64, r: 24, t: 16, b: 48}, xaxis: chartAxis("Rate", {showgrid: false}), yaxis: chartAxis("", {showgrid: false, autorange: "reversed"})}),
    };
}

function lossFigure(data) {
    return {
        data: ["press", "release"].map((kind, index) => ({x: data.loss.offset, y: data.loss[kind], mode: "lines", line: {color: SERIES[index], width: 2}, name: kind === "press" ? "Note presses" : "Hold releases", hovertemplate: `${kind === "press" ? "Press" : "Release"}: difficulty - skill %{x:.2f}, miss weight %{y:.3f}<extra></extra>`})),
        layout: chartLayout({xaxis: chartAxis("Note difficulty minus player skill"), yaxis: chartAxis("Expected accuracy lost on the note", {rangemode: "tozero"})}),
    };
}

function mixFigure(data) {
    return {
        data: [{x: data.skill_mix.shown, y: data.skill_mix.strong.map((value) => value * 100), mode: "lines", line: {color: BRAND, width: 2}, hovertemplate: "A %{x:.0f} player: %{y:.0f}% strong-player weights<extra></extra>"}],
        layout: chartLayout({showlegend: false, xaxis: chartAxis("Player skill, on the displayed scale", {range: [0, 150]}), yaxis: chartAxis("Strong-player weights used (%)", {range: [0, 100]})}),
    };
}

function covariateFigures(data) {
    const shift = (values) => values.map((value) => Math.exp(value));
    const panel = (x, y, title, format, logarithmic = false) => ({
        data: [{x, y: shift(y), mode: "lines", line: {color: BRAND, width: 2}, hovertemplate: `${format}: difficulty x%{y:.3f}<extra></extra>`}],
        layout: chartLayout({showlegend: false, margin: {l: 56, r: 16, t: 16, b: 52}, xaxis: chartAxis(title, logarithmic ? {type: "log"} : {}), yaxis: chartAxis("Difficulty multiplier")}),
    });
    return [
        panel(data.covariates.notes.x, data.covariates.notes.shift, "Notes in the chart", "%{x:,.0f} notes", true),
        panel(data.covariates.minutes.x, data.covariates.minutes.shift, "Active length (minutes)", "%{x:.1f} minutes"),
        panel(data.covariates.holds.x.map((value) => value * 100), data.covariates.holds.shift, "Notes that are holds (%)", "%{x:.0f}% holds"),
    ];
}

function jackFigure(data) {
    const gaps = data.jack_response.gap_ms;
    const bpm = gaps.map((gap) => 15000 / gap);
    return {
        data: [
            {x: gaps, y: data.jack_response.short, customdata: bpm, mode: "lines", line: {color: SERIES[0], width: 2}, name: "Short runs (up to 5 notes)", hovertemplate: "Short run, %{x:.0f} ms (1/4 at %{customdata:.0f} BPM): %{y:.4f}<extra></extra>"},
            {x: gaps, y: data.jack_response.long, customdata: bpm, mode: "lines", line: {color: SERIES[1], width: 2}, name: "Long runs (12+ notes)", hovertemplate: "Long run, %{x:.0f} ms (1/4 at %{customdata:.0f} BPM): %{y:.4f}<extra></extra>"},
        ],
        layout: chartLayout({xaxis: chartAxis("Time since the previous note in the same column (ms)"), yaxis: chartAxis("Weighted jack difficulty per note", {type: "log"})}),
    };
}

function contributionFigure(data) {
    const totals = new Map();
    data.weights.forEach((row) => row.share != null && totals.set(row.group, (totals.get(row.group) ?? 0) + row.share));
    const top = [...totals.entries()].sort((a, b) => b[1] - a[1]).slice(0, 25).reverse();
    return {
        data: [{type: "bar", orientation: "h", x: top.map(([, share]) => share * 100), y: top.map(([group]) => group.replaceAll("_", " ")), marker: {color: BRAND}, hovertemplate: "%{y}: %{x:.1f}% of difficulty on a typical chart<extra></extra>"}],
        layout: chartLayout({showlegend: false, bargap: 0.25, margin: {l: 64, r: 24, t: 16, b: 48}, xaxis: chartAxis("Share of total difficulty on an average osu! 4K chart (%)"), yaxis: chartAxis("", {showgrid: false})}),
    };
}

function weightHeatmap(rows, groups, columns, title, height) {
    const lookup = new Map(rows.map((row) => [`${row.group}|${row.column}`, row]));
    const z = groups.map((group) => columns.map((column) => {
        const row = lookup.get(`${group}|${column}`);
        return row ? Math.log10(row.weight) : null;
    }));
    const text = groups.map((group) => columns.map((column) => lookup.get(`${group}|${column}`)?.name ?? "not used"));
    return {
        height,
        figure: {
            data: [{
                type: "heatmap",
                x: columns,
                y: groups.map((group) => group.replaceAll("_", " ")),
                z,
                customdata: text,
                colorscale: SEQUENTIAL.map((color, index) => [index / (SEQUENTIAL.length - 1), color]),
                xgap: 2,
                ygap: 2,
                hoverongaps: false,
                colorbar: {title: {text: "log10 weight", font: {color: CHART_INK.secondary}}, tickfont: {color: CHART_INK.muted}, outlinewidth: 0},
                hovertemplate: "%{customdata}<br>weight 10^%{z:.2f}<extra></extra>",
            }],
            layout: chartLayout({margin: {l: 64, r: 24, t: 16, b: 48}, xaxis: chartAxis(title, {showgrid: false, type: "category"}), yaxis: chartAxis("", {showgrid: false, autorange: "reversed"})}),
        },
    };
}

function weightHeatmaps(data) {
    const timescales = data.timescales.map((value) => `${value}s`);
    const withColumn = (rows, column) => rows.map((row) => ({...row, column: column(row)}));
    const byFamily = (family) => data.weights.filter((row) => row.family === family);
    const groupsOf = (rows) => [...new Set(rows.map((row) => row.group))];
    const pattern = byFamily("pattern");
    const maps = [1, 2, 3].map((power) => {
        const rows = withColumn(pattern.filter((row) => row.power === power), (row) => `${row.timescale}s`);
        return {label: `Patterns, power ${power}`, ...weightHeatmap(rows, groupsOf(pattern), timescales, "Memory timescale", 640)};
    });
    const loads = withColumn(byFamily("load"), (row) => `${row.timescale}s`);
    const curves = withColumn(byFamily("jack curve"), (row) => `${row.timescale}s`);
    const interactions = withColumn(byFamily("interaction"), (row) => `${row.timescale}s`);
    return [
        ...maps,
        {label: "Column and hand load", ...weightHeatmap(loads, groupsOf(loads), timescales, "Memory timescale", 360)},
        {label: "Jack speed curves", ...weightHeatmap(curves, groupsOf(curves), timescales, "Memory timescale", 620)},
        {label: "Skill interactions", ...weightHeatmap(interactions, groupsOf(interactions), ["0.25s", "2.5s"], "Memory timescale", 480)},
    ];
}

function factorFigure(data) {
    const panels = [["strong", "Strong-player factor"], ["release", "Hold-release factor"]];
    const traces = panels.map(([key, label], index) => {
        return {
            x: data.weights.map((row) => row.weight),
            y: data.weights.map((row) => row[key]),
            xaxis: `x${index + 1}`,
            yaxis: `y${index + 1}`,
            mode: "markers",
            marker: {color: BRAND, size: 5, opacity: 0.7},
            customdata: data.weights.map((row) => row.name),
            hovertemplate: `%{customdata}<br>weight %{x:.2e}<br>${label} x%{y:.2f}<extra></extra>`,
        };
    });
    const layout = chartLayout({showlegend: false, grid: {rows: 1, columns: panels.length, pattern: "independent"}, margin: {l: 56, r: 16, t: 36, b: 52}});
    panels.forEach(([, label], index) => {
        layout[`xaxis${index + 1}`] = chartAxis("Base weight", {type: "log"});
        layout[`yaxis${index + 1}`] = chartAxis(index === 0 ? "Factor" : "", {type: "log"});
    });
    layout.annotations = panels.map(([, label], index) => ({text: label, xref: `x${index + 1} domain`, yref: `y${index + 1} domain`, x: 0, y: 1.08, showarrow: false, xanchor: "left", font: {color: CHART_INK.primary, size: 13}}));
    return {data: traces, layout};
}

function scatterFigure(data, key, anchorKey, label, zeta, reference) {
    const indices = data.scatter.title.map((_, index) => index).filter((index) => data.scatter[key][index] != null);
    const courses = data.anchors.filter((anchor) => anchor[anchorKey] != null);
    const reach = Math.max(...indices.map((index) => data.scatter[key][index])) * 1.05;
    const named = (anchor) => (SCATTER_LABELS.includes(anchor.label) ? shortLabel(anchor.label) : "");
    return {
        data: [
            {x: [0, reach], y: [0, (reach * 100) / zeta], mode: "lines", name: reference, line: {color: CHART_INK.muted, width: 1, dash: "dot"}, hoverinfo: "skip"},
            {
                type: "scattergl",
                mode: "markers",
                name: "Charts",
                x: indices.map((index) => data.scatter[key][index]),
                y: indices.map((index) => data.scatter.nascent[index]),
                customdata: indices.map((index) => [data.scatter.title[index], data.scatter.source[index]]),
                marker: {color: SERIES[0], size: 4, opacity: 0.5},
                hovertemplate: `%{customdata[0]}<br>%{customdata[1]}<br>Nascent %{y:.2f}, ${label} %{x:.2f}<extra></extra>`,
            },
            {type: "scattergl", mode: "markers", name: "Dan courses", x: courses.map((anchor) => anchor[anchorKey]), y: courses.map((anchor) => anchor.shown), customdata: courses.map((anchor) => shortLabel(anchor.label)), marker: {color: CHART_INK.primary, size: 10, symbol: "square", line: {color: CHART_INK.surface, width: 2}}, hovertemplate: `%{customdata}<br>Nascent %{y:.2f}, ${label} %{x:.2f}<extra></extra>`},
        ],
        layout: chartLayout({
            xaxis: chartAxis(label, {rangemode: "tozero"}),
            yaxis: chartAxis("Nascent difficulty", {rangemode: "tozero"}),
            annotations: courses.filter(named).map((anchor) => ({x: anchor[anchorKey], y: anchor.shown, text: named(anchor), showarrow: false, xanchor: "right", yanchor: "bottom", xshift: -6, yshift: 6, font: {color: CHART_INK.primary, size: 11}, bgcolor: CHART_INK.surface, borderpad: 2})),
        }),
    };
}

function agreement(data) {
    const sources = [...new Set(data.scatter.source)];
    return sources.map((source) => {
        const members = data.scatter.source.map((value, index) => (value === source ? index : -1)).filter((index) => index >= 0);
        return {source, charts: members.length, spearman: spearman(members.map((index) => data.scatter.nascent[index]), members.map((index) => data.scatter.msd[index]))};
    });
}

function shownCourses(data) {
    const kept = (label) => !HIDDEN_COURSES.includes(label);
    return {...data, anchors: data.anchors.filter((anchor) => kept(anchor.label)), rates: {...data.rates, courses: data.rates.courses.filter((course) => kept(course.label))}};
}

const CALCULATORS = [["nascent", "Nascent"], ["msd", "Etterna MSD"], ["osu", "osu! stars"]];

function trendLine(xs, ys) {
    const n = xs.length;
    const mx = xs.reduce((sum, x) => sum + x, 0) / n;
    const my = ys.reduce((sum, y) => sum + y, 0) / n;
    let top = 0;
    let bottom = 0;

    for (let index = 0; index < n; index++) {
        top += (xs[index] - mx) * (ys[index] - my);
        bottom += (xs[index] - mx) ** 2;
    }

    const slope = top / bottom;
    const low = Math.min(...xs);
    const high = Math.max(...xs);
    return {x: [low, high], y: [my + slope * (low - mx), my + slope * (high - mx)]};
}

function listFigure(list, target) {
    const trends = CALCULATORS.map(([key], index) => ({...trendLine(list[key], list.target), xaxis: `x${index + 1}`, yaxis: `y${index + 1}`, mode: "lines", line: {color: CHART_INK.secondary, width: 1.5, dash: "dot"}, hoverinfo: "skip"}));
    const traces = CALCULATORS.map(([key, name], index) => ({
        x: list[key],
        y: list.target,
        xaxis: `x${index + 1}`,
        yaxis: `y${index + 1}`,
        mode: "markers",
        marker: {color: SERIES[index], size: 6, opacity: 0.75},
        customdata: list.title.map((title, chart) => [title, list.group[chart]]),
        hovertemplate: `%{customdata[0]} (%{customdata[1]})<br>${name} %{x:.2f}, ${target} %{y:.2f}<extra></extra>`,
    }));
    const layout = chartLayout({showlegend: false, grid: {rows: 1, columns: 3, pattern: "independent"}, margin: {l: 56, r: 16, t: 36, b: 52}});
    CALCULATORS.forEach(([key, name], index) => {
        layout[`xaxis${index + 1}`] = chartAxis(name);
        layout[`yaxis${index + 1}`] = chartAxis(index === 0 ? target : "");
    });
    traces.push(...trends);
    layout.annotations = CALCULATORS.map(([key, name], index) => ({text: `${name}, agreement ${list.metrics[key].spearman.toFixed(3)}`, xref: `x${index + 1} domain`, yref: `y${index + 1} domain`, x: 0, y: 1.08, showarrow: false, xanchor: "left", font: {color: CHART_INK.primary, size: 13}}));
    return {data: traces, layout};
}

function benchmarkRows(data) {
    return CALCULATORS.map(([key, name]) => ({name, scores: data.scores.metrics[key], calcTest: data.calc_test.metrics[key], community: data.community.metrics[key]}));
}

function difficultyFigures(source) {
    const data = shownCourses(source);
    return {
        display: displayCurveFigure(data),
        rates: ratesFigure(data),
        loss: lossFigure(data),
        mix: mixFigure(data),
        covariates: covariateFigures(data),
        jack: jackFigure(data),
        contribution: contributionFigure(data),
        heatmaps: weightHeatmaps(data),
        factors: factorFigure(data),
        scatter: scatterFigure(data, "msd", "msd", "Etterna MSD", data.model.msd_zeta, "Scaled MSD Reference"),
        osuScatter: scatterFigure(data, "stars", "osu", "osu! star rating", data.model.osu_zeta, "Scaled star rating reference"),
        scores: listFigure(data.scores, "Difficulty from scores"),
        calcTest: listFigure(data.calc_test, "Target MSD"),
        community: listFigure(data.community, "Community rating"),
        benchmarks: benchmarkRows(data),
        agreement: agreement(data),
    };
}
