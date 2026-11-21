import copy
import unittest
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import report


def capture():
    slots = []
    for i in range(100):
        slots.append(dict(slot=i, label="base", count=100, deadline_misses=0,
                          duration_overruns=0, skipped_ticks=0, max_occupied_us=1000,
                          max_start_delay_us=0, max_end_delay_us=0,
                          tasks_us=100000, gps_us=0, services_us=0, poll_us=100,
                          histogram=[0, 100, 0]))
    return dict(schema_version=1, source="device", work_clock="elapsed", hardware="leaf_3_2_8",
                firmware="test", cpu_mhz=240, memory_supported=True, start_us=1, end_us=100000001,
                settings={}, histogram_upper_us=[500, 1000, 4294967295], slots=slots,
                first_memory=None, memory=[])


def modern_capture():
    c = capture()
    c["schema_version"] = 2
    for s in c["slots"]:
        s.update(button_excluded=0, recovery_excluded=0, recovery_deadline_misses=0,
                 recovery_skipped_ticks=0, max_recovery_start_delay_us=0,
                 max_recovery_end_delay_us=0, max_recovery_baro_start_delay_us=0,
                 max_recovery_occupied_us=0)
    c["pages"] = [dict(context=i+10, label=label, count=0, total_us=0, max_render_us=0,
                       slots=0, max_slot_occupied_us=0, deadline_misses=0,
                       button_excluded=0, recovery_excluded=0, histogram=[0, 0, 0])
                  for i, label in enumerate(["Basic", "User", "Thermal Core", "Navigate", "Debug", "Debug 2", "Thermal Track"])]
    totals = dict(count=0, deadline_misses=0, duration_overruns=0, skipped_ticks=0,
                  max_occupied_us=0, max_start_delay_us=0, max_end_delay_us=0,
                  max_baro_start_delay_us=0)
    c.update(button_slots=totals.copy(), recovery_slots=totals.copy())
    return c


def gps_capture():
    c = modern_capture()
    c["schema_version"] = 3
    stages = ["UART read", "Bus mutex wait", "Mutex wait", "Parse", "GPS updates", "Reading callbacks", "BLE enqueue"]
    types = [dict(type=i, label=name, count=0, accepted=0, rejected=0, unobserved=0,
                  deadline_crossings=0, total_us=0, max_processing_us=0, max_end_delay_us=0,
                  stage_total_us=[0]*7, stage_max_us=[0]*7, histogram=[0, 0, 0])
             for i, name in enumerate(["GGA", "RMC", "GSV", "GSA", "VTG", "GLL", "ZDA", "Other"])]
    types[0].update(count=2, accepted=1, rejected=1, deadline_crossings=1,
                    total_us=3000, max_processing_us=2000, max_end_delay_us=1000,
                    stage_total_us=[100, 20, 40, 500, 100, 200, 100],
                    stage_max_us=[100, 20, 40, 500, 100, 200, 100], histogram=[1, 1, 0])
    event = dict(type=0, talker="GP", validity=2, slot=76, tick=100, flags=0, length=65,
                 first_read_us=18000, ready_us=19000, end_us=21000, expected_us=10000,
                 processing_us=2000, stage_us=[100, 20, 40, 500, 100, 200, 100])
    earlier = dict(event, validity=1, first_read_us=17000, ready_us=18000, end_us=19000,
                   processing_us=1000, stage_us=[0]*7)
    slots = [0]*100; slots[76] = 2
    c["gps_profile"] = dict(total=2, unassigned=0, partial_uart=0, excluded_async=0,
                             nested_dropped=0, uart_resets=0, recent_capacity=8, worst_capacity=4,
                             recent_overwritten=0, record_max_us=12, stage_names=stages,
                             histogram_upper_us=[1000, 2000, 4294967295], start_slots=slots,
                             types=types, recent=[earlier, event], worst=[copy.deepcopy(event)])
    return c


