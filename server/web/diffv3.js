const V3_CALCULATORS = [["v3", "Nascent v3"], ["v2", "Nascent v2"], ["msd", "Etterna MSD"], ["osu", "osu! stars"]];
const V3_COLORS = {v3: "#3987e5", v2: "#b45fd0", msd: "#d95926", osu: "#199e70"};
const RESERVED_ORDER = [["Nascent v3", "v3"], ["Nascent v2", "v2"], ["Etterna MSD", "msd"], ["osu! star rating", "osu"]];
const HARDER = "#d95926";
const EASIER = "#3987e5";
const ROW_LABELS = {
    chord_2: "Two-note chords",
    chord_3: "Three-note chords",
    chord_4plus: "Four-note chords and more",
    hand_chord: "Notes the hand presses together",
    ln_head: "Hold starts",
    held_same_hand: "Notes pressed while the same hand holds",
    trill: "One-hand trills",
    anchor: "Anchored columns",
    grace: "Grace notes and partial flams",
    jack_fast: "Fast jacks (150 ms down to 90 ms)",
    jack_long: "Long jack runs",
    vibro_fast: "Vibro, on top of the jack multipliers",
    jack_vfast: "Very fast jacks (100 ms down to 70 ms)",
};
const PATTERN_LABELS = {
    vibro_share: "Notes repeating within 100 ms",
    fast_jack_share: "Notes repeating within 90 ms",
    chord_share: "Notes in chords",
    ln_share: "Holds",
    one_hand_trill_share: "One-hand trills",
    anchor_share: "Anchoring",
    grace_share: "Grace notes",
    nps: "Notes per second",
    peak_nps_5s: "Peak notes per second (5 s)",
    length_s: "Length (s)",
};
const NEW_FAMILY_LABELS = {flam: "Flams", hand_trill: "One-hand trills", anchor: "Anchors", burst: "Bursts", "chorded jacks": "Chorded jacks", "150 s and 300 s windows": "150 s and 300 s windows"};
const DATASET_LABELS = {"osuapi 4K": "osu! 4K", "osuapi 7K": "osu! 7K", "etterna 4K": "Etterna 4K"};

function binLabel(value, share) {
    if (share) {
        const scaled = value * 100;
        return `${scaled >= 10 ? scaled.toFixed(0) : scaled.toFixed(1)}%`;
    }

    return value >= 10 ? value.toFixed(0) : value.toFixed(1);
}

function chartLabel(title, rate, source) {
    const split = title.lastIndexOf(" [");
    const base = split > 0 ? title.slice(0, split) : title;
    const version = split > 0 ? title.slice(split + 1) : "";
    const shortBase = base.length > 44 ? `${base.slice(0, 42)}…` : base;
    const shortVersion = version.length > 32 ? `${version.slice(0, 30)}…]` : version;
    return `${shortBase} ${shortVersion}${rate !== 1 ? ` ${rate}x` : ""} · ${source}`;
}

function uniqueLabels(labels) {
    const seen = new Map();
    return labels.map((label) => {
        const count = (seen.get(label) ?? 0) + 1;
        seen.set(label, count);
        return count > 1 ? `${label} (${count})` : label;
    });
}

function percent(value, digits = 2) {
    return `${(value * 100).toFixed(digits)}%`;
}

function v3Data(source) {
    const data = shownCourses(source);
    return {...data, scatter: {...data.scatter, nascent: data.scatter.v3}};
}

