# Controller latency analyzer

This tool calculates BLE connection-event timing from controller traces. It is
intended to establish a repeatable baseline before changing the Link Layer
scheduler. The report includes event duration, connection-event interval,
schedule lateness, skipped event counters, malformed counter transitions,
unmatched trace events, and p50, p95, p99, and maximum values.

## Input

Export controller trace events to CSV with this header:

```text
timestamp_us,event,conn_handle,event_counter,scheduled_us
```

`event` accepts the SystemView names `ll_conn_ev_start` and
`ll_conn_ev_end`, or the shorter names `start` and `end`. `event_counter` is
normally available on end events. `scheduled_us` is optional; include it on a
start event to measure scheduler lateness.

Example:

```text
timestamp_us,event,conn_handle,event_counter,scheduled_us
1000,ll_conn_ev_start,0x0001,,990
1125,ll_conn_ev_end,0x0001,42,
8500,ll_conn_ev_start,0x0001,,8490
8620,ll_conn_ev_end,0x0001,43,
```

Enable `BLE_LL_SYSVIEW` in the controller configuration to emit the existing
connection start and end trace events.

NimBLE's `ll_sched` trace event also contains the current controller tick and
the scheduled start tick. The SystemView export adapter converts their
wrap-safe difference to microseconds and places the result in `scheduled_us`
as `timestamp_us - schedule_delay_us`. This avoids adding instrumentation in
the timing-sensitive controller callback.

The included adapter performs that conversion from a normalized SystemView CSV
containing `timestamp_us` and `message` columns. Pass the controller timer
frequency explicitly; it depends on the target configuration.

```sh
python3 tools/latency/systemview_to_csv.py \
    --timer-hz 32768 systemview.csv > trace.csv
```

The expected message text is the text emitted by the existing trace
descriptions, for example:

```text
ll_sched lls=0 cputime=105 start_time=100
ll_conn_ev_start conn_handle=1
ll_conn_ev_end conn_handle=1 event_cntr=42
```

## Usage

```sh
python3 tools/latency/latency.py trace.csv > latency-report.json
```

Use `--fail-on-anomaly` in automated comparisons to return exit status 3 when
the trace contains unmatched events or duplicate/out-of-order counters. The
JSON report is still emitted for diagnosis.

Run the self-contained tests with:

```sh
python3 -m unittest discover -s tools/latency -p 'test_*.py'
```

For scheduler comparisons, collect identical workloads before and after a
change and compare p95/p99 schedule lateness and skipped event counters. A
counter gap can mean that the controller skipped an event or that the trace
lost a record, so interpret it together with the trace-integrity counters and
the reported schedule-lateness coverage. Average latency alone can hide the
scheduling failures this tool is designed to expose.
Check unmatched events and duplicate or out-of-order counters before trusting a
comparison; nonzero values can indicate an incomplete or reordered trace.
