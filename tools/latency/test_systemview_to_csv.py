#!/usr/bin/env python3
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to you under the Apache License, Version 2.0.

import csv
import io
import unittest
from pathlib import Path

import systemview_to_csv


TRACE = """timestamp_us,message
1000,ll_sched lls=0 cputime=105 start_time=100
1010,ll_conn_ev_start conn_handle=1
1090,ll_conn_ev_end conn_handle=1 event_cntr=42
"""


class SystemViewToCsvTest(unittest.TestCase):
    def test_normalized_systemview_fixture(self):
        testdata = Path(__file__).with_name("testdata")
        output = io.StringIO()
        with (testdata / "normalized_systemview.csv").open() as source:
            systemview_to_csv.convert(source, output, timer_hz=1000000)
        with (testdata / "expected_trace.csv").open() as expected:
            self.assertEqual(list(csv.DictReader(io.StringIO(output.getvalue()))),
                             list(csv.DictReader(expected)))

    def test_converts_connection_events_and_scheduler_lateness(self):
        output = io.StringIO()
        systemview_to_csv.convert(io.StringIO(TRACE), output, timer_hz=1000000)
        rows = list(csv.DictReader(io.StringIO(output.getvalue())))

        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["event"], "ll_conn_ev_start")
        self.assertEqual(float(rows[0]["scheduled_us"]), 1005)
        self.assertEqual(rows[1]["event_counter"], "42")

    def test_tick_delta_handles_wrap(self):
        self.assertEqual(systemview_to_csv.tick_delta(1, 0xffffffff, 32), 2)

    def test_rejects_missing_columns(self):
        with self.assertRaisesRegex(ValueError, "missing SystemView columns"):
            systemview_to_csv.convert(
                io.StringIO("timestamp_us\n1\n"), io.StringIO(), 1000000)

    def test_does_not_reuse_scheduler_sample_after_another_event(self):
        trace = io.StringIO(
            "timestamp_us,message\n"
            "1000,ll_sched lls=0 cputime=105 start_time=100\n"
            "1005,ll_adv_txdone inst=0 chanset=7\n"
            "1010,ll_conn_ev_start conn_handle=1\n")
        output = io.StringIO()
        systemview_to_csv.convert(trace, output, timer_hz=1000000)
        rows = list(csv.DictReader(io.StringIO(output.getvalue())))

        self.assertEqual(rows[0]["scheduled_us"], "")

    def test_rejects_invalid_timer_configuration(self):
        with self.assertRaisesRegex(ValueError, "frequency must be positive"):
            systemview_to_csv.convert(io.StringIO(TRACE), io.StringIO(), 0)


if __name__ == "__main__":
    unittest.main()