function reservedFigure(data) {
    const modes = Object.keys(data.compare.reserved);
    const traces = RESERVED_ORDER.map(([name, key]) => {
        const values = modes.map((mode) => data.compare.reserved[mode][name]);
        return {
            type: "bar",
            name,
            x: modes,
            y: values.map((value) => value.pairs * 100),
            error_y: {type: "data", symmetric: false, array: values.map((value) => (value.high - value.pairs) * 100), arrayminus: values.map((value) => (value.pairs - value.low) * 100), color: CHART_INK.secondary, thickness: 1, width: 4},
            marker: {color: V3_COLORS[key]},
            hovertemplate: `${name}, %{x}: %{y:.2f}% of pairs<extra></extra>`,
        };
    });
    return {data: traces, layout: chartLayout({barmode: "group", bargap: 0.25, xaxis: chartAxis("", {showgrid: false}), yaxis: chartAxis("Pairs ranked correctly (%)", {range: [60, 100]})})};
}

function reservedRows(data) {
    return Object.entries(data.compare.reserved).map(([mode, calculators]) => {
        const v3 = calculators["Nascent v3"];
        const v2 = calculators["Nascent v2"];
        return {mode, v3, v2, msd: calculators["Etterna MSD"], osu: calculators["osu! star rating"], gain: v3.pairs - v2.pairs, closeGain: v3.close - v2.close};
    });
}

function listFigure4(list, target) {
    const traces = [];
    const layout = chartLayout({showlegend: false, grid: {rows: 1, columns: 4, pattern: "independent"}, margin: {l: 56, r: 16, t: 36, b: 52}});
    V3_CALCULATORS.forEach(([key, name], index) => {
        traces.push({
            x: list[key],
            y: list.target,
            xaxis: `x${index + 1}`,
            yaxis: `y${index + 1}`,
            mode: "markers",
            marker: {color: V3_COLORS[key], size: 6, opacity: 0.75},
            customdata: list.title.map((title, chart) => [title, list.group[chart]]),
            hovertemplate: `%{customdata[0]} (%{customdata[1]})<br>${name} %{x:.2f}, ${target} %{y:.2f}<extra></extra>`,
        });
        traces.push({...trendLine(list[key], list.target), xaxis: `x${index + 1}`, yaxis: `y${index + 1}`, mode: "lines", line: {color: CHART_INK.secondary, width: 1.5, dash: "dot"}, hoverinfo: "skip"});
        layout[`xaxis${index + 1}`] = chartAxis(name);
        layout[`yaxis${index + 1}`] = chartAxis(index === 0 ? target : "");
    });
    layout.annotations = V3_CALCULATORS.map(([key, name], index) => ({text: `${name}, agreement ${list.metrics[key].spearman.toFixed(3)}`, xref: `x${index + 1} domain`, yref: `y${index + 1} domain`, x: 0, y: 1.08, showarrow: false, xanchor: "left", font: {color: CHART_INK.primary, size: 12}}));
    return {data: traces, layout};
}

function skillFigure(list) {
    const skills = Object.keys(list.metrics.v3.by_group);
    const traces = V3_CALCULATORS.filter(([key]) => key !== "osu").map(([key, name]) => ({
        type: "bar",
        name,
        x: skills,
        y: skills.map((skill) => list.metrics[key].by_group[skill]),
        marker: {color: V3_COLORS[key]},
        hovertemplate: `${name}, %{x}: off by %{y:.2f} MSD on average<extra></extra>`,
    }));
    return {data: traces, layout: chartLayout({barmode: "group", bargap: 0.25, xaxis: chartAxis("Skillset", {showgrid: false}), yaxis: chartAxis("Average miss in MSD points (lower is better)", {rangemode: "tozero"})})};
}

function benchmarkRowsV3(data) {
    return V3_CALCULATORS.map(([key, name]) => ({key, name, scores: data.scores.metrics[key], calcTest: data.calc_test.metrics[key], community: data.community.metrics[key]}));
}

