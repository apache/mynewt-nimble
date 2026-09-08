#!/usr/bin/env python3
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to you under the Apache License, Version 2.0.

import io
import unittest

import latency


TRACE = """timestamp_us,event,conn_handle,event_counter,scheduled_us
100,start,0x0001,,90
180,end,0x0001,10,
1100,start,0x0001,,1090
1190,end,0x0001,11,
3100,start,0x0001,,3050
3210,end,0x0001,13,
200,start,0x0002,,
260,end,0x0002,20,
"""


class LatencyTest(unittest.TestCase):
    def test_analyze_connections(self):
        report = latency.analyze(latency.read_trace(io.StringIO(TRACE)))

        self.assertEqual(report["aggregate"]["connection_count"], 2)
        self.assertEqual(report["aggregate"]["skipped_event_counters"], 1)
        self.assertEqual(report["connections"]["1"]["events_started"], 3)
        self.assertEqual(
            report["connections"]["1"]["start_interval"]["max_us"], 2000)
        self.assertEqual(
            report["connections"]["1"]["event_duration"]["p50_us"], 90)
        self.assertEqual(
            report["connections"]["1"]["schedule_lateness"]["max_us"], 50)
        self.assertEqual(
            report["connections"]["1"]["schedule_lateness_coverage"], 1)

    def test_rejects_unknown_event(self):
        trace = io.StringIO(
            "timestamp_us,event,conn_handle\n1,unknown,1\n")
        with self.assertRaisesRegex(ValueError, "invalid trace row 2"):
            latency.read_trace(trace)

    def test_empty_trace(self):
        trace = io.StringIO(
            "timestamp_us,event,conn_handle,event_counter,scheduled_us\n")
        report = latency.analyze(latency.read_trace(trace))
        self.assertEqual(report["aggregate"]["connection_count"], 0)
        self.assertIsNone(report["aggregate"]["event_duration"])

    def test_event_counter_wrap(self):
        self.assertEqual(latency.missed_between(0xffff, 0), 0)
        self.assertEqual(latency.missed_between(0xfffe, 1), 2)

    def test_dropped_end_does_not_corrupt_next_duration(self):
        trace = io.StringIO(
            "timestamp_us,event,conn_handle,event_counter\n"
            "100,start,1,\n"
            "200,start,1,\n"
            "275,end,1,10\n")
        report = latency.analyze(latency.read_trace(trace))["connections"]["1"]

        self.assertEqual(report["unmatched_starts"], 1)
        self.assertEqual(report["unmatched_ends"], 0)
        self.assertEqual(report["event_duration"]["max_us"], 75)

    def test_counter_anomalies_are_not_reported_as_missed_events(self):
        trace = io.StringIO(
            "timestamp_us,event,conn_handle,event_counter\n"
            "100,end,1,10\n"
            "200,end,1,10\n"
            "300,end,1,9\n")
        report = latency.analyze(latency.read_trace(trace))["connections"]["1"]

        self.assertEqual(report["skipped_event_counters"], 0)
        self.assertEqual(report["duplicate_event_counters"], 1)
        self.assertEqual(report["out_of_order_event_counters"], 1)
        self.assertEqual(report["unmatched_ends"], 3)

    def test_rejects_missing_columns(self):
        with self.assertRaisesRegex(ValueError, "missing trace columns"):
            latency.read_trace(io.StringIO("timestamp_us,event\n1,start\n"))

    def test_rejects_nonfinite_and_out_of_range_values(self):
        invalid_rows = (
            "nan,start,1,,",
            "-1,start,1,,",
            "1,start,0x0f00,,",
            "1,end,1,65536,",
            "1,start,1,,inf",
        )
        header = "timestamp_us,event,conn_handle,event_counter,scheduled_us\n"
        for row in invalid_rows:
            with self.subTest(row=row):
                with self.assertRaisesRegex(ValueError, "invalid trace row 2"):
                    latency.read_trace(io.StringIO(header + row + "\n"))

    def test_reports_partial_lateness_coverage(self):
        trace = io.StringIO(
            "timestamp_us,event,conn_handle,scheduled_us\n"
            "100,start,1,90\n"
            "200,end,1,\n"
            "300,start,1,\n")
        report = latency.analyze(latency.read_trace(trace))["connections"]["1"]

        self.assertEqual(report["schedule_lateness_sample_count"], 1)
        self.assertEqual(report["schedule_lateness_coverage"], 0.5)
        self.assertTrue(latency.has_anomalies({"aggregate": report}))


class CounterTransitionTest(unittest.TestCase):
    def test_duplicate(self):
        self.assertEqual(latency.counter_transition(10, 10),
                         (0, "duplicate"))

    def test_out_of_order(self):
        self.assertEqual(latency.counter_transition(10, 9),
                         (0, "out_of_order"))

    def test_missed_between_rejects_anomalies(self):
        with self.assertRaisesRegex(ValueError, "duplicate"):
            latency.missed_between(10, 10)


if __name__ == "__main__":
    unittest.main()
