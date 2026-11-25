"""Reader compatibility and rejection checks for compact IGC sensor records."""
import unittest

from check_g_load_igc import check_sensors


class SensorRecordsTest(unittest.TestCase):
    def records(self, version, sample):
        declaration = (b"LXLVSENSOR V=1 G=MG T=DECI_C HEX=1" if version == 1 else
                       b"LXLVSENSOR V=2 G=MG T=DECI_C RH=DECI_PCT HEX=1")
        return [declaration, b"LXLVA000003E801012513000000",
                b"B1300004730000N01112000EA0120001200004050090000000000", sample]

    def test_version_1_remains_readable(self):
        check_sensors(self.records(1, b"LXLVS000005DC04D2F85"), "legacy.igc")

    def test_version_2_humidity_range_and_missing(self):
        for humidity in (b"000", b"28E", b"3E8", b"FFF"):
            with self.subTest(humidity=humidity):
                check_sensors(self.records(2, b"LXLVS000005DC04D2F85" + humidity), "current.igc")

    def test_invalid_humidity_and_record_lengths_are_rejected(self):
        for sample in (b"LXLVS000005DC04D2F853E9", b"LXLVS000005DC04D2F85FFE",
                       b"LXLVS000005DC04D2F85", b"LXLVS000005DC04D2F8528E0"):
            with self.subTest(sample=sample), self.assertRaises(AssertionError):
                check_sensors(self.records(2, sample), "invalid.igc")

    def test_version_1_rejects_version_2_length(self):
        with self.assertRaises(AssertionError):
            check_sensors(self.records(1, b"LXLVS000005DC04D2F8528E"), "invalid.igc")

    def test_unknown_schema_is_rejected(self):
        lines = self.records(2, b"LXLVS000005DC04D2F8528E")
        lines[0] = lines[0].replace(b"V=2", b"V=3")
        with self.assertRaises(AssertionError):
            check_sensors(lines, "unknown.igc")


if __name__ == "__main__":
    unittest.main()