function danCompareFigure(data) {
    const courses = [...data.rates.courses].sort((a, b) => courseRank(a.label) - courseRank(b.label));
    const unit = data.rates.rates.indexOf(1);
    const labels = courses.map((course) => course.label);
    const traces = [{x: courses.flatMap((course) => [course.v2[unit], course.values[unit], null]), y: labels.flatMap((label) => [label, label, null]), mode: "lines", line: {color: CHART_INK.axis, width: 2}, hoverinfo: "skip", showlegend: false}];
    [["v2", "Nascent v2", (course) => course.v2[unit]], ["v3", "Nascent v3", (course) => course.values[unit]]].forEach(([key, name, value]) => {
        traces.push({x: courses.map(value), y: labels, mode: "markers", name, marker: {color: V3_COLORS[key], size: 10, line: {color: CHART_INK.surface, width: 2}}, hovertemplate: `%{y}: ${name} %{x:.1f}<extra></extra>`});
    });
    return {data: traces, layout: chartLayout({xaxis: chartAxis("Difficulty at 1.0x", {rangemode: "tozero"}), yaxis: chartAxis("", {showgrid: false, autorange: "reversed"})})};
}

function signedPoints(value) {
    const rounded = Math.round(value * 10) / 10;
    return rounded === 0 ? "0.0" : (rounded > 0 ? "+" : "") + rounded.toFixed(1);
}

function skillDanFigure(data) {
    const rows = data.compare.skill_dans;
    const skills = [...new Set(rows.map((row) => row.skill))];
    const reach = Math.max(...rows.map((row) => Math.abs(row.v3 - row.v2))) * 1.25;
    const layout = chartLayout({showlegend: false, grid: {rows: 2, columns: 2, pattern: "independent", xgap: 0.08, ygap: 0.22}, margin: {l: 56, r: 24, t: 40, b: 56}, annotations: []});
    const traces = skills.map((skill, index) => {
        const members = rows.filter((row) => row.skill === skill);
        const deltas = members.map((row) => row.v3 - row.v2);
        const median = [...deltas].sort((a, b) => a - b)[Math.floor(deltas.length / 2)];
        layout[`xaxis${index + 1}`] = chartAxis("", {showgrid: false, type: "category", tickangle: -45, tickfont: {color: CHART_INK.muted, size: 10}});
        layout[`yaxis${index + 1}`] = chartAxis(index % 2 === 0 ? "v3 minus v2" : "", {range: [-reach, reach], zeroline: true, zerolinecolor: CHART_INK.secondary});
        layout.annotations.push({text: `${skill}: median ${signedPoints(median)}`, xref: `x${index + 1} domain`, yref: `y${index + 1} domain`, x: 0, y: 1.12, showarrow: false, xanchor: "left", font: {color: CHART_INK.primary, size: 13}});
        return {
            type: "bar",
            xaxis: `x${index + 1}`,
            yaxis: `y${index + 1}`,
            x: members.map((row) => row.level),
            y: deltas,
            marker: {color: deltas.map((delta) => (delta >= 0 ? HARDER : EASIER))},
            text: deltas.map(signedPoints),
            textposition: "outside",
            textfont: {color: CHART_INK.secondary, size: 10},
            cliponaxis: false,
            customdata: members.map((row) => [row.song, row.v2, row.v3, row.msd]),
            hovertemplate: `${skill} %{x}: %{customdata[0]}<br>v2 %{customdata[1]:.1f} to v3 %{customdata[2]:.1f} (%{y:+.1f})<br>MSD %{customdata[3]:.2f}<extra></extra>`,
        };
    });
    return {data: traces, layout};
}

function skillDanRows(data) {
    const rows = data.compare.skill_dans;
    const skills = [...new Set(rows.map((row) => row.skill))];
    const levels = [...new Set(rows.map((row) => row.level))];
    const cell = (row) => row && {...row, change: signedPoints(row.v3 - row.v2), direction: Math.round((row.v3 - row.v2) * 10) / 10};
    return {skills, rows: levels.map((level) => ({level, cells: skills.map((skill) => cell(rows.find((row) => row.skill === skill && row.level === level)) ?? null)}))};
}