class ReportTests(unittest.TestCase):
    def test_fanet_logging_changes_withhold_baseline_comparison(self):
        c = gps_capture()
        c["settings"]["fanet_log"] = False
        meta = dict(profile="p", scenario="s", toolchain="t")
        baseline = dict(capture=copy.deepcopy(c), metadata=meta)
        self.assertTrue(report.assess(c, meta, baseline)["baseline_comparable"])
        c["settings"]["fanet_log"] = True
        assessment = report.assess(c, meta, baseline)
        self.assertFalse(assessment["baseline_comparable"])
        self.assertIn("Other diagnostic SD logging was enabled and can affect timing.", assessment["notes"])

    def test_inherited_lateness_does_not_become_duration_overrun(self):
        c = capture()
        c["slots"][0].update(deadline_misses=1, max_start_delay_us=9500, max_end_delay_us=500)
        a = report.assess(c, {})
        self.assertEqual(a["deadline_misses"], 1)
        self.assertEqual(a["duration_overruns"], 0)
        self.assertEqual(report.percentile_upper(c["slots"][0], c["histogram_upper_us"], .95), 1000)

    def test_host_results_never_receive_device_capacity_colors(self):
        c = capture()
        c.update(source="simulator", work_clock="host_thread_cpu", memory_supported=False)
        result = dict(capture=c, metadata={"label": "<script>unsafe</script>"}, assessment=report.assess(c, {}))
        page = report.render(result)
        self.assertNotIn('class="cell green"', page)
        self.assertEqual(page.count('class="cell unknown"'), 100)
        self.assertIn("No ESP32 timing estimate", page)
        self.assertNotIn("<script>unsafe</script>", page)

    def test_rejects_missing_slots_and_corrupt_counts(self):
        c = capture()
        c["slots"].pop()
        with self.assertRaises(ValueError):
            report.assess(c, {})
        c = capture()
        c["slots"][9]["histogram"][0] = 1
        with self.assertRaises(ValueError):
            report.assess(c, {})

    def test_baseline_requires_equivalent_measurement_configuration(self):
        c = capture()
        metadata = dict(profile="flight", scenario="thermal-v1", toolchain="gcc-fixed")
        baseline = dict(capture=copy.deepcopy(c), metadata=metadata)
        self.assertTrue(report.assess(c, metadata, baseline)["baseline_comparable"])
        c["settings"]["ble"] = True
        result = report.assess(c, metadata, baseline)
        self.assertFalse(result["baseline_comparable"])
        self.assertEqual(result["largest_changes"], [])

    def test_missing_workload_metadata_withholds_comparison(self):
        c = capture()
        result = report.assess(c, {}, dict(capture=c, metadata={}))
        self.assertFalse(result["baseline_comparable"])

    def test_partial_coverage_is_incomplete(self):
        c = capture()
        c["slots"][0].update(count=0, histogram=[0, 0, 0])
        self.assertEqual(report.assess(c, {})["status"], "incomplete")

    def test_unbounded_histogram_bin_is_capped_at_observed_maximum(self):
        c = capture()
        s = c["slots"][0]
        s.update(histogram=[0, 0, 100], max_occupied_us=23000)
        self.assertEqual(report.percentile_upper(s, c["histogram_upper_us"], .99), 23000)

    def test_sampled_minima_survive_history_wrapping(self):
        c = capture()
        first = dict(time_us=1, internal_free=100)
        last = dict(time_us=2, internal_free=95)
        c.update(first_memory=first, memory=[last], memory_minima=dict(internal_free=50),
                 memory_overwritten=10, failed_allocations=1)
        m = report.assess(c, {})["memory"]
        self.assertEqual(m["sampled_minima"]["internal_free"], 50)
        self.assertEqual(m["internal_free_change"], -5)
        self.assertEqual(m["failed_allocations"], 1)

    def test_output_cannot_overwrite_raw_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / "runtime.json"
            original = json.dumps(capture())
            raw.write_text(original, encoding="utf-8")
            result = subprocess.run([sys.executable, str(Path(report.__file__)), str(raw),
                                     "--output", str(raw.with_suffix(".html"))],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("must not overwrite", result.stderr)
            self.assertEqual(raw.read_text(encoding="utf-8"), original)

    def test_comparison_requires_similar_duration(self):
        c = capture()
        baseline = dict(capture=copy.deepcopy(c), metadata=dict(profile="p", scenario="s", toolchain="t"))
        c["end_us"] *= 2
        a = report.assess(c, baseline["metadata"], baseline)
        self.assertFalse(a["baseline_comparable"])

    def test_unmeasured_background_work_is_incomplete(self):
        c = capture()
        c.update(source="simulator", work_clock="host_thread_cpu", memory_supported=False,
                 unmeasured_async_segments=1)
        self.assertEqual(report.assess(c, {})["status"], "incomplete")

    def test_pages_show_render_and_whole_slot_cost_with_unsampled_rows(self):
        c = modern_capture()
        c["pages"][0].update(count=100, total_us=90000, max_render_us=1000,
                              slots=100, max_slot_occupied_us=8000, histogram=[0, 100, 0],
                              button_excluded=2)
        a = report.assess(c, {})
        self.assertEqual(a["pages"][0]["average_render_us"], 900)
        self.assertEqual(a["pages"][0]["p95_render_upper_us"], 1000)
        page = report.render(dict(capture=c, metadata={}, assessment=a))
        self.assertIn("Display pages", page)
        self.assertIn("Not measured", page)
        self.assertIn("8.000", page)

    def test_recovery_is_visible_without_changing_normal_grid(self):
        c = modern_capture()
        c["slots"][0].update(button_excluded=1, recovery_excluded=1,
                               recovery_deadline_misses=1, max_recovery_start_delay_us=20000)
        c["button_slots"].update(count=1, max_occupied_us=30000)
        c["recovery_slots"].update(count=1, deadline_misses=1, max_start_delay_us=20000)
        a = report.assess(c, {})
        self.assertEqual(a["deadline_misses"], 0)
        self.assertEqual(a["recovery_slots"]["deadline_misses"], 1)
        self.assertTrue(any("ATTENTION: button recovery" in note for note in a["notes"]))
        self.assertIn("20.000", report.render(dict(capture=c, metadata={}, assessment=a)))

    def test_rejects_inconsistent_page_and_exclusion_counts(self):
        c = modern_capture()
        c["pages"][0]["count"] = 1
        with self.assertRaises(ValueError):
            report.assess(c, {})
        c = modern_capture()
        c["slots"][0]["button_excluded"] = 1
        with self.assertRaises(ValueError):
            report.assess(c, {})

    def test_legacy_capture_is_explicit_and_cannot_compare_new_accounting(self):
        c = modern_capture()
        old = capture()
        m = dict(profile="p", scenario="s", toolchain="t")
        self.assertFalse(report.assess(c, m, dict(capture=old, metadata=m))["baseline_comparable"])
        self.assertTrue(any("Legacy capture" in note for note in report.assess(old, {})["notes"]))

    def test_legacy_unused_memory_fields_are_omitted_without_changing_input(self):
        raw = dict(memory=[dict(internal_free=42, psram_free=0, psram_min=0, psram_largest=0)])
        clean = report.report_fields(raw)
        self.assertEqual(clean, dict(memory=[dict(internal_free=42)]))
        self.assertEqual(len(raw["memory"][0]), 4)

    def test_gps_sentence_types_phases_and_stages_are_reported(self):
        c = gps_capture()
        a = report.assess(c, {})
        self.assertEqual(a["gps"]["sentence_deadline_crossings"], 1)
        self.assertEqual(a["gps"]["types"][0]["average_processing_us"], 1500)
        self.assertEqual(a["gps"]["types"][0]["other_us"], 1940)
        page = report.render(dict(capture=c, metadata={}, assessment=a))
        for text in ("GPGGA", "Bus mutex wait", "Slot 76: 2 sentence dispatches", "11.000", "wire-arrival"):
            self.assertIn(text, page)

    def test_rejects_overlapping_gps_stages_and_corrupt_phase_counts(self):
        c = gps_capture()
        c["gps_profile"]["recent"][0]["stage_us"][2] = 1001
        with self.assertRaises(ValueError):
            report.assess(c, {})
        c = gps_capture()
        c["gps_profile"]["start_slots"][76] = 1
        with self.assertRaises(ValueError):
            report.assess(c, {})

    def test_gps_coverage_gaps_withhold_baseline_comparison(self):
        c = gps_capture()
        meta = dict(profile="p", scenario="s", toolchain="t")
        baseline = dict(capture=copy.deepcopy(c), metadata=meta)
        self.assertTrue(report.assess(c, meta, baseline)["baseline_comparable"])
        c["gps_profile"]["excluded_async"] = 1
        a = report.assess(c, meta, baseline)
        self.assertEqual(a["status"], "incomplete")
        self.assertFalse(a["baseline_comparable"])

    def test_rejects_non_crossing_worst_event_and_missing_profile(self):
        c = gps_capture()
        c["gps_profile"]["worst"][0]["end_us"] = 20000
        with self.assertRaises(ValueError):
            report.assess(c, {})
        c.pop("gps_profile")
        with self.assertRaises(ValueError):
            report.assess(c, {})

    def test_previous_page_capture_remains_readable_without_sentence_details(self):
        c = modern_capture()
        a = report.assess(c, {})
        self.assertIsNone(a["gps"])
        self.assertIn("new capture with sentence profiling", report.render(dict(capture=c, metadata={}, assessment=a)))


if __name__ == "__main__":
    unittest.main()
