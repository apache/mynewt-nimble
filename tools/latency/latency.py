#!/usr/bin/env python3
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to you under the Apache License, Version 2.0.

"""Analyze BLE controller connection-event latency from a CSV trace."""

import argparse
import csv
import json
import math
import sys
from collections import defaultdict


EVENT_ALIASES = {
    "ll_conn_ev_start": "start",
    "conn_start": "start",
    "start": "start",
    "ll_conn_ev_end": "end",
    "conn_end": "end",
    "end": "end",
}

REQUIRED_COLUMNS = {"timestamp_us", "event", "conn_handle"}


def percentile(values, quantile):
    """Return a linearly interpolated percentile."""
    if not values:
        return None
    ordered = sorted(values)
    index = (len(ordered) - 1) * quantile
    lower = math.floor(index)
    upper = math.ceil(index)
    if lower == upper:
        return ordered[lower]
    return (ordered[lower] * (upper - index) +
            ordered[upper] * (index - lower))


def distribution(values):
    """Summarize a collection of microsecond measurements."""
    if not values:
        return None
    return {
        "count": len(values),
        "min_us": min(values),
        "mean_us": sum(values) / len(values),
        "p50_us": percentile(values, 0.50),
        "p95_us": percentile(values, 0.95),
        "p99_us": percentile(values, 0.99),
        "max_us": max(values),
    }


def missed_between(first, second):
    """Count skipped 16-bit Bluetooth event counters, including wrap."""
    skipped, anomaly = counter_transition(first, second)
    if anomaly:
        raise ValueError("invalid event counter transition: %s" % anomaly)
    return skipped


def counter_transition(first, second):
    """Classify an event-counter transition without inflating missed counts."""
    delta = (second - first) & 0xffff
    if delta == 0:
        return 0, "duplicate"
    if delta > 0x8000:
        return 0, "out_of_order"
    return delta - 1, None


def read_trace(stream):
    """Read and normalize latency events from a CSV stream."""
    reader = csv.DictReader(stream)
    columns = set(reader.fieldnames or ())
    missing = sorted(REQUIRED_COLUMNS - columns)
    if missing:
        raise ValueError("missing trace columns: %s" % ", ".join(missing))

    rows = []
    for line_number, row in enumerate(reader, start=2):
        try:
            event = EVENT_ALIASES[row["event"].strip()]
            normalized = {
                "timestamp_us": float(row["timestamp_us"]),
                "event": event,
                "conn_handle": int(row["conn_handle"], 0),
                "event_counter": None,
                "scheduled_us": None,
            }
            if row.get("event_counter", "").strip():
                normalized["event_counter"] = int(row["event_counter"], 0)
            if row.get("scheduled_us", "").strip():
                normalized["scheduled_us"] = float(row["scheduled_us"])
            if not math.isfinite(normalized["timestamp_us"]):
                raise ValueError("timestamp_us must be finite")
            if normalized["timestamp_us"] < 0:
                raise ValueError("timestamp_us must not be negative")
            if not 0 <= normalized["conn_handle"] <= 0x0eff:
                raise ValueError("conn_handle must be between 0 and 0x0eff")
            if (normalized["event_counter"] is not None and
                    not 0 <= normalized["event_counter"] <= 0xffff):
                raise ValueError("event_counter must be between 0 and 0xffff")
            if (normalized["scheduled_us"] is not None and
                    (not math.isfinite(normalized["scheduled_us"]) or
                     normalized["scheduled_us"] < 0)):
                raise ValueError("scheduled_us must be finite and nonnegative")
            rows.append(normalized)
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError("invalid trace row %d: %s" %
                             (line_number, error)) from error
    return sorted(rows, key=lambda row: row["timestamp_us"])


