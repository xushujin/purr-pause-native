"""Portable tests of the macOS dependency graph and load-command validation.

The Mach-O tools are simulated here; DMG mounting, codesigning and Cocoa startup
are performed by verify-macos-package.sh on actual macOS CI runners.
"""
import copy
import importlib.util
import pathlib
import sys
import struct
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts/bundle-macos-libs.py"
spec = importlib.util.spec_from_file_location("purr_macos_bundle", SCRIPT)
bundle = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = bundle
spec.loader.exec_module(bundle)


def macho_fixture(identifier, code=b"\x01\x02\x03\x04", uuid=b"0123456789abcdef", signature=b""):
    """A real little-endian Mach-O header and section for identity regression."""
    name = identifier.encode() + b"\0"
    identity_size = (24 + len(name) + 7) // 8 * 8
    identity = struct.pack("<6I", 0xD, identity_size, 24, 0, 0x10000, 0x10000)
    identity += name + bytes(identity_size - 24 - len(name))
    uuid_command = struct.pack("<II", 0x1B, 24) + uuid
    version = struct.pack("<6I", 0x32, 24, 1, 0xC0000, 0xF0000, 0)
    commands_size = 152 + len(identity) + len(uuid_command) + len(version)
    offset = 32 + commands_size
    segment = struct.pack("<II16s4Q4I", 0x19, 152, b"__TEXT", 0x100000000, 4096,
                          0, offset + len(code), 7, 5, 1, 0)
    section = struct.pack("<16s16sQQ8I", b"__text", b"__TEXT", 0x100000000 + offset,
                          len(code), offset, 2, 0, 0, 0x80000400, 0, 0, 0)
    header = struct.pack("<8I", 0xFEEDFACF, 0x0100000C, 0, 6, 4, commands_size, 0, 0)
    return header + segment + section + identity + uuid_command + version + code + signature


def commands(info, heading="example (architecture arm64):"):
    blocks = []
    def add(command, value):
        blocks.append(f"Load command {len(blocks)}\n          cmd {command}\n      cmdsize 80\n{value}\n")
    if info.get("id"):
        add("LC_ID_DYLIB", f"         name {info['id']} (offset 24)")
    for dependency in info.get("dependencies", []):
        add("LC_LOAD_DYLIB", f"         name {dependency} (offset 24)")
    for path in info.get("rpaths", []):
        add("LC_RPATH", f"         path {path} (offset 12)")
    add("LC_BUILD_VERSION", f"     platform {info.get('platform', '1')}\n        minos {info.get('minimum', '12.0')}\n          sdk 15.0")
    return heading + "\n" + "".join(blocks)


class FakeMachO:
    def __init__(self):
        self.models = {}
        self.blobs = {}
        self.mutations = []

    def add(self, path, **properties):
        path.parent.mkdir(parents=True, exist_ok=True)
        content = f"Mach-O fixture {len(self.blobs)}".encode()
        path.write_bytes(content)
        info = {"architectures": ["arm64"], "rpaths": [], "dependencies": [], **properties}
        self.models[path.resolve()] = info
        self.blobs[content] = copy.deepcopy(info)
        return path

    def model(self, path):
        path = pathlib.Path(path).resolve()
        if path not in self.models:
            self.models[path] = copy.deepcopy(self.blobs[path.read_bytes()])
        return self.models[path]

    def run(self, *arguments):
        tool = arguments[0]
        path = pathlib.Path(arguments[-1])
        if tool == "file":
            return "Mach-O universal binary" if path.read_bytes().startswith(b"Mach-O") else "data"
        info = self.model(path)
        if tool == "lipo":
            return " ".join(info["architectures"])
        if tool == "otool":
            return commands(info, f"{path} (architecture {arguments[2]}):")
        raise AssertionError(arguments)

    def mutate(self, arguments):
        self.mutations.append(arguments)
        tool = arguments[0]
        if tool == "lipo":
            original, target = pathlib.Path(arguments[1]), pathlib.Path(arguments[-1])
            model = copy.deepcopy(self.model(original))
            model["architectures"] = [arguments[3]]
            target.write_bytes(original.read_bytes())
            # The next os.replace keeps the same original pathname.
            self.models[original.resolve()] = model
            return 0
        self.assert_tool(tool, "install_name_tool")
        info = self.model(arguments[-1])
        option = arguments[1]
        if option == "-id":
            info["id"] = arguments[2]
        elif option == "-change":
            info["dependencies"] = [arguments[3] if value == arguments[2] else value for value in info["dependencies"]]
        elif option == "-delete_rpath":
            info["rpaths"] = [value for value in info["rpaths"] if value != arguments[2]]
        elif option == "-add_rpath":
            info["rpaths"].append(arguments[2])
        else:
            raise AssertionError(arguments)
        return 0

    @staticmethod
    def assert_tool(actual, expected):
        if actual != expected:
            raise AssertionError(actual)


class MacPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="purr mac package ")
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        self.app = self.root / "Applications with spaces/Purr Pause.app"
        self.executable = self.app / "Contents/MacOS/purr-pause"
        self.frameworks = self.app / "Contents/Frameworks"
        self.dependencies = self.root / "build dependencies"
        self.tools = FakeMachO()
        self.addCleanup(patch.stopall)
        patch.object(bundle, "run", self.tools.run).start()
        patch.object(bundle.subprocess, "check_call", self.tools.mutate).start()

    def executable_fixture(self, **properties):
        return self.tools.add(self.executable, **properties)

    def test_load_commands_do_not_treat_universal_heading_or_id_as_dependency(self):
        text = commands({"id": "/build dir/libcodec.dylib", "dependencies": ["@rpath/libvpx.dylib"],
                         "rpaths": ["@loader_path/../Frameworks"]}, "Universal binary (architecture arm64):")
        info = bundle.parse_load_commands(text)
        self.assertEqual(info.identifier, "/build dir/libcodec.dylib")
        self.assertEqual(info.dependencies, ("@rpath/libvpx.dylib",))
        self.assertEqual(info.rpaths, ("@loader_path/../Frameworks",))
        self.assertEqual(info.minimum_versions, ("12.0",))

    def test_legacy_deployment_command(self):
        info = bundle.parse_load_commands("x:\nLoad command 0\ncmd LC_VERSION_MIN_MACOSX\ncmdsize 16\nversion 11.0\nsdk 12.0\n")
        self.assertEqual(info.minimum_versions, ("11.0",))
        self.assertEqual(info.platforms, ("macos",))

    def test_transitive_graph_is_copied_thinned_and_relocated(self):
        codec = self.dependencies / "libcodec.dylib"
        vpx = self.dependencies / "libvpx.dylib"
        self.executable_fixture(architectures=["x86_64", "arm64"], rpaths=[str(self.dependencies)],
                                dependencies=["@rpath/libcodec.dylib", "/usr/lib/libSystem.B.dylib"])
        self.tools.add(codec, id=str(codec), architectures=["x86_64", "arm64"], dependencies=[str(vpx)])
        self.tools.add(vpx, id=str(vpx), minimum="11.0")
        deployment = bundle.Bundle(self.app, "arm64", [self.dependencies])
        self.assertEqual(deployment.deploy(), 3)
        self.assertEqual(self.tools.model(self.executable)["architectures"], ["arm64"])
        self.assertEqual(self.tools.model(self.frameworks / codec.name)["dependencies"], ["@rpath/libvpx.dylib"])
        self.assertEqual(self.tools.model(self.frameworks / vpx.name)["id"], "@rpath/libvpx.dylib")
        self.assertNotIn(str(self.dependencies), self.tools.model(self.executable)["rpaths"])
        count = len(self.tools.mutations)
        self.assertEqual(deployment.verify(), 3)
        self.assertEqual(len(self.tools.mutations), count, "Verification of a signed app must not mutate it")

    def test_different_sources_cannot_overwrite_same_dylib_basename(self):
        one = self.root / "first/libcollision.dylib"
        two = self.root / "second/libcollision.dylib"
        self.tools.add(one, id=str(one))
        self.tools.add(two, id=str(two), minimum="11.0")
        self.executable_fixture(dependencies=[str(one), str(two)])
        with self.assertRaisesRegex(RuntimeError, "Conflicting dylibs"):
            bundle.Bundle(self.app, "arm64").deploy()

    def test_verification_never_resolves_using_build_search_paths(self):
        self.executable_fixture(rpaths=["@executable_path/../Frameworks"], dependencies=["@rpath/libmissing.dylib"])
        self.tools.add(self.dependencies / "libmissing.dylib", id="@rpath/libmissing.dylib")
        with self.assertRaisesRegex(RuntimeError, "Unresolved dependency"):
            bundle.Bundle(self.app, "arm64", [self.dependencies]).verify()

    def test_absolute_build_rpath_is_rejected_after_install(self):
        self.executable_fixture(rpaths=[str(self.dependencies)])
        with self.assertRaisesRegex(RuntimeError, "non-relocatable RPATH"):
            bundle.Bundle(self.app, "arm64").verify()

    def test_absolute_path_inside_build_bundle_is_rewritten_for_relocation(self):
        self.frameworks.mkdir(parents=True)
        self.executable_fixture(rpaths=[str(self.frameworks)])
        deployment = bundle.Bundle(self.app, "arm64")
        self.assertEqual(deployment.deploy(), 1)
        self.assertEqual(self.tools.model(self.executable)["rpaths"], ["@executable_path/../Frameworks"])

    def test_existing_dylib_cannot_be_silently_replaced(self):
        source = self.dependencies / "libexisting.dylib"
        self.tools.add(source, id=str(source))
        self.tools.add(self.frameworks / source.name, id="@rpath/libexisting.dylib", minimum="11.0")
        self.executable_fixture(dependencies=[str(source)])
        with self.assertRaisesRegex(RuntimeError, "Existing bundle library conflicts"):
            bundle.Bundle(self.app, "arm64").deploy()

    def test_already_deployed_copy_can_have_rewritten_install_name_and_signature(self):
        source = self.dependencies / "libdeployed.dylib"
        destination = self.frameworks / source.name
        source.parent.mkdir(parents=True)
        destination.parent.mkdir(parents=True)
        source.write_bytes(macho_fixture("/very/long/build/location/libdeployed.dylib"))
        destination.write_bytes(macho_fixture("@rpath/libdeployed.dylib", signature=b"new ad hoc signature"))
        self.assertNotEqual(source.read_bytes(), destination.read_bytes())
        self.assertEqual(bundle.content_identity(source, "arm64"), bundle.content_identity(destination, "arm64"))
        bundle.Bundle(self.app, "arm64").claim(source, destination)

    def test_modified_code_or_link_uuid_still_conflicts_after_install_name_change(self):
        source = self.dependencies / "libdeployed.dylib"
        destination = self.frameworks / source.name
        source.parent.mkdir(parents=True)
        destination.parent.mkdir(parents=True)
        source.write_bytes(macho_fixture(str(source)))
        for properties in ({"code": b"different code"}, {"uuid": b"differentlinkid!"}):
            destination.write_bytes(macho_fixture("@rpath/libdeployed.dylib", **properties))
            with self.assertRaisesRegex(RuntimeError, "Existing bundle library conflicts"):
                bundle.Bundle(self.app, "arm64").claim(source, destination)

    def test_deployed_thin_copy_matches_selected_universal_slice(self):
        thin = macho_fixture("/build/libuniversal.dylib")
        source = self.root / "universal.dylib"
        destination = self.root / "thin.dylib"
        fat_header = struct.pack(">7I", 0xCAFEBABE, 1, 0x0100000C, 0, 4096, len(thin), 12)
        source.write_bytes(fat_header + bytes(4096 - len(fat_header)) + thin)
        destination.write_bytes(macho_fixture("@rpath/libuniversal.dylib"))
        self.assertEqual(bundle.content_identity(source, "arm64"), bundle.content_identity(destination, "arm64"))
        self.assertIsNone(bundle.content_identity(source, "x86_64"))

    def test_malformed_macho_never_uses_relaxed_identity(self):
        path = self.root / "truncated.dylib"
        path.write_bytes(macho_fixture("@rpath/libbroken.dylib")[:48])
        self.assertIsNone(bundle.content_identity(path, "arm64"))

    def test_read_only_verification_rejects_absolute_dependency_even_inside_app(self):
        library = self.tools.add(self.frameworks / "libabsolute.dylib", id="@rpath/libabsolute.dylib")
        self.executable_fixture(dependencies=[str(library)])
        with self.assertRaisesRegex(RuntimeError, "Non-relocatable dependency"):
            bundle.Bundle(self.app, "arm64").verify()

    def test_symlink_dependency_escape_is_rejected(self):
        external = self.tools.add(self.dependencies / "liboutside.dylib", id="@rpath/liboutside.dylib")
        self.frameworks.mkdir(parents=True)
        (self.frameworks / external.name).symlink_to(external)
        self.executable_fixture(rpaths=["@executable_path/../Frameworks"], dependencies=["@rpath/liboutside.dylib"])
        with self.assertRaisesRegex(RuntimeError, "External dependency"):
            bundle.Bundle(self.app, "arm64").verify()

    def test_newer_patch_deployment_target_is_rejected(self):
        self.executable_fixture(minimum="12.0.1")
        with self.assertRaisesRegex(RuntimeError, "requires macOS 12.0.1"):
            bundle.Bundle(self.app, "arm64").verify()

    def test_wrong_architecture_and_platform_are_rejected(self):
        self.executable_fixture(architectures=["x86_64"])
        with self.assertRaisesRegex(RuntimeError, "expected arm64"):
            bundle.Bundle(self.app, "arm64").verify()
        self.tools.model(self.executable)["architectures"] = ["arm64"]
        self.tools.model(self.executable)["platform"] = "2"
        with self.assertRaisesRegex(RuntimeError, "Unexpected Mach-O platform"):
            bundle.Bundle(self.app, "arm64").verify()


if __name__ == "__main__":
    unittest.main()
