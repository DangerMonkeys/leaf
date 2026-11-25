"""Reader compatibility and rejection checks for IGC sensor records."""
import unittest

from check_g_load_igc import check_sensors


class SensorRecordsTest(unittest.TestCase):
    def records(self, version=2, sample=b"LXLVS000005DC04D2", ambient=b"K1300000018.0065.4"):
        declaration = (b"LXLVSENSOR V=1 G=MG T=DECI_C HEX=1" if version == 1 else
                       b"LXLVSENSOR V=2 G=MG HEX=1")
        lines = [declaration, b"LXLVA000003E801012513000000",
                 b"B1300004730000N01112000EA0120001200004050090000000000", sample]
        if version == 2:
            lines.insert(0, b"J020813OAT1418HUM")
            if ambient:
                lines.append(ambient)
        return lines

    def test_version_1_remains_readable(self):
        check_sensors(self.records(1, b"LXLVS000005DC04D2F85"), "legacy.igc")

    def test_standard_ambient_values_and_missing_readings(self):
        for ambient in (b"K1300000018.0065.4", b"K130000-012.3100.0",
                        b"K1300000000.0000.0", None):
            with self.subTest(ambient=ambient):
                check_sensors(self.records(ambient=ambient), "current.igc")

    def test_invalid_ambient_values_are_rejected(self):
        for ambient in (b"K1300000018.0100.1", b"K1300000018.0-00.1",
                        b"K1300000018.0FFF", b"K1300000204.8065.4"):
            with self.subTest(ambient=ambient), self.assertRaises(AssertionError):
                check_sensors(self.records(ambient=ambient), "invalid.igc")

    def test_versioned_sensor_lengths(self):
        for version, sample in ((1, b"LXLVS000005DC04D2"), (2, b"LXLVS000005DC04D2F85")):
            with self.subTest(version=version), self.assertRaises(AssertionError):
                check_sensors(self.records(version, sample), "invalid.igc")

    def test_unknown_or_missing_schema_is_rejected(self):
        for missing in (False, True):
            lines = self.records()
            lines = [line for line in lines if line != b"J020813OAT1418HUM"] if missing else [
                line.replace(b"V=2", b"V=3") for line in lines]
            with self.subTest(missing=missing), self.assertRaises(AssertionError):
                check_sensors(lines, "unknown.igc")


if __name__ == "__main__":
    unittest.main()
