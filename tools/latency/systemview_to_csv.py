#!/usr/bin/env python3
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to you under the Apache License, Version 2.0.

"""Convert normalized SystemView messages to latency-analyzer CSV."""

import argparse
import csv
import re
import sys


MESSAGE = re.compile(r"(?P<event>ll_[a-z_]+)(?P<fields>.*)")
FIELD = re.compile(r"([a-z_]+)=(0x[0-9a-fA-F]+|[0-9]+)")


def parse_message(message):
    """Return an event name and its integer key-value fields."""
    match = MESSAGE.search(message)
    if not match:
        return None, {}
    fields = {key: int(value, 0)
              for key, value in FIELD.findall(match.group("fields"))}
    return match.group("event"), fields


def tick_delta(current, scheduled, counter_bits):
    """Return the signed wrap-safe difference between controller ticks."""
    modulus = 1 << counter_bits
    delta = (current - scheduled) & (modulus - 1)
    if delta >= modulus // 2:
        delta -= modulus
    return delta


def convert(source, destination, timer_hz, counter_bits=32):
    """Convert timestamp_us,message rows into analyzer input rows."""
    if timer_hz <= 0:
        raise ValueError("timer frequency must be positive")
    if not 1 <= counter_bits <= 64:
        raise ValueError("counter width must be between 1 and 64 bits")

    reader = csv.DictReader(source)
    required = {"timestamp_us", "message"}
    missing = sorted(required - set(reader.fieldnames or ()))
    if missing:
        raise ValueError("missing SystemView columns: %s" %
                         ", ".join(missing))

    writer = csv.DictWriter(
        destination,
        fieldnames=["timestamp_us", "event", "conn_handle",
                    "event_counter", "scheduled_us"])
    writer.writeheader()
    pending_delay_us = None

    for line_number, row in enumerate(reader, start=2):
        try:
            timestamp_us = float(row["timestamp_us"])
            event, fields = parse_message(row["message"])
            if event == "ll_sched":
                delay_ticks = tick_delta(fields["cputime"],
                                         fields["start_time"], counter_bits)
                pending_delay_us = delay_ticks * 1000000.0 / timer_hz
            elif event == "ll_conn_ev_start":
                scheduled_us = ""
                if pending_delay_us is not None:
                    scheduled_us = timestamp_us - pending_delay_us
                writer.writerow({
                    "timestamp_us": timestamp_us,
                    "event": event,
                    "conn_handle": fields["conn_handle"],
                    "event_counter": "",
                    "scheduled_us": scheduled_us,
                })
                pending_delay_us = None
            elif event == "ll_conn_ev_end":
                writer.writerow({
                    "timestamp_us": timestamp_us,
                    "event": event,
                    "conn_handle": fields["conn_handle"],
                    "event_counter": fields["event_cntr"],
                    "scheduled_us": "",
                })
                pending_delay_us = None
            elif event is not None:
                # A connection-start trace is emitted immediately by its
                # scheduler callback. Any other LL event makes an unmatched
                # scheduler sample unsafe to associate with a later start.
                pending_delay_us = None
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError("invalid SystemView row %d: %s" %
                             (line_number, error)) from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=argparse.FileType("r"),
                        help="CSV with timestamp_us and message columns")
    parser.add_argument("--timer-hz", type=float, required=True,
                        help="controller timer frequency in Hz")
    parser.add_argument("--counter-bits", type=int, default=32,
                        help="controller timer width (default: 32)")
    args = parser.parse_args()

    try:
        convert(args.trace, sys.stdout, args.timer_hz, args.counter_bits)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