def analyze(rows):
    """Calculate per-connection and aggregate latency measurements."""
    connections = defaultdict(list)
    for row in rows:
        connections[row["conn_handle"]].append(row)

    reports = {}
    all_intervals = []
    all_durations = []
    all_lateness = []
    total_missed = 0
    total_unmatched_starts = 0
    total_unmatched_ends = 0
    total_duplicate_counters = 0
    total_out_of_order_counters = 0
    total_starts = 0
    total_lateness_samples = 0

    for handle, events in sorted(connections.items()):
        starts = [event for event in events if event["event"] == "start"]
        ends = [event for event in events if event["event"] == "end"]
        intervals = [second["timestamp_us"] - first["timestamp_us"]
                     for first, second in zip(starts, starts[1:])]
        lateness = [event["timestamp_us"] - event["scheduled_us"]
                    for event in starts if event["scheduled_us"] is not None]
        lateness_coverage = len(lateness) / len(starts) if starts else None

        pending = None
        unmatched_starts = 0
        unmatched_ends = 0
        durations = []
        for event in events:
            if event["event"] == "start":
                if pending is not None:
                    unmatched_starts += 1
                pending = event["timestamp_us"]
            elif pending is None:
                unmatched_ends += 1
            else:
                durations.append(event["timestamp_us"] - pending)
                pending = None

        if pending is not None:
            unmatched_starts += 1

        counters = [event["event_counter"] for event in ends
                    if event["event_counter"] is not None]
        missed = 0
        duplicate_counters = 0
        out_of_order_counters = 0
        for first, second in zip(counters, counters[1:]):
            skipped, anomaly = counter_transition(first, second)
            missed += skipped
            duplicate_counters += anomaly == "duplicate"
            out_of_order_counters += anomaly == "out_of_order"

        reports[str(handle)] = {
            "events_started": len(starts),
            "events_ended": len(ends),
            "unmatched_starts": unmatched_starts,
            "unmatched_ends": unmatched_ends,
            "skipped_event_counters": missed,
            "duplicate_event_counters": duplicate_counters,
            "out_of_order_event_counters": out_of_order_counters,
            "schedule_lateness_sample_count": len(lateness),
            "schedule_lateness_coverage": lateness_coverage,
            "start_interval": distribution(intervals),
            "event_duration": distribution(durations),
            "schedule_lateness": distribution(lateness),
        }
        all_intervals.extend(intervals)
        all_durations.extend(durations)
        all_lateness.extend(lateness)
        total_missed += missed
        total_unmatched_starts += unmatched_starts
        total_unmatched_ends += unmatched_ends
        total_duplicate_counters += duplicate_counters
        total_out_of_order_counters += out_of_order_counters
        total_starts += len(starts)
        total_lateness_samples += len(lateness)

    return {
        "connections": reports,
        "aggregate": {
            "connection_count": len(reports),
            "skipped_event_counters": total_missed,
            "unmatched_starts": total_unmatched_starts,
            "unmatched_ends": total_unmatched_ends,
            "duplicate_event_counters": total_duplicate_counters,
            "out_of_order_event_counters": total_out_of_order_counters,
            "schedule_lateness_sample_count": total_lateness_samples,
            "schedule_lateness_coverage": (
                total_lateness_samples / total_starts if total_starts else None),
            "start_interval": distribution(all_intervals),
            "event_duration": distribution(all_durations),
            "schedule_lateness": distribution(all_lateness),
        },
    }


def has_anomalies(report):
    """Return whether trace-integrity counters indicate unreliable input."""
    aggregate = report["aggregate"]
    return any(aggregate[key] for key in (
        "unmatched_starts",
        "unmatched_ends",
        "duplicate_event_counters",
        "out_of_order_event_counters",
    ))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=argparse.FileType("r"),
                        help="CSV trace to analyze")
    parser.add_argument("--indent", type=int, default=2,
                        help="JSON indentation (default: 2)")
    parser.add_argument("--fail-on-anomaly", action="store_true",
                        help="exit 3 when trace-integrity anomalies are found")
    args = parser.parse_args()

    try:
        report = analyze(read_trace(args.trace))
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    json.dump(report, sys.stdout, indent=args.indent, sort_keys=True)
    print()
    if args.fail_on_anomaly and has_anomalies(report):
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
