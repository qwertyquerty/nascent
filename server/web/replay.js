const HIT_SCORES = {
    PERFECT: {color: "rgb(255, 221, 68)", weight: 100},
    AMAZING: {color: "rgb(204, 153, 0)", weight: 95},
    GREAT: {color: "rgb(51, 204, 51)", weight: 70},
    GOOD: {color: "rgb(0, 102, 255)", weight: 40},
    BAD: {color: "rgb(204, 204, 204)", weight: 10},
    MISS: {color: "rgb(255, 0, 0)", weight: 0},
};

const MISS_DELAY_MS = 160;
const LEAD_IN_MS = 1500;
const JUDGEMENT_SHOW_MS = 500;
const TIMING_HISTORY_MS = 3000;
const SEEK_STEP_MS = 5000;
const SIDE_COLUMN_PX = 256;
const SIDE_GAP_PX = 24;

const SKIN_FILES = {
    notes: [1, 2, 3, 4, 5, 6, 7].map((index) => `/skins/default/note_${index}.png`),
    gradient: "/skins/default/title_gradient.png",
    body: "/skins/arrows_holds/hold_body.png",
    cap: "/skins/arrows_holds/hold_cap.png",
};
const KEY_SPACING = 0.15;
const JUDGE_HEIGHT = 0.06;
const RECEPTOR_ALPHA = 64 / 255;
const HOLD_EDGE_INSET = 0.5;
const TINT_CACHE_LIMIT = 800;

let skinPromise = null;

function loadImage(source) {
    return new Promise((resolve) => {
        const image = new Image();
        image.onload = () => resolve(image);
        image.onerror = () => resolve(null);
        image.src = source;
    });
}

function readPixels(image) {
    if (!image) {
        return null;
    }

    const canvas = document.createElement("canvas");
    canvas.width = image.naturalWidth;
    canvas.height = image.naturalHeight;
    const context = canvas.getContext("2d", {willReadFrequently: true});

    if (!context) {
        return null;
    }

    context.drawImage(image, 0, 0);
    return {width: canvas.width, height: canvas.height, data: context.getImageData(0, 0, canvas.width, canvas.height).data};
}

function loadSkin() {
    skinPromise ??= Promise.all([
        Promise.all(SKIN_FILES.notes.map(loadImage)),
        loadImage(SKIN_FILES.gradient),
        loadImage(SKIN_FILES.body),
        loadImage(SKIN_FILES.cap),
    ]).then(([notes, gradient, body, cap]) => ({notes, gradient: readPixels(gradient), body, cap, cache: new Map()}));

    return skinPromise;
}

function gradientAt(skin, x, y) {
    const gradient = skin?.gradient;

    if (!gradient || !gradient.width || !gradient.height) {
        return [255, 255, 255];
    }

    const px = Math.floor(Math.min(1, Math.max(0, x)) * (gradient.width - 1));
    const py = Math.floor(Math.min(1, Math.max(0, y)) * (gradient.height - 1));
    const offset = (py * gradient.width + px) * 4;
    return [gradient.data[offset], gradient.data[offset + 1], gradient.data[offset + 2]];
}

function tinted(skin, image, color, width, height) {
    if (!skin || !image) {
        return null;
    }

    const quantized = color.map((channel) => Math.min(255, Math.round(channel / 6) * 6));
    const key = `${image.src}|${quantized.join(",")}|${width}x${height}`;
    let canvas = skin.cache.get(key);

    if (canvas) {
        return canvas;
    }

    canvas = document.createElement("canvas");
    canvas.width = width;
    canvas.height = height;
    const context = canvas.getContext("2d");

    if (!context) {
        return null;
    }

    context.drawImage(image, 0, 0, width, height);
    context.globalCompositeOperation = "multiply";
    context.fillStyle = `rgb(${quantized.join(", ")})`;
    context.fillRect(0, 0, width, height);
    context.globalCompositeOperation = "destination-in";
    context.drawImage(image, 0, 0, width, height);

    if (skin.cache.size >= TINT_CACHE_LIMIT) {
        skin.cache.clear();
    }

    skin.cache.set(key, canvas);
    return canvas;
}

