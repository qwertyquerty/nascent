from dataclasses import dataclass
import csv

HIT_TYPE_CIRCLE = (1<<0)
HIT_TYPE_HOLD = (1<<7)

MODE_MANIA = 3

@dataclass
class Event():
    event_type: str
    start_time: int
    event_params: tuple

@dataclass
class TimingPoint():
    time: int
    beat_length: float
    meter: int
    sample_set: int
    sample_index: int
    volume: int
    uninherited: int
    effects: int

@dataclass
class ManiaHitObject():
    key: int
    time: int
    hit_type: int
    end_time: int

class Map():
    NO_SECTION = 0
    GENERAL = 1
    METADATA = 2
    DIFFICULTY = 3
    EVENTS = 4
    TIMING_POINTS = 5
    HIT_OBJECTS = 6

    SECTION_NAMES = {
        "[General]": GENERAL,
        "[Metadata]": METADATA,
        "[Difficulty]": DIFFICULTY,
        "[Events]": EVENTS,
        "[TimingPoints]": TIMING_POINTS,
        "[HitObjects]": HIT_OBJECTS,
        "[Editor]": NO_SECTION,
        "[Colours]": NO_SECTION
    }

    def __init__(self, filename):
        self.general = {}
        self.metadata = {}
        self.difficulty = {}
        self.events = []
        self.timing_points = []
        self.hit_objects = []
        with open(filename, "r", encoding="utf-8") as f:
            s = f.read()
        
        lines = s.splitlines()
        
        state = Map.NO_SECTION

        for line in lines:
            if line.startswith("//"):
                continue

            elif line in Map.SECTION_NAMES:
                state = Map.SECTION_NAMES[line]

            elif len(line) and state != Map.NO_SECTION:
                if state in (Map.GENERAL, Map.METADATA, Map.DIFFICULTY):
                    k = line.split(":",1)[0].strip()
                    v = line.split(":",1)[1].strip()

                    if state == Map.GENERAL and k == "Mode":
                        assert(int(v) == MODE_MANIA)

                    (self.general, self.metadata, self.difficulty)[state - Map.GENERAL][k] = v

                elif state in (Map.EVENTS, Map.TIMING_POINTS, Map.HIT_OBJECTS):
                    tokens = list(csv.reader([line]))[0]

                    if state == Map.EVENTS:
                        self.events.append(Event(
                            tokens.pop(0),
                            int(tokens.pop(0)),
                            tuple(tokens)
                        ))

                    elif state == Map.TIMING_POINTS:
                        self.timing_points.append(TimingPoint(
                            int(float(tokens.pop(0))),
                            float(tokens.pop(0)),
                            int(tokens.pop(0)),
                            int(tokens.pop(0)),
                            int(tokens.pop(0)),
                            int(tokens.pop(0)),
                            int(tokens.pop(0)),
                            int(tokens.pop(0))
                        ))
                    
                    elif state == Map.HIT_OBJECTS:
                        key_count = int(self.difficulty["CircleSize"])

                        key = (int(tokens.pop(0)) * key_count) // 512

                        _ = tokens.pop(0) # y

                        time = int(tokens.pop(0))

                        hit_type = int(tokens.pop(0))

                        _ = int(tokens.pop(0)) # hit sound

                        end_time = None
                        if hit_type & HIT_TYPE_HOLD:
                            end_time = int(tokens.pop(0).split(":")[0])

                        self.hit_objects.append(ManiaHitObject(
                            key,
                            time,
                            hit_type,
                            end_time
                        ))
