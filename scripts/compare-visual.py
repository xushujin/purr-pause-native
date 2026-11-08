#!/usr/bin/env python3
"""Validate visual captures and report all pixel/layout differences without masks."""

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageStat


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def validate_manifest(manifest, kind, fixture, digest):
    require(manifest.get("schemaVersion") == 1, f"Invalid {kind} manifest schema")
    require(manifest.get("kind") == kind, f"Wrong {kind} manifest kind")
    require(manifest.get("fixtureSha256") == digest, f"{kind} fixture digest differs")
    require(manifest.get("timezone") == fixture["timezone"], f"{kind} timezone differs")
    require(manifest.get("devicePixelRatio") == 1, f"{kind} pixel ratio differs")
    if kind == "reference":
        require(manifest.get("electronVersion") == fixture["electronVersion"], "Wrong reference Electron version")
        require(bool(manifest.get("chromiumVersion")), "Missing reference Chromium version")
    else:
        require(bool(manifest.get("qtVersion")), "Missing native Qt version")
    pages = manifest.get("pages", [])
    ids = [page.get("id") for page in pages]
    expected = [page["id"] for page in fixture["pages"]]
    require(len(ids) == len(set(ids)) and set(ids) == set(expected), f"{kind} pages missing, duplicated or unexpected")
    return {page["id"]: page for page in pages}


def open_capture(directory, page, expected_size):
    filename = page.get("image", "")
    require(filename and Path(filename).name == filename, "Image must be a filename in the capture directory")
    require((page.get("width"), page.get("height")) == expected_size, "Manifest viewport mismatch")
    with Image.open(Path(directory) / filename) as image:
        require(image.format == "PNG", "Capture must be PNG")
        require(image.size == expected_size, "Image dimensions differ from fixture")
        return image.convert("RGB")


def compare(fixture_path, capture_directory, output_directory):
    fixture_path = Path(fixture_path)
    fixture = read_json(fixture_path)
    require(fixture.get("schemaVersion") == 1, "Invalid fixture schema")
    expected_pages = fixture.get("pages", [])
    ids = [page["id"] for page in expected_pages]
    require(ids and len(ids) == len(set(ids)), "Fixture pages empty or duplicated")
    digest = hashlib.sha256(fixture_path.read_bytes()).hexdigest()
    directory = Path(capture_directory)
    reference_manifest = read_json(directory / "reference-manifest.json")
    native_manifest = read_json(directory / "native-manifest.json")
    reference_pages = validate_manifest(reference_manifest, "reference", fixture, digest)
    native_pages = validate_manifest(native_manifest, "native", fixture, digest)
    output = Path(output_directory)
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for expected in expected_pages:
        page_id = expected["id"]
        reference = reference_pages[page_id]
        native = native_pages[page_id]
        size = (expected["width"], expected["height"])
        require(all(isinstance(value, int) and 320 <= value <= 4096 for value in size), "Invalid fixture viewport")
        before = open_capture(directory, reference, size)
        after = open_capture(directory, native, size)
        binding_differences = {}
        keys = [binding["key"] for binding in expected.get("bindings", [])]
        require(len(keys) == len(set(keys)), "Fixture has duplicate binding keys")
        for side in (reference, native):
            require(set(side.get("bindings", {})) == set(keys), f"{page_id}: missing or unexpected field bindings")
        for binding in expected.get("bindings", []):
            key = binding["key"]
            left = reference["bindings"][key]
            right = native["bindings"][key]
            for kind, field in (("reference", left), ("native", right)):
                require(field.get("value") == binding["expected"], f"{page_id}/{key}: {kind} field value differs")
                require(isinstance(field.get("font", {}).get("pixelSize"), (int, float)), f"{page_id}/{key}: missing {kind} font metrics")
                for coordinate in ("x", "y", "width", "height"):
                    require(isinstance(field.get("rect", {}).get(coordinate), (int, float)), f"{page_id}/{key}: missing {kind} geometry")
            for state in ("visible", "enabled"):
                require(isinstance(left.get(state), bool) and isinstance(right.get(state), bool), f"{page_id}/{key}: missing {state} state")
                require(left[state] == right[state], f"{page_id}/{key}: native {state} state differs from reference")
            binding_differences[key] = {
                "reference": left, "native": right,
                "nativeMinusReferenceRect": {axis: right["rect"][axis] - left["rect"][axis] for axis in ("x", "y", "width", "height")},
                "nativeMinusReferenceFontSize": right["font"]["pixelSize"] - left["font"]["pixelSize"],
            }
        difference = ImageChops.difference(before, after)
        red, green, blue = difference.split()
        maximum = ImageChops.lighter(ImageChops.lighter(red, green), blue)
        histogram = maximum.histogram()
        pixels = size[0] * size[1]
        metrics = {
            "meanAbsoluteRgbError0To255": sum(ImageStat.Stat(difference).mean) / 3,
            "pixelsWithAnyDifferencePercent": 100 * (pixels - histogram[0]) / pixels,
            "pixelsWithAnyChannelDifferenceAbove8Percent": 100 * sum(histogram[9:]) / pixels,
        }
        combined = Image.new("RGB", (size[0] * 2 + 24, size[1] + 48), "#f0f0f0")
        draw = ImageDraw.Draw(combined)
        draw.text((12, 16), f"Electron {reference_manifest['electronVersion']} / original HTML", fill="#333333")
        draw.text((size[0] + 36, 16), f"Qt {native_manifest['qtVersion']} / native widgets", fill="#333333")
        combined.paste(before, (0, 48))
        combined.paste(after, (size[0] + 24, 48))
        combined.save(output / f"comparison-{page_id}.png")
        difference.save(output / f"difference-{page_id}.png")
        results.append({"page": page_id, "width": size[0], "height": size[1], **metrics, "bindings": binding_differences})
    report = {
        "schemaVersion": 1, "fixtureSha256": digest, "captureValidation": "passed",
        "pixelIdentity": all(page["pixelsWithAnyDifferencePercent"] == 0 for page in results),
        "interpretation": "Capture validation checks fixture data, dimensions and selected fields. Pixel, font and geometry differences are reported without masking or a parity pass threshold.",
        "reference": {key: value for key, value in reference_manifest.items() if key != "pages"},
        "native": {key: value for key, value in native_manifest.items() if key != "pages"}, "pages": results,
    }
    (output / "visual-report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = ["# Visual capture report", "", report["interpretation"], "", "| Page | Mean absolute RGB error (0–255) | Pixels with any channel difference > 8 |", "| --- | ---: | ---: |"]
    lines.extend(f"| {page['page']} | {page['meanAbsoluteRgbError0To255']:.4f} | {page['pixelsWithAnyChannelDifferenceAbove8Percent']:.2f}% |" for page in results)
    lines += ["", "Complete field values, geometry, font properties and raw differences are in `visual-report.json`. Exact pixel identity: " + str(report["pixelIdentity"]).lower() + ".", ""]
    (output / "visual-report.md").write_text("\n".join(lines), encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True)
    parser.add_argument("--captures", required=True)
    parser.add_argument("--output", required=True)
    arguments = parser.parse_args()
    try:
        report = compare(arguments.config, arguments.captures, arguments.output)
    except (ValueError, KeyError, OSError, TypeError) as error:
        parser.exit(1, f"Visual capture validation failed: {error}\n")
    print(f"Validated {len(report['pages'])} pages; exact pixel identity: {report['pixelIdentity']}")
