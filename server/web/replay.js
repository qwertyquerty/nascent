const HIT_SCORES = {
    PERFECT: {color: "rgb(255, 221, 68)", weight: 100},
    AMAZING: {color: "rgb(204, 153, 0)", weight: 95},
    GREAT: {color: "rgb(51, 204, 51)", weight: 70},
    GOOD: {color: "rgb(0, 102, 255)", weight: 40},
    BAD: {color: "rgb(204, 204, 204)", weight: 10},
    MISS: {color: "rgb(255, 0, 0)", weight: 0},
};

const LOGO_START = [0, 177, 225];
const LOGO_END = [0, 245, 154];
const MISS_DELAY_MS = 160;
const LEAD_IN_MS = 1500;
const JUDGEMENT_SHOW_MS = 500;
const TIMING_HISTORY_MS = 3000;

function laneColor(lane, keyCount) {
    const t = keyCount > 1 ? lane / (keyCount - 1) : 0.5;
    return `rgb(${LOGO_START.map((start, index) => Math.round(start + (LOGO_END[index] - start) * t)).join(", ")})`;
}

function scoreName(score) {
    return String(score || "").toUpperCase();
}

function prepareTimeline(timeline) {
    const keyCount = timeline.key_count;
    const notes = [...timeline.notes].sort((a, b) => a.time - b.time);
    const held = Array.from({length: keyCount}, () => []);
    const pressedAt = new Array(keyCount).fill(null);

    for (const [time, lane, pressed] of [...timeline.inputs].sort((a, b) => a[0] - b[0])) {
        if (lane < 0 || lane >= keyCount) {
            continue;
        }

        if (pressed && pressedAt[lane] === null) {
            pressedAt[lane] = time;
        }
        else if (!pressed && pressedAt[lane] !== null) {
            held[lane].push([pressedAt[lane], time]);
            pressedAt[lane] = null;
        }
    }

    pressedAt.forEach((start, lane) => start !== null && held[lane].push([start, Infinity]));

    const events = [];

    for (const note of notes) {
        const hit = scoreName(note.hit_score);

        if (HIT_SCORES[hit]) {
            events.push({time: note.hit_played ? note.time + note.hit_error : note.time + MISS_DELAY_MS, score: hit, error: note.hit_played ? note.hit_error : null});
        }

        const release = scoreName(note.release_score);

        if (note.hold && HIT_SCORES[release]) {
            events.push({time: note.release_played ? note.end_time + note.release_error : note.end_time + MISS_DELAY_MS, score: release, error: null});
        }
    }

    events.sort((a, b) => a.time - b.time);
    let combo = 0;
    let weights = 0;

    events.forEach((event, index) => {
        combo = event.score === "MISS" ? 0 : combo + 1;
        weights += HIT_SCORES[event.score].weight;
        event.combo = combo;
        event.accuracy = weights / (index + 1) / 100;
    });

    const longestHold = notes.reduce((longest, note) => Math.max(longest, note.hold ? note.end_time - note.time : 0), 0);
    const first = notes.length ? notes[0].time : 0;
    return {keyCount, notes, held, events, longestHold, start: Math.min(0, first - LEAD_IN_MS), end: Math.max(timeline.end_time, notes.length ? notes[notes.length - 1].end_time : 0) + 500};
}

function lastIndexAtOrBefore(items, time, key = (item) => item.time) {
    let low = 0;
    let high = items.length - 1;
    let found = -1;

    while (low <= high) {
        const middle = (low + high) >> 1;

        if (key(items[middle]) <= time) {
            found = middle;
            low = middle + 1;
        }
        else {
            high = middle - 1;
        }
    }

    return found;
}

function isHeld(intervals, time) {
    const index = lastIndexAtOrBefore(intervals, time, (interval) => interval[0]);
    return index >= 0 && intervals[index][1] > time;
}