function rateDeltaFigure(data) {
    const courses = [...data.rates.courses].sort((a, b) => courseRank(a.label) - courseRank(b.label));
    const z = courses.map((course) => course.values.map((value, index) => value - course.v2[index]));
    const reach = Math.max(...z.flat().map(Math.abs));
    return {
        data: [{
            type: "heatmap",
            x: data.rates.rates.map((rate) => `${rate.toFixed(2)}x`),
            y: courses.map((course) => course.label),
            z,
            text: z.map((row) => row.map((value) => (value >= 0 ? "+" : "") + value.toFixed(1))),
            texttemplate: "%{text}",
            textfont: {size: 10, color: CHART_INK.primary},
            colorscale: [[0, EASIER], [0.5, CHART_INK.grid], [1, HARDER]],
            zmin: -reach,
            zmax: reach,
            xgap: 2,
            ygap: 2,
            colorbar: {title: {text: "v3 minus v2", font: {color: CHART_INK.secondary}}, tickfont: {color: CHART_INK.muted}, outlinewidth: 0},
            hovertemplate: "%{y} at %{x}: %{z:+.1f}<extra></extra>",
        }],
        layout: chartLayout({margin: {l: 64, r: 24, t: 16, b: 48}, xaxis: chartAxis("Rate", {showgrid: false}), yaxis: chartAxis("", {showgrid: false, autorange: "reversed"})}),
    };
}

function versionScatter(data) {
    const sources = [...new Set(data.scatter.source)];
    const reach = Math.max(...data.scatter.v2, ...data.scatter.v3) * 1.05;
    const traces = [{x: [0, reach], y: [0, reach], mode: "lines", name: "No change", line: {color: CHART_INK.muted, width: 1, dash: "dot"}, hoverinfo: "skip"}];
    sources.forEach((source, index) => {
        const members = data.scatter.source.map((value, chart) => (value === source ? chart : -1)).filter((chart) => chart >= 0);
        traces.push({type: "scattergl", mode: "markers", name: source, x: members.map((chart) => data.scatter.v2[chart]), y: members.map((chart) => data.scatter.v3[chart]), customdata: members.map((chart) => data.scatter.title[chart]), marker: {color: SERIES[index], size: 4, opacity: 0.5}, hovertemplate: "%{customdata}<br>v2 %{x:.2f}, v3 %{y:.2f}<extra></extra>"});
    });
    return {data: traces, layout: chartLayout({xaxis: chartAxis("Nascent v2", {rangemode: "tozero"}), yaxis: chartAxis("Nascent v3", {rangemode: "tozero"})})};
}

function moverSegments(rows, labels, keep) {
    const x = [];
    const y = [];
    rows.forEach((row, index) => {
        if (keep(row)) {
            x.push(row.v2, row.v3, null);
            y.push(labels[index], labels[index], null);
        }
    });
    return {x, y};
}

function moversFigure(rows) {
    const labels = uniqueLabels(rows.map((row) => chartLabel(row.title, row.rate, row.source)));
    return {
        data: [
            {...moverSegments(rows, labels, (row) => row.delta >= 0), mode: "lines", name: "Harder in v3", line: {color: HARDER, width: 2}, hoverinfo: "skip"},
            {...moverSegments(rows, labels, (row) => row.delta < 0), mode: "lines", name: "Easier in v3", line: {color: EASIER, width: 2}, hoverinfo: "skip"},
            {x: rows.map((row) => row.v2), y: labels, mode: "markers", name: "Nascent v2", marker: {color: V3_COLORS.v2, size: 9, line: {color: CHART_INK.surface, width: 2}}, hovertemplate: "%{y}<br>v2 %{x:.1f}<extra></extra>"},
            {x: rows.map((row) => row.v3), y: labels, mode: "markers", name: "Nascent v3", marker: {color: V3_COLORS.v3, size: 9, symbol: "diamond", line: {color: CHART_INK.surface, width: 2}}, customdata: rows.map((row) => [row.delta, row.scores]), hovertemplate: "%{y}<br>v3 %{x:.1f} (%{customdata[0]:+.1f}), %{customdata[1]:,} scores<extra></extra>"},
        ],
        layout: chartLayout({margin: {l: 64, r: 24, t: 120, b: 24}, legend: {orientation: "h", x: 0, y: 1 + 72 / (120 + 22 * rows.length), yanchor: "bottom", font: {color: CHART_INK.secondary}}, xaxis: chartAxis("Displayed difficulty, v2 to v3", {side: "top"}), yaxis: chartAxis("", {showgrid: false, autorange: "reversed", categoryorder: "array", categoryarray: labels, tickfont: {color: CHART_INK.muted, size: 11}})}),
    };
}

