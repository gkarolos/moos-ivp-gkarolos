# PID Benchmark Harness — YAW_PID_KP sweep

Runs the self-evaluating stem mission in `../` (`missions/pid_benchmark`) once
per steering-gain value and aggregates the results into one comparison table.

The **stem owns the grade**: `pMissionEval` writes `results.txt` with
`grade=`, `mission_time=` and `total_dist=`. This harness only owns case
selection, port isolation, per-case temp copies, aggregation and cleanup.

## What it measures

The vehicle runs the **same fixed survey path** in every case. The only thing
that changes is `YAW_PID_KP` (the steering proportional gain). On a fixed path:

- **mission_time** — lower = reaches waypoints faster (tighter tracking)
- **total_dist** — lower = less overshoot / fewer loop-backs (measured by your
  own `pOdometry` app)

## Cases (Current Matrix)

| Case   | YAW_PID_KP | Intent                                   |
|--------|-----------:|------------------------------------------|
| `kp03` | 0.3        | Very low gain — sluggish, wide turns     |
| `kp06` | 0.6        | Low gain                                 |
| `kp09` | 0.9        | Stem default / baseline                  |
| `kp12` | 1.2        | High gain — snappier                     |
| `kp15` | 1.5        | Very high gain — risk of overshoot       |

Each case runs in its own temp copy under `.runs/<case>/` with an isolated
MOOSDB/pShare port block, so cases never collide.

## Run

Full sweep (headless):

```bash
./zlaunch.sh --max_time=300 10
```

One case only:

```bash
./zlaunch.sh --case=kp09 --max_time=300 10
```

Watch a single case in the viewer:

```bash
./zlaunch.sh --case=kp15 --gui 10
```

Keep the temp run dirs for inspection:

```bash
./zlaunch.sh --keep_workdirs --max_time=300 10
```

Use a fresh port base if the 9000 range is busy:

```bash
./zlaunch.sh --port_base=9600 --max_time=300 10
```

## Output

- `results.txt` — one normalized row per case
  (`case=… yaw_kp=… grade=… mission_time=… total_dist=…`)
- a printed comparison table at the end of the run

Exit status is nonzero if any case fails to `grade=pass` or if no case rows are
produced.

## Files

- `zlaunch.sh` — the harness runner (case loop, port blocks, aggregation)
- `../` — the stem eval mission this harness drives
- `<repo>/scripts/moos_scoped_teardown.sh` — scoped cleanup helper
