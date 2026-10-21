# nascent

```
pip install nascent
```

```python
import nascent

for rating in nascent.difficulty("song.sm", rate=1.2):
    print(rating.chart.difficulty_name, rating.difficulty)

for curve in nascent.difficulty_rates("chart.osu"):
    print(curve[0.75], curve[1.0], curve[1.5])
```

```python
for chart in nascent.load("beatmap.osz"):
    print(chart.artist, chart.title, chart.difficulty_name, chart.key_count, chart.bpm)

    for note in chart.notes_at(1.5):
        print(note.lane, note.time, note.end_time, note.is_hold)

charts = nascent.load(data, format="stepmania")
```

```python
notes = [nascent.Note(lane=0, time=1000), nascent.Note(lane=2, time=1250, end_time=2000)]
chart = nascent.Chart.create(notes, key_count=4, timing_points=[nascent.TimingPoint(0, bpm=240)])
print(chart.difficulty())
```

```python
print(nascent.judge(-18), nascent.judgement_windows())

attempt = nascent.Attempt(chart, rate=1.0)
attempt.press(0, 1004)
attempt.release(0, 1060)
attempt.update(3000)
print(attempt.accuracy, attempt.result().grade)

replay = attempt.replay()
result = nascent.simulate(chart, nascent.Replay.from_json(replay.to_json()))
print(result.accuracy, nascent.performance(chart.difficulty(), result.accuracy))
```