function histogramFigure(data) {
    const edges = data.compare.movers.histogram_edges;
    const centres = edges.slice(0, -1).map((edge, index) => (edge + edges[index + 1]) / 2);
    return {
        data: [{type: "bar", x: centres, y: data.compare.movers.histogram, marker: {color: centres.map((centre) => (centre < 0 ? EASIER : HARDER))}, hovertemplate: "%{x:+.0f} points: %{y:,} charts<extra></extra>"}],
        layout: chartLayout({showlegend: false, bargap: 0.08, xaxis: chartAxis("v3 minus v2 (displayed points, clipped to ±30)"), yaxis: chartAxis("Charts")}),
    };
}

function patternFigures(data) {
    return Object.entries(data.compare.by_pattern).map(([stat, rows]) => {
        const share = stat.endsWith("_share");
        const labels = rows.map((row) => (row.high === 0 ? "none" : `${binLabel(row.low, share)}–${binLabel(row.high, share)}`));
        return {
            label: PATTERN_LABELS[stat] ?? stat,
            height: 300,
            figure: {
                data: [{type: "bar", x: labels, y: rows.map((row) => (row.ratio - 1) * 100), customdata: rows.map((row) => [row.charts, row.delta]), marker: {color: rows.map((row) => (row.ratio < 1 ? EASIER : HARDER))}, hovertemplate: "%{x}: median change %{y:+.1f}% (mean %{customdata[1]:+.1f} points) over %{customdata[0]:,} charts<extra></extra>"}],
                layout: chartLayout({showlegend: false, bargap: 0.25, margin: {l: 56, r: 16, t: 16, b: 52}, xaxis: chartAxis(PATTERN_LABELS[stat] ?? stat, {type: "category", showgrid: false}), yaxis: chartAxis("Median change from v2 (%)", {zeroline: true})}),
            },
        };
    });
}

function pairPatternFigure(data) {
    const datasets = Object.keys(data.compare.pair_patterns);
    const stats = Object.keys(data.compare.pair_patterns[datasets[0]].v3.patterns);
    const labels = stats.map((stat) => PATTERN_LABELS[stat] ?? stat);
    const traces = [];
    const layout = chartLayout({barmode: "group", bargap: 0.25, grid: {rows: 1, columns: datasets.length, pattern: "independent"}, margin: {l: 64, r: 24, t: 96, b: 52}, legend: {orientation: "h", x: 0, y: 1.2, font: {color: CHART_INK.secondary}}});
    datasets.forEach((dataset, index) => {
        [["v2", "Nascent v2"], ["v3", "Nascent v3"]].forEach(([version, name]) => {
            const result = data.compare.pair_patterns[dataset][version];
            traces.push({
                type: "bar",
                orientation: "h",
                name,
                legendgroup: version,
                showlegend: index === 0,
                xaxis: `x${index + 1}`,
                yaxis: `y${index + 1}`,
                y: labels,
                x: stats.map((stat) => result.patterns[stat]),
                marker: {color: V3_COLORS[version]},
                hovertemplate: `${dataset}, ${name}: %{y} %{x:+.3f}<extra></extra>`,
            });
        });
        const v3 = data.compare.pair_patterns[dataset].v3;
        const v2 = data.compare.pair_patterns[dataset].v2;
        layout[`xaxis${index + 1}`] = chartAxis("Correlation with mis-ranked pairs", {zeroline: true, zerolinecolor: CHART_INK.secondary});
        layout[`yaxis${index + 1}`] = chartAxis("", {showgrid: false, autorange: "reversed", showticklabels: index === 0});
        layout.annotations = [...(layout.annotations ?? []), {text: `${dataset}: ${percent(v2.wrong)} of pairs wrong in v2, ${percent(v3.wrong)} in v3`, xref: `x${index + 1} domain`, yref: `y${index + 1} domain`, x: 0, y: 1.06, showarrow: false, xanchor: "left", font: {color: CHART_INK.primary, size: 12}}];
    });
    return {data: traces, layout};
}

