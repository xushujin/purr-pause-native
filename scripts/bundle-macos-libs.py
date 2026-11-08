#!/usr/bin/env python3
"""Deploy Mach-O dependencies or verify an installed bundle without modifying it."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import os
import pathlib
import re
import shutil
import struct
import subprocess


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def macho(path):
    return path.is_file() and not path.is_symlink() and "Mach-O" in run("file", "-b", str(path))


@dataclass(frozen=True)
class Metadata:
    identifier: str | None
    dependencies: tuple[str, ...]
    rpaths: tuple[str, ...]
    minimum_versions: tuple[str, ...]
    platforms: tuple[str, ...]


def parse_load_commands(output):
    """Read explicit load commands, not otool headings or universal slice labels."""
    identifier = None
    dependencies, paths, versions, platforms = [], [], [], []
    for block in re.split(r"(?m)^Load command \d+\s*$", output)[1:]:
        command = re.search(r"\bcmd (LC_\w+)", block)
        if not command:
            continue
        command = command.group(1)
        if command in {"LC_ID_DYLIB", "LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_UPWARD_DYLIB"}:
            name = re.search(r"\bname (.*?) \(offset \d+\)", block)
            if not name:
                raise RuntimeError(f"Missing dylib name in {command}")
            if command == "LC_ID_DYLIB":
                identifier = name.group(1)
            else:
                dependencies.append(name.group(1))
        elif command == "LC_RPATH":
            value = re.search(r"\bpath (.*?) \(offset \d+\)", block)
            if not value:
                raise RuntimeError("Missing path in LC_RPATH")
            paths.append(value.group(1))
        elif command == "LC_BUILD_VERSION":
            version = re.search(r"\bminos ([\d.]+)", block)
            platform = re.search(r"\bplatform (\S+)", block)
            if version:
                versions.append(version.group(1))
            if platform:
                platforms.append(platform.group(1).lower())
        elif command == "LC_VERSION_MIN_MACOSX":
            version = re.search(r"\bversion ([\d.]+)", block)
            if version:
                versions.append(version.group(1))
                platforms.append("macos")
    return Metadata(identifier, tuple(dict.fromkeys(dependencies)), tuple(paths), tuple(versions), tuple(platforms))


def metadata(path, arch):
    return parse_load_commands(run("otool", "-arch", arch, "-l", str(path)))


def system_library(name):
    return name.startswith(("/System/Library/", "/usr/lib/"))


def within(path, directory):
    path, directory = path.resolve(), directory.resolve()
    return path == directory or directory in path.parents


def content_identity(path, arch):
    """Identify a deployed copy despite thinning, install-name edits and signing.

    macdeployqt can rewrite an existing dylib before this script sees it. Compare
    the original link UUID, target metadata and every file-backed section rather
    than code-signature/load-command bytes. Unknown or malformed binaries never
    receive a relaxed identity check.
    """
    data = pathlib.Path(path).read_bytes()
    cpu = {"x86_64": 0x01000007, "arm64": 0x0100000C}[arch]
    fat = {b"\xca\xfe\xba\xbe": (">", False), b"\xbe\xba\xfe\xca": ("<", False),
           b"\xca\xfe\xba\xbf": (">", True), b"\xbf\xba\xfe\xca": ("<", True)}
    try:
        if data[:4] in fat:
            endian, wide = fat[data[:4]]
            count = struct.unpack_from(endian + "I", data, 4)[0]
            if count > 64:
                return None
            selected = None
            for index in range(count):
                position = 8 + index * (32 if wide else 20)
                kind, _subtype = struct.unpack_from(endian + "II", data, position)
                offset, size = struct.unpack_from(endian + ("QQ" if wide else "II"), data, position + 8)
                if kind == cpu:
                    if selected is not None or offset + size > len(data):
                        return None
                    selected = data[offset:offset + size]
            if selected is None:
                return None
            data = selected
        if data[:4] != b"\xcf\xfa\xed\xfe":
            return None
        _magic, kind, subtype, filetype, count, command_size, flags, _reserved = struct.unpack_from("<8I", data)
        if kind != cpu or count > 4096 or 32 + command_size > len(data):
            return None
        digest = hashlib.sha256(struct.pack("<4I", kind, subtype, filetype, flags))
        uuid, sections = None, 0
        position = 32
        for _ in range(count):
            command, size = struct.unpack_from("<II", data, position)
            if size < 8 or position + size > 32 + command_size:
                return None
            if command == 0x1B:  # LC_UUID remains unchanged by deploy/sign/strip.
                if size != 24 or uuid is not None:
                    return None
                uuid = data[position + 8:position + 24]
                digest.update(uuid)
            elif command in (0x24, 0x32):  # macOS target and SDK versions
                digest.update(data[position:position + size])
            elif command == 0x19:  # LC_SEGMENT_64
                if size < 72:
                    return None
                segment = data[position + 8:position + 24].rstrip(b"\0")
                section_count = struct.unpack_from("<I", data, position + 64)[0]
                if 72 + 80 * section_count > size:
                    return None
                for index in range(section_count):
                    section = position + 72 + 80 * index
                    name = data[section:section + 32]
                    section_size = struct.unpack_from("<Q", data, section + 40)[0]
                    offset = struct.unpack_from("<I", data, section + 48)[0]
                    section_flags = struct.unpack_from("<I", data, section + 64)[0]
                    if segment == b"__LINKEDIT":
                        continue
                    digest.update(name + struct.pack("<QI", section_size, section_flags))
                    # Zero-fill sections have no bytes stored in the file.
                    if (section_flags & 0xFF) not in (1, 0xC, 0x12):
                        if offset + section_size > len(data):
                            return None
                        digest.update(data[offset:offset + section_size])
                    sections += 1
            position += size
        return digest.hexdigest() if uuid is not None and sections else None
    except (struct.error, OverflowError, ValueError):
        return None


def validate_target(path, arch, info):
    architectures = run("lipo", "-archs", str(path)).split()
    if arch not in architectures:
        raise RuntimeError(f"{path} has {architectures}, expected {arch}")
    if not info.minimum_versions:
        raise RuntimeError(f"No macOS deployment target in {path}")
    if not info.platforms or any(value not in {"1", "macos", "macosx"} for value in info.platforms):
        raise RuntimeError(f"Unexpected Mach-O platform in {path}: {info.platforms}")
    for value in info.minimum_versions:
        version = tuple(int(part) for part in value.split("."))
        if (version + (0, 0, 0))[:3] > (12, 0, 0):
            raise RuntimeError(f"{path} requires macOS {value}, above supported 12.0")


class Bundle:
    def __init__(self, app, arch, search=()):
        self.app = pathlib.Path(app).resolve()
        self.arch = arch
        self.search = tuple(pathlib.Path(path).resolve() for path in search)
        self.frameworks = self.app / "Contents/Frameworks"
        self.executable = self.app / "Contents/MacOS/purr-pause"
        self.original_metadata = {}
        self.claims = {}

    def expand(self, value, owner):
        return pathlib.Path(value.replace("@loader_path", str(owner.parent)).replace("@executable_path", str(self.executable.parent)))

    def resolve(self, name, owner, info, executable_info, allow_search=False):
        if name.startswith("@rpath/"):
            tail = name[len("@rpath/"):]
            candidates = [self.expand(path, owner) / tail for path in info.rpaths]
            candidates += [self.expand(path, self.executable) / tail for path in executable_info.rpaths]
            if allow_search:
                candidates += [self.frameworks / tail] + [directory / tail for directory in self.search]
        else:
            candidates = [self.expand(name, owner)]
        for candidate in candidates:
            if candidate.is_absolute() and candidate.is_file():
                return candidate.resolve()
        raise RuntimeError(f"Unresolved dependency {name!r} in {owner}")

    def thin(self, binary):
        architectures = run("lipo", "-archs", str(binary)).split()
        if self.arch not in architectures:
            raise RuntimeError(f"{binary} has {architectures}, expected {self.arch}")
        if len(architectures) > 1:
            temporary = binary.with_name(binary.name + ".purr-thin")
            subprocess.check_call(["lipo", str(binary), "-thin", self.arch, "-output", str(temporary)])
            temporary.chmod(binary.stat().st_mode & 0o777)
            os.replace(temporary, binary)

    def claim(self, source, destination):
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        prior = self.claims.get(destination)
        if prior is not None and prior != digest:
            raise RuntimeError(f"Conflicting dylibs map to {destination}: {source}")
        if prior is None and destination.exists() and destination.resolve() != source.resolve():
            if hashlib.sha256(destination.read_bytes()).hexdigest() != digest:
                original_identity = content_identity(source, self.arch)
                deployed_identity = content_identity(destination, self.arch)
                if original_identity is None or original_identity != deployed_identity:
                    raise RuntimeError(f"Existing bundle library conflicts with {source}: {destination}")
        self.claims[destination] = digest

    def deploy(self):
        self.frameworks.mkdir(parents=True, exist_ok=True)
        executable_info = metadata(self.executable, self.arch)
        pending = [(path, path) for path in self.app.rglob("*") if macho(path)]
        seen = set()
        while pending:
            owner, original = pending.pop()
            owner, original = owner.resolve(), original.resolve()
            if owner in seen:
                continue
            seen.add(owner)
            if original not in self.original_metadata:
                self.original_metadata[original] = metadata(original, self.arch)
            info = self.original_metadata[original]
            validate_target(original, self.arch, info)
            self.thin(owner)
            if info.identifier and within(owner, self.frameworks):
                subprocess.check_call(["install_name_tool", "-id", "@rpath/" + str(owner.relative_to(self.frameworks)), str(owner)])
            for name in info.dependencies:
                if system_library(name):
                    continue
                source = self.resolve(name, original, info, executable_info, allow_search=True)
                if within(source, self.app):
                    destination = source
                    if not within(destination, self.frameworks):
                        raise RuntimeError(f"Linked library is outside Contents/Frameworks: {destination}")
                else:
                    if any(part.endswith(".framework") for part in source.parts):
                        raise RuntimeError(f"macdeployqt did not deploy framework {source}")
                    destination = self.frameworks / source.name
                    self.claim(source, destination)
                    if not destination.exists():
                        shutil.copy2(source, destination)
                        destination.chmod(0o755)
                    pending.append((destination, source))
                replacement = "@rpath/" + str(destination.relative_to(self.frameworks))
                if name != replacement:
                    subprocess.check_call(["install_name_tool", "-change", name, replacement, str(owner)])
            current = metadata(owner, self.arch)
            for path in dict.fromkeys(current.rpaths):
                expanded = self.expand(path, owner)
                if not path.startswith(("@loader_path", "@executable_path")) or not within(expanded, self.app):
                    subprocess.check_call(["install_name_tool", "-delete_rpath", path, str(owner)])
            if "@executable_path/../Frameworks" not in metadata(owner, self.arch).rpaths:
                subprocess.check_call(["install_name_tool", "-add_rpath", "@executable_path/../Frameworks", str(owner)])
        return self.verify()

    def verify(self):
        binaries = sorted(path for path in self.app.rglob("*") if macho(path))
        if self.executable not in binaries:
            raise RuntimeError(f"Missing Mach-O executable: {self.executable}")
        executable_info = metadata(self.executable, self.arch)
        for binary in binaries:
            info = metadata(binary, self.arch)
            validate_target(binary, self.arch, info)
            if run("lipo", "-archs", str(binary)).split() != [self.arch]:
                raise RuntimeError(f"Unexpected extra architecture in per-architecture package: {binary}")
            if info.identifier and not info.identifier.startswith("@rpath/"):
                raise RuntimeError(f"Non-relocatable dylib identifier in {binary}: {info.identifier}")
            for path in info.rpaths:
                expanded = self.expand(path, binary)
                if not path.startswith(("@loader_path", "@executable_path")) or not within(expanded, self.app):
                    raise RuntimeError(f"External or non-relocatable RPATH in {binary}: {path}")
            for name in info.dependencies:
                if system_library(name):
                    continue
                if not name.startswith(("@rpath/", "@loader_path/", "@executable_path/")):
                    raise RuntimeError(f"Non-relocatable dependency in {binary}: {name}")
                resolved = self.resolve(name, binary, info, executable_info)
                if not within(resolved, self.app):
                    raise RuntimeError(f"External dependency survived bundling: {binary}: {name}")
        print(f"Verified {len(binaries)} Mach-O files: {self.arch}, macOS 12.0, relocatable dependencies")
        return len(binaries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=pathlib.Path)
    parser.add_argument("--search", action="append", default=[], type=pathlib.Path)
    parser.add_argument("--arch", required=True, choices=["arm64", "x86_64"])
    parser.add_argument("--verify-only", action="store_true", help="Inspect a signed or installed app without modifying it")
    args = parser.parse_args()
    bundle = Bundle(args.app, args.arch, args.search)
    bundle.verify() if args.verify_only else bundle.deploy()


if __name__ == "__main__":
    main()