function scoreName(score) {
    return String(score || "").toUpperCase();
}

function formatClock(ms) {
    const seconds = Math.max(0, Math.floor(ms / 1000));
    return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
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
    props: {timeline: Object, accuracy: Number, downloadUrl: String},
    data: () => ({playing: false, position: 0, speed: 1, scroll: 200}),
    computed: {
        prepared: (self) => prepareTimeline(self.timeline),
        duration: (self) => self.prepared.end - self.prepared.start,
        clock: (self) => `${formatClock(self.position - Math.max(0, self.prepared.start))} / ${formatClock(self.prepared.end - Math.max(0, self.prepared.start))}`,
    },
    mounted() {
        this.position = this.prepared.start;
        this.resize = () => this.layout();
        addEventListener("resize", this.resize);
        this.layout();
        loadSkin().then((skin) => {
            this.skin = skin;
            this.draw();
        });
    },
    unmounted() {
        cancelAnimationFrame(this.frame);
        removeEventListener("resize", this.resize);
    },
    methods: {
        layout() {
            const canvas = this.$refs.canvas;
            const ratio = devicePixelRatio || 1;
            const stacked = matchMedia("(max-width: 768px)").matches;
            const available = this.$el.clientWidth - (stacked ? 0 : SIDE_COLUMN_PX + SIDE_GAP_PX);
            const width = Math.max(160, Math.min(available, this.prepared.keyCount * 72 + 48));
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
        seekTo(time) {
            this.position = Math.min(this.prepared.end, Math.max(this.prepared.start, time));
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

            const skin = this.skin;
            const width = canvas.width / ratio;
            const height = canvas.height / ratio;
            const {keyCount, notes, held, events, longestHold} = this.prepared;
            const time = this.position;
            const fieldX = 12;
            const fieldWidth = width - fieldX * 2;
            const noteSize = fieldWidth / (keyCount + keyCount * KEY_SPACING - KEY_SPACING);
            const laneStep = noteSize * (1 + KEY_SPACING);
            const judgeY = height - noteSize - height * JUDGE_HEIGHT;
            const pxPerMs = (height / 100000) * this.scroll;
            const spritePx = Math.max(1, Math.round(noteSize * ratio));
            const colorAt = (x, y) => gradientAt(skin, x / width, y / height);

            const square = (image, x, y, color, alpha) => {
                const sprite = tinted(skin, image, color, spritePx, spritePx);
                context.globalAlpha = alpha;

                if (sprite) {
                    context.drawImage(sprite, x, y, noteSize, noteSize);
                }
                else {
                    context.fillStyle = `rgb(${color.join(", ")})`;
                    context.fillRect(x + noteSize * 0.1, y + noteSize * 0.35, noteSize * 0.8, noteSize * 0.3);
                }
            };

            const holdBody = (x, top, bottom, color) => {
                const body = skin?.body;
                const sprite = body ? tinted(skin, body, color, spritePx, body.naturalHeight) : null;
                context.globalAlpha = 1;

                if (!sprite) {
                    context.globalAlpha = 0.5;
                    context.fillStyle = `rgb(${color.join(", ")})`;
                    context.fillRect(x + noteSize * 0.2, top, noteSize * 0.6, bottom - top);
                    return;
                }

                const inset = sprite.height > HOLD_EDGE_INSET * 2 ? HOLD_EDGE_INSET : 0;
                context.save();
                context.translate(x, bottom);
                context.scale(1, -1);
                context.drawImage(sprite, 0, inset, sprite.width, sprite.height - inset * 2, 0, 0, noteSize, bottom - top);
                context.restore();
            };

            const holdCap = (x, bodyTop, color) => {
                const cap = skin?.cap;

                if (!cap || !cap.naturalWidth) {
                    return;
                }

                const capHeight = noteSize * cap.naturalHeight / cap.naturalWidth;
                const sprite = tinted(skin, cap, color, spritePx, cap.naturalHeight);

                if (!sprite) {
                    return;
                }

                const inset = sprite.height > HOLD_EDGE_INSET ? HOLD_EDGE_INSET : 0;
                context.globalAlpha = 1;
                context.save();
                context.translate(x, bodyTop);
                context.scale(1, -1);
                context.drawImage(sprite, 0, inset, sprite.width, sprite.height - inset, 0, 0, noteSize, capHeight);
                context.restore();
            };

            context.setTransform(ratio, 0, 0, ratio, 0, 0);
            context.globalAlpha = 1;
            context.fillStyle = "#0b0e12";
            context.fillRect(0, 0, width, height);

            for (let lane = 0; lane < keyCount; lane++) {
                const x = fieldX + lane * laneStep;
                const pressed = isHeld(held[lane], time);
                square(skin?.notes[lane % 7], x, judgeY, gradientAt(skin, (x - fieldX) / fieldWidth, judgeY / height), pressed ? 1 : RECEPTOR_ALPHA);
            }

            const first = Math.max(0, lastIndexAtOrBefore(notes, time - longestHold - height / pxPerMs));

            for (let index = first; index < notes.length; index++) {
                const note = notes[index];
                const x = fieldX + note.lane * laneStep;
                const screenY = judgeY - (note.time - time) * pxPerMs;

                if (screenY + noteSize < 0) {
                    break;
                }

                const holdPx = note.hold ? (note.end_time - note.time) * pxPerMs : 0;

                if (screenY - holdPx >= height) {
                    continue;
                }

                const pressTime = note.hit_played ? note.time + note.hit_error : null;
                const hitPlayed = pressTime !== null && time >= pressTime;
                const releaseTime = note.hold ? (note.release_played ? note.end_time + note.release_error : note.end_time) : null;
                const released = note.hold && hitPlayed && time >= releaseTime;
                const holding = note.hold && hitPlayed && !released;
                const dropped = released && note.release_played && note.release_error < 0 && scoreName(note.release_score) === "MISS";
                const cutY = dropped ? judgeY - (releaseTime - time) * pxPerMs : judgeY;
                const image = skin?.notes[note.lane % 7];
                const color = colorAt(x, screenY);

                if (note.hold) {
                    const tailY = screenY - holdPx + noteSize / 2;
                    let bodyBottom = screenY + noteSize / 2;

                    if (hitPlayed) {
                        bodyBottom = Math.min(bodyBottom, cutY + noteSize / 2);
                    }

                    bodyBottom = Math.min(bodyBottom, height);

                    if (bodyBottom > tailY) {
                        holdBody(x, tailY, bodyBottom, color);
                        holdCap(x, tailY, color);
                    }
                }

                if (holding) {
                    square(image, x, Math.min(screenY, judgeY), colorAt(x, judgeY), 1);
                }
                else if ((!hitPlayed || screenY <= judgeY) && screenY < height) {
                    square(image, x, screenY, color, 1);
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
            const barY = height - height * JUDGE_HEIGHT / 2;
            const range = 180;
            context.fillStyle = "rgba(255, 255, 255, 0.08)";
            context.fillRect(barX, barY - 5, barWidth, 10);
            context.fillStyle = "rgba(255, 255, 255, 0.5)";
            context.fillRect(width / 2 - 1, barY - 7, 2, 14);

            for (let index = latest; index >= 0 && time - events[index].time < TIMING_HISTORY_MS; index--) {
                const past = events[index];

                if (past.error === null) {
                    continue;
                }

                context.globalAlpha = 1 - (time - past.time) / TIMING_HISTORY_MS;
                context.fillStyle = HIT_SCORES[past.score].color;
                context.fillRect(width / 2 + (Math.max(-range, Math.min(range, past.error)) / range) * (barWidth / 2) - 1, barY - 5, 2, 10);
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
        <div class="replay-side">
            <hit-error-graph class="replay-timeline" :timeline="timeline" :position="position" @seek="seekTo"></hit-error-graph>
            <div class="replay-controls">
                <button type="button" class="button is-link" @click="toggle" :aria-label="playing ? 'Pause replay' : 'Play replay'">{{ playing ? "Pause" : "Play" }}</button>
                <span class="replay-clock">{{ clock }}</span>
                <div class="select is-small">
                    <select v-model.number="speed" aria-label="Playback speed">
                        <option v-for="option in [0.25, 0.5, 1, 1.5, 2]" :value="option">{{ option }}x</option>
                    </select>
                </div>
                <div class="select is-small">
                    <select v-model.number="scroll" aria-label="Scroll speed">
                        <option v-for="option in [100, 150, 200, 250, 300]" :value="option">Scroll {{ option }}</option>
                    </select>
                </div>
                <a v-if="downloadUrl" class="button is-small" :href="downloadUrl" download>Download replay file</a>
            </div>
        </div>
    </div>`,
};

const HitErrorGraph = {
    props: {timeline: Object, position: {type: Number, default: null}},
    emits: ["seek"],
    computed: {
        prepared: (self) => prepareTimeline(self.timeline),
        points: (self) => self.prepared.events.filter((event) => event.error !== null),
        misses: (self) => self.prepared.events.filter((event) => event.score === "MISS"),
        range: (self) => Math.max(160, ...self.points.map((event) => Math.abs(event.error))),
        seekable: (self) => self.position !== null,
        playhead: (self) => self.x(Math.min(self.prepared.end, Math.max(self.prepared.start, self.position ?? self.prepared.start))),
        valueText: (self) => formatClock((self.position ?? 0) - Math.max(0, self.prepared.start)),
    },
    methods: {
        x(time) {
            const {start, end} = this.prepared;
            return ((time - start) / Math.max(1, end - start)) * 1000;
        },
        y(error) {
            return 60 + (error / this.range) * 56;
        },
        color(score) {
            return HIT_SCORES[score].color;
        },
        timeAt(event) {
            const bounds = event.currentTarget.getBoundingClientRect();
            const fraction = Math.min(1, Math.max(0, (event.clientX - bounds.left) / Math.max(1, bounds.width)));
            return this.prepared.start + fraction * (this.prepared.end - this.prepared.start);
        },
        press(event) {
            if (!this.seekable || event.button !== 0) {
                return;
            }

            this.dragging = true;
            event.currentTarget.setPointerCapture?.(event.pointerId);
            this.$emit("seek", this.timeAt(event));
        },
        move(event) {
            if (this.dragging) {
                this.$emit("seek", this.timeAt(event));
            }
        },
        release(event) {
            this.dragging = false;
            event.currentTarget.releasePointerCapture?.(event.pointerId);
        },
        key(event) {
            if (!this.seekable) {
                return;
            }

            const targets = {
                ArrowLeft: this.position - SEEK_STEP_MS,
                ArrowRight: this.position + SEEK_STEP_MS,
                Home: this.prepared.start,
                End: this.prepared.end,
            };

            if (event.key in targets) {
                event.preventDefault();
                this.$emit("seek", targets[event.key]);
            }
        },
    },
    template: `<figure class="hit-error-graph" :class="{'is-seekable': seekable}">
        <svg viewBox="0 0 1000 120" preserveAspectRatio="none"
            :role="seekable ? 'slider' : 'img'"
            :tabindex="seekable ? 0 : null"
            :aria-label="seekable ? 'Replay position' : 'Hit timing over the song, ' + points.length + ' timed hits and ' + misses.length + ' misses'"
            :aria-valuemin="seekable ? prepared.start : null"
            :aria-valuemax="seekable ? prepared.end : null"
            :aria-valuenow="seekable ? Math.round(position) : null"
            :aria-valuetext="seekable ? valueText : null"
            @pointerdown="press" @pointermove="move" @pointerup="release" @pointercancel="release" @keydown="key">
            <g v-memo="[prepared, range]">
                <rect x="0" y="0" width="1000" height="120" class="graph-background"></rect>
                <line x1="0" x2="1000" y1="60" y2="60" class="graph-center"></line>
                <line v-for="miss in misses" :x1="x(miss.time)" :x2="x(miss.time)" y1="0" y2="120" class="graph-miss"></line>
                <circle v-for="point in points" :cx="x(point.time)" :cy="y(point.error)" r="1.8" :fill="color(point.score)"></circle>
            </g>
            <rect v-if="seekable" x="0" y="0" :width="playhead" height="120" class="graph-played"></rect>
            <line v-if="seekable" :x1="playhead" :x2="playhead" y1="0" y2="120" class="graph-playhead"></line>
        </svg>
    </figure>`,
};