function anthologyFigure(data) {
    const maps = data.compare.anthology.maps.filter((map) => map.implied_v3 != null);
    const labels = maps.map((map) => map.version);
    return {
        data: [
            {x: maps.flatMap((map) => [map.implied_v3, map.v3, null]), y: labels.flatMap((label) => [label, label, null]), mode: "lines", line: {color: CHART_INK.axis, width: 2}, hoverinfo: "skip", showlegend: false},
            {x: maps.map((map) => map.v2), y: labels, mode: "markers", name: "Nascent v2", marker: {color: V3_COLORS.v2, size: 8, opacity: 0.8}, hovertemplate: "%{y}: v2 %{x:.1f}<extra></extra>"},
            {x: maps.map((map) => map.v3), y: labels, mode: "markers", name: "Nascent v3", marker: {color: V3_COLORS.v3, size: 9, symbol: "diamond", line: {color: CHART_INK.surface, width: 2}}, hovertemplate: "%{y}: v3 %{x:.1f}<extra></extra>"},
            {x: maps.map((map) => map.implied_v3), y: labels, mode: "markers", name: "What scores imply (v3 scale)", marker: {color: CHART_INK.primary, size: 8, symbol: "line-ns-open", line: {width: 2}}, hovertemplate: "%{y}: scores imply %{x:.1f}<extra></extra>"},
        ],
        layout: chartLayout({margin: {l: 64, r: 24, t: 24, b: 48}, xaxis: chartAxis("Displayed difficulty", {rangemode: "tozero"}), yaxis: chartAxis("", {showgrid: false, autorange: "reversed", tickfont: {color: CHART_INK.muted, size: 11}})}),
    };
}

const VIBRO_PARTS = ["jack_fast", "jack_long", "jack_vfast", "vibro_fast"];
const FULL_VIBRO = "Full vibro note (all four jack multipliers)";

function rowMultiplierFigure(data) {
    const multipliers = data.v3.row_multipliers;
    const entries = Object.entries(multipliers).map(([name, value]) => [ROW_LABELS[name] ?? name, value]);
    if (VIBRO_PARTS.every((name) => name in multipliers)) {
        entries.push([FULL_VIBRO, VIBRO_PARTS.reduce((product, name) => product * multipliers[name], 1)]);
    }
    entries.sort((a, b) => a[1] - b[1]);
    const labels = entries.map(([label]) => label);
    const combined = entries.map(([label]) => label === FULL_VIBRO);
    return {
        data: [
            {x: entries.flatMap(([, value]) => [1, value, null]), y: labels.flatMap((label) => [label, label, null]), mode: "lines", line: {color: CHART_INK.axis, width: 3}, hoverinfo: "skip", showlegend: false},
            {x: entries.map(([, value]) => value), y: labels, mode: "markers", marker: {color: entries.map(([, value]) => (value < 1 ? EASIER : HARDER)), symbol: combined.map((full) => (full ? "diamond" : "circle")), size: combined.map((full) => (full ? 14 : 11)), line: {color: CHART_INK.surface, width: 2}}, hovertemplate: "%{y}: strain x%{x:.3f}<extra></extra>", showlegend: false},
        ],
        layout: chartLayout({margin: {l: 64, r: 24, t: 16, b: 52}, shapes: [{type: "line", x0: 1, x1: 1, yref: "paper", y0: 0, y1: 1, line: {color: CHART_INK.secondary, width: 1, dash: "dot"}}], xaxis: chartAxis("Strain multiplier on those notes (1 = unchanged, log scale)", {type: "log"}), yaxis: chartAxis("", {showgrid: false})}),
    };
}