const ReplayViewer = {
    props: {timeline: Object, accuracy: Number},
    data: () => ({playing: false, position: 0, speed: 1, scroll: 1.2}),
    computed: {
        prepared: (self) => prepareTimeline(self.timeline),
        duration: (self) => self.prepared.end - self.prepared.start,
        clock: (self) => {
            const seconds = Math.max(0, Math.floor((self.position - Math.max(0, self.prepared.start)) / 1000));
            return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
        },
    },
    mounted() {
        this.position = this.prepared.start;
        this.resize = () => this.layout();
        addEventListener("resize", this.resize);
        this.layout();
    },
    unmounted() {
        cancelAnimationFrame(this.frame);
        removeEventListener("resize", this.resize);
    },
    methods: {
        layout() {
            const canvas = this.$refs.canvas;
            const ratio = devicePixelRatio || 1;
            const width = Math.min(canvas.parentElement.clientWidth, this.prepared.keyCount * 72 + 48);
            const height = Math.min(520, Math.max(320, innerHeight * 0.6));
            canvas.style.width = `${width}px`;
            canvas.style.height = `${height}px`;
            canvas.width = Math.round(width * ratio);
            canvas.height = Math.round(height * ratio);
            this.draw();
        },
        toggle() {
            this.playing ? this.pause() : this.play();
        },
        play() {
            if (this.position >= this.prepared.end) {
                this.position = this.prepared.start;
            }

            this.playing = true;
            this.last = performance.now();
            this.frame = requestAnimationFrame(this.tick);
        },
        pause() {
            this.playing = false;
            cancelAnimationFrame(this.frame);
        },
        tick(now) {
            this.position = Math.min(this.prepared.end, this.position + (now - this.last) * this.speed);
            this.last = now;
            this.draw();

            if (this.position >= this.prepared.end) {
                this.pause();
                return;
            }

            this.frame = requestAnimationFrame(this.tick);
        },
        seek(event) {
            this.position = Number(event.target.value);
            this.draw();
        },
        draw() {
            const canvas = this.$refs.canvas;

            if (!canvas) {
                return;
            }

            const ratio = devicePixelRatio || 1;
            const context = canvas.getContext("2d");

            if (!context) {
                return;
            }

            const width = canvas.width / ratio;
            const height = canvas.height / ratio;
            const {keyCount, notes, held, events, longestHold} = this.prepared;
            const time = this.position;
            const lane = (width - 24) / keyCount;
            const left = 12;
            const judgeY = height - 70;
            const pxPerMs = (height / 1000) * this.scroll;
            const noteHeight = Math.max(10, lane * 0.28);

            context.setTransform(ratio, 0, 0, ratio, 0, 0);
            context.fillStyle = "#0b0e12";
            context.fillRect(0, 0, width, height);

            for (let index = 0; index < keyCount; index++) {
                context.fillStyle = index % 2 ? "rgba(255, 255, 255, 0.025)" : "rgba(255, 255, 255, 0.045)";
                context.fillRect(left + index * lane, 0, lane, height);
            }

            for (let index = 0; index < keyCount; index++) {
                const x = left + index * lane + 3;
                context.globalAlpha = isHeld(held[index], time) ? 0.9 : 0.25;
                context.fillStyle = laneColor(index, keyCount);
                context.fillRect(x, judgeY - noteHeight / 2, lane - 6, noteHeight);
            }

            context.globalAlpha = 1;
            const visibleUntil = time + judgeY / pxPerMs;
            const first = Math.max(0, lastIndexAtOrBefore(notes, time - longestHold - 400));

            for (let index = first; index < notes.length && notes[index].time <= visibleUntil; index++) {
                const note = notes[index];
                const pressTime = note.hit_played ? note.time + note.hit_error : null;
                const releaseTime = note.hold ? (note.release_played ? note.end_time + note.release_error : note.end_time) : pressTime;
                const x = left + note.lane * lane + 3;
                const color = laneColor(note.lane, keyCount);
                const headY = judgeY - (note.time - time) * pxPerMs;

                if (pressTime !== null && time >= (note.hold ? releaseTime : pressTime)) {
                    continue;
                }

                const missed = pressTime === null && time > note.time + MISS_DELAY_MS;
                context.globalAlpha = missed ? 0.35 : 1;
                context.fillStyle = color;

                if (note.hold) {
                    const holding = pressTime !== null && time >= pressTime;
                    const tailY = judgeY - (note.end_time - time) * pxPerMs;
                    const bodyBottom = holding ? judgeY : headY;
                    context.globalAlpha *= 0.45;
                    context.fillRect(x + lane * 0.18, tailY, lane - 6 - lane * 0.36, Math.max(0, bodyBottom - tailY));
                    context.globalAlpha = missed ? 0.35 : 1;
                    context.fillRect(x, (holding ? judgeY : headY) - noteHeight / 2, lane - 6, noteHeight);
                }
                else {
                    context.fillRect(x, headY - noteHeight / 2, lane - 6, noteHeight);
                }
            }

            context.globalAlpha = 1;
            const latest = lastIndexAtOrBefore(events, time);
            const event = latest >= 0 ? events[latest] : null;
            context.textAlign = "center";
            context.font = "400 26px Tomorrow, sans-serif";

            if (event && time - event.time < JUDGEMENT_SHOW_MS) {
                const fade = 1 - (time - event.time) / JUDGEMENT_SHOW_MS;
                context.globalAlpha = Math.min(1, fade * 2);
                context.fillStyle = HIT_SCORES[event.score].color;
                context.fillText(event.score, width / 2, height * 0.42);
            }

            context.globalAlpha = 1;

            if (event && event.combo > 0) {
                context.fillStyle = "#ffffff";
                context.font = "400 34px Tomorrow, sans-serif";
                context.fillText(String(event.combo), width / 2, height * 0.42 + 42);
            }

            context.textAlign = "left";
            context.font = "400 15px Tomorrow, sans-serif";
            context.fillStyle = "#ffffff";
            context.fillText(`${((event ? event.accuracy : 1) * 100).toFixed(2)}%`, 10, 22);

            const barWidth = Math.min(width - 40, 260);
            const barX = (width - barWidth) / 2;
            const barY = height - 26;
            const range = 180;
            context.fillStyle = "rgba(255, 255, 255, 0.08)";
            context.fillRect(barX, barY - 7, barWidth, 14);
            context.fillStyle = "rgba(255, 255, 255, 0.5)";
            context.fillRect(width / 2 - 1, barY - 9, 2, 18);

            for (let index = latest; index >= 0 && time - events[index].time < TIMING_HISTORY_MS; index--) {
                const past = events[index];

                if (past.error === null) {
                    continue;
                }

                context.globalAlpha = 1 - (time - past.time) / TIMING_HISTORY_MS;
                context.fillStyle = HIT_SCORES[past.score].color;
                context.fillRect(width / 2 + (Math.max(-range, Math.min(range, past.error)) / range) * (barWidth / 2) - 1, barY - 7, 2, 14);
            }

            context.globalAlpha = 1;
        },
        key(event) {
            if (event.code === "Space") {
                event.preventDefault();
                this.toggle();
            }
        },
    },
    watch: {
        scroll() {
            this.draw();
        },
    },
    template: `<div class="replay-viewer">
        <div class="replay-stage">
            <canvas ref="canvas" tabindex="0" role="img" aria-label="Replay of this play" @click="toggle" @keydown="key"></canvas>
        </div>
        <div class="replay-controls">
            <button type="button" class="button is-link" @click="toggle" :aria-label="playing ? 'Pause replay' : 'Play replay'">{{ playing ? "Pause" : "Play" }}</button>
            <input class="replay-seek" type="range" :min="prepared.start" :max="prepared.end" step="1" :value="position" @input="seek" aria-label="Replay position">
            <span class="replay-clock">{{ clock }}</span>
            <div class="select is-small">
                <select v-model.number="speed" aria-label="Playback speed">
                    <option v-for="option in [0.25, 0.5, 1, 1.5, 2]" :value="option">{{ option }}x</option>
                </select>
            </div>
            <div class="select is-small">
                <select v-model.number="scroll" aria-label="Scroll speed">
                    <option v-for="option in [0.8, 1.2, 1.6, 2.2]" :value="option">Scroll {{ option }}</option>
                </select>
            </div>
        </div>
    </div>`,
};

