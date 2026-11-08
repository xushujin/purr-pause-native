import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image

SPEC = importlib.util.spec_from_file_location("compare_visual", Path(__file__).resolve().parents[2] / "scripts/compare-visual.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VisualComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.fixture = {"schemaVersion": 1, "timezone": "Asia/Shanghai", "electronVersion": "33.2.1", "pages": [
            {"id": "settings", "width": 320, "height": 320, "bindings": [{"key": "count", "expected": 45}]}]}
        self.fixture_path = self.root / "fixture.json"
        self.fixture_path.write_text(json.dumps(self.fixture))
        field = {"value": 45, "visible": True, "enabled": True, "rect": {"x": 10, "y": 20, "width": 70, "height": 32}, "font": {"pixelSize": 15}}
        base = {"schemaVersion": 1, "timezone": "Asia/Shanghai", "devicePixelRatio": 1,
            "fixtureSha256": hashlib.sha256(self.fixture_path.read_bytes()).hexdigest(),
            "pages": [{"id": "settings", "width": 320, "height": 320, "bindings": {"count": field}}]}
        self.reference = copy.deepcopy(base)
        self.reference.update(kind="reference", electronVersion="33.2.1", chromiumVersion="130.0.6723.137")
        self.reference["pages"][0]["image"] = "original-settings.png"
        self.native = copy.deepcopy(base)
        self.native.update(kind="native", qtVersion="6.2.4")
        self.native["pages"][0]["image"] = "native-settings.png"
        self.write_manifests()
        for name in ("original-settings.png", "native-settings.png"):
            Image.new("RGB", (320, 320), "white").save(self.root / name)

    def write_manifests(self):
        for name, value in (("reference", self.reference), ("native", self.native)):
            (self.root / f"{name}-manifest.json").write_text(json.dumps(value))

    def compare(self):
        return MODULE.compare(self.fixture_path, self.root, self.root / "report")

    def test_reports_pixels_without_treating_difference_as_identity(self):
        image = Image.open(self.root / "native-settings.png")
        image.putpixel((0, 0), (0, 0, 0))
        image.save(self.root / "native-settings.png")
        report = self.compare()
        self.assertEqual(report["captureValidation"], "passed")
        self.assertFalse(report["pixelIdentity"])
        self.assertGreater(report["pages"][0]["meanAbsoluteRgbError0To255"], 0)

    def test_rejects_output_dimensions_that_do_not_match_fixture(self):
        Image.new("RGB", (321, 320), "white").save(self.root / "native-settings.png")
        with self.assertRaisesRegex(ValueError, "dimensions"):
            self.compare()

    def test_rejects_changed_or_missing_fixture_data(self):
        self.native["pages"][0]["bindings"]["count"]["value"] = 99
        self.write_manifests()
        with self.assertRaisesRegex(ValueError, "field value"):
            self.compare()

    def test_rejects_duplicate_pages_and_wrong_reference_engine(self):
        self.native["pages"].append(copy.deepcopy(self.native["pages"][0]))
        self.write_manifests()
        with self.assertRaisesRegex(ValueError, "duplicated"):
            self.compare()
        self.native["pages"].pop()
        self.reference["electronVersion"] = "34.0.0"
        self.write_manifests()
        with self.assertRaisesRegex(ValueError, "Electron"):
            self.compare()

    def test_rejects_fixture_hash_mismatch(self):
        self.native["fixtureSha256"] = "incorrect"
        self.write_manifests()
        with self.assertRaisesRegex(ValueError, "digest"):
            self.compare()

    def test_rejects_hidden_or_disabled_controls_that_should_be_available(self):
        for state in ("visible", "enabled"):
            self.native["pages"][0]["bindings"]["count"][state] = False
            self.write_manifests()
            with self.assertRaisesRegex(ValueError, state + " state differs"):
                self.compare()
            self.native["pages"][0]["bindings"]["count"][state] = True


if __name__ == "__main__":
    unittest.main()