function rowAdjustmentFigure(data) {
    const runs = Object.keys(data.v3.row_adjustments);
    const names = Object.keys(data.v3.row_multipliers);
    return {
        data: runs.map((run, index) => ({type: "bar", orientation: "h", name: DATASET_LABELS[run] ?? run, x: names.map((name) => (data.v3.row_adjustments[run][name] ?? 1) - 1), y: names.map((name) => ROW_LABELS[name] ?? name), marker: {color: SERIES[index % SERIES.length]}, customdata: names.map((name) => data.v3.row_adjustments[run][name] ?? 1), hovertemplate: `${DATASET_LABELS[run] ?? run}: %{y} x%{customdata:.3f} during training<extra></extra>`})),
        layout: chartLayout({barmode: "group", bargap: 0.2, margin: {l: 64, r: 24, t: 48, b: 52}, xaxis: chartAxis("Extra multiplier for this dataset during training, minus 1", {tickformat: "+.0%", zeroline: true, zerolinecolor: CHART_INK.secondary}), yaxis: chartAxis("", {showgrid: false, autorange: "reversed"})}),
    };
}

function releaseFigure(data) {
    return {
        data: [{x: data.v3.release_length.hold_ms, y: data.v3.release_length.weight, mode: "lines+markers", line: {color: V3_COLORS.v3, width: 2}, marker: {size: 8}, hovertemplate: "A %{x:.0f} ms hold's release counts x%{y:.2f}<extra></extra>"}],
        layout: chartLayout({showlegend: false, xaxis: chartAxis("Hold length (ms)", {type: "log"}), yaxis: chartAxis("How much the release counts", {rangemode: "tozero"})}),
    };
}

function vibroRatioFigure(data) {
    const labels = {knee: "Fast jack speed (knees)", cap: "Jack speed up to a cap", power: "Jack speed (powers)", hand: "Hand speed in jack runs"};
    const entries = Object.entries(data.v3.vibro_ratios).filter(([, value]) => value != null);
    return {
        data: [{type: "bar", x: entries.map(([kind]) => labels[kind] ?? kind), y: entries.map(([, value]) => value), marker: {color: V3_COLORS.v3}, hovertemplate: "%{x}: a long run counts x%{y:.3f} of a short run<extra></extra>"}],
        layout: chartLayout({showlegend: false, bargap: 0.35, xaxis: chartAxis("", {showgrid: false}), yaxis: chartAxis("Long run ÷ short run", {range: [0, 1.05]})}),
    };
}

function jackFigureV3(data) {
    const gaps = data.jack_response.gap_ms;
    const bpm = gaps.map((gap) => 15000 / gap);
    const runs = [["short", "One finger, short runs"], ["long", "One finger, long runs (12+)"], ["chord_short", "Chorded, short runs"], ["chord_long", "Chorded, long runs"]];
    const colors = [SERIES[0], SERIES[1], SERIES[2], "#b45fd0"];
    return {
        data: runs.map(([key, name], index) => ({x: gaps, y: data.jack_response[key], customdata: bpm, mode: "lines", line: {color: colors[index], width: 2, dash: key.startsWith("chord") ? "dash" : "solid"}, name, hovertemplate: `${name}, %{x:.0f} ms (1/4 at %{customdata:.0f} BPM): %{y:.4f}<extra></extra>`})),
        layout: chartLayout({xaxis: chartAxis("Time since the previous note in the same column (ms)"), yaxis: chartAxis("Weighted jack difficulty per note", {type: "log"})}),
    };
}