const HitErrorGraph = {
    props: {timeline: Object},
    computed: {
        prepared: (self) => prepareTimeline(self.timeline),
        points: (self) => self.prepared.events.filter((event) => event.error !== null),
        misses: (self) => self.prepared.events.filter((event) => event.score === "MISS"),
        range: (self) => Math.max(160, ...self.points.map((event) => Math.abs(event.error))),
    },
    methods: {
        x(time) {
            const {start, end} = this.prepared;
            return ((time - Math.max(0, start)) / Math.max(1, end - Math.max(0, start))) * 1000;
        },
        y(error) {
            return 60 + (error / this.range) * 56;
        },
        color(score) {
            return HIT_SCORES[score].color;
        },
    },
    template: `<figure class="hit-error-graph">
        <svg viewBox="0 0 1000 120" preserveAspectRatio="none" role="img" :aria-label="'Hit timing over the song, ' + points.length + ' timed hits and ' + misses.length + ' misses'">
            <rect x="0" y="0" width="1000" height="120" class="graph-background"></rect>
            <line x1="0" x2="1000" y1="60" y2="60" class="graph-center"></line>
            <line v-for="miss in misses" :x1="x(miss.time)" :x2="x(miss.time)" y1="0" y2="120" class="graph-miss"></line>
            <circle v-for="point in points" :cx="x(point.time)" :cy="y(point.error)" r="1.8" :fill="color(point.score)"></circle>
        </svg>
        <figcaption class="is-size-7 has-text-grey">Hit timing over the song: early above the line, late below, misses in red</figcaption>
    </figure>`,
};