function newShareFigure(data) {
    const entries = Object.entries(data.v3.new_share).sort((a, b) => b[1] - a[1]);
    return {
        data: [{type: "bar", x: entries.map(([family]) => NEW_FAMILY_LABELS[family] ?? family), y: entries.map(([, share]) => share * 100), marker: {color: V3_COLORS.v3}, hovertemplate: "%{x}: %{y:.2f}% of difficulty on a typical chart<extra></extra>"}],
        layout: chartLayout({showlegend: false, bargap: 0.35, xaxis: chartAxis("", {showgrid: false}), yaxis: chartAxis("Share of difficulty on an average osu! 4K chart (%)", {rangemode: "tozero"})}),
    };
}

function fairnessRows(data) {
    const fairness = data.compare.fairness;

    if (!fairness) {
        return [];
    }

    return Object.entries(fairness).flatMap(([dataset, result]) => ["Nascent", "Nascent v2", "Etterna MSD"].map((calculator) => ({dataset: dataset === "osuapi" ? "osu! 4K" : "Etterna 4K", calculator: calculator === "Nascent" ? "Nascent v3" : calculator, ...result.overrating[calculator]})));
}

function weightHeatmapsV3(data) {
    const maps = weightHeatmaps(data);
    const curves = data.weights.filter((row) => row.family === "jack curve").map((row) => ({...row, column: `${row.timescale}s`}));
    const groups = [...new Set(curves.map((row) => row.group))];
    return maps.map((map) => (map.label === "Jack speed curves" ? {label: map.label, ...weightHeatmap(curves, groups, data.timescales.map((value) => `${value}s`), "Memory timescale", 980)} : map));
}

function diffv3Figures(source) {
    const data = v3Data(source);
    return {
        display: displayCurveFigure(data),
        rates: ratesFigure(data),
        rateDelta: rateDeltaFigure(data),
        dans: danCompareFigure(data),
        skillDans: data.compare.skill_dans ? skillDanFigure(data) : null,
        skillDanRows: data.compare.skill_dans ? skillDanRows(data) : null,
        loss: lossFigure(data),
        mix: mixFigure(data),
        covariates: covariateFigures(data),
        jack: jackFigureV3(data),
        contribution: contributionFigure(data),
        heatmaps: weightHeatmapsV3(data),
        factors: factorFigure(data),
        scatter: scatterFigure(data, "msd", "msd", "Etterna MSD", data.model.msd_zeta, "Scaled MSD Reference"),
        osuScatter: scatterFigure(data, "stars", "osu", "osu! star rating", data.model.osu_zeta, "Scaled star rating reference"),
        versions: versionScatter(data),
        scores: listFigure4(data.scores, "Difficulty from scores"),
        calcTest: listFigure4(data.calc_test, "Target MSD"),
        calcSkills: skillFigure(data.calc_test),
        community: listFigure4(data.community, "Community rating"),
        benchmarks: benchmarkRowsV3(data),
        reserved: reservedFigure(data),
        reservedRows: reservedRows(data),
        fairness: fairnessRows(data),
        anthology: anthologyFigure(data),
        movers: moversFigure(data.compare.movers.biggest),
        moversHeight: 120 + 22 * data.compare.movers.biggest.length,
        histogram: histogramFigure(data),
        patterns: patternFigures(data),
        pairPatterns: pairPatternFigure(data),
        rows: rowMultiplierFigure(data),
        rowAdjustments: rowAdjustmentFigure(data),
        release: releaseFigure(data),
        vibro: vibroRatioFigure(data),
        newShare: newShareFigure(data),
    };
}
