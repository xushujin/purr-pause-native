#!/usr/bin/env python3
"""Bundle native dependencies and Qt plugins without capturing glibc or drivers."""
import argparse
import hashlib
import os
import pathlib
import re
import shutil
import subprocess


BASE_LIBRARIES = re.compile(
    r"^(?:ld-linux[^/]*|ld64[^/]*|lib(?:c|m|dl|rt|pthread|resolv|util|anl|nss_[^/]+)\.so(?:\..*)?)$")


def run(*args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs).strip()


def is_elf(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("appdir", type=pathlib.Path)
    parser.add_argument("--qt-plugins", required=True, type=pathlib.Path)
    args = parser.parse_args()
    appdir = args.appdir.resolve()
    libdir = appdir / "usr/lib"
    libdir.mkdir(parents=True, exist_ok=True)
    plugins = appdir / "usr/plugins"
    # Desktop raster rendering requires XCB; offscreen supports automated checks.
    # Deliberately omit graphics-driver and GTK platform plugins: they belong to
    # the host desktop and can pull incompatible driver stacks into an AppImage.
    plugin_patterns = ["platforms/libqxcb.so", "platforms/libqoffscreen.so",
                       "imageformats/*.so", "iconengines/*.so", "tls/*.so",
                       "platforminputcontexts/*.so", "networkinformation/*.so"]
    for pattern in plugin_patterns:
        matches = list(args.qt_plugins.glob(pattern))
        if not matches and not pattern.startswith("networkinformation/"):
            raise RuntimeError(f"Required Qt plugins are missing: {pattern}")
        for source in matches:
            destination = plugins / source.relative_to(args.qt_plugins)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)

    sources = {}
    pending = [(path, path) for path in appdir.rglob("*") if is_elf(path)]
    # Qt dynamically opens OpenSSL rather than linking its TLS backend to it.
    ldconfig = run("ldconfig", "-p")
    for soname in ("libssl.so.3", "libcrypto.so.3"):
        matches = re.findall(r"^\s*" + re.escape(soname) + r"\s+\([^\n]+\) => (.+)$", ldconfig, re.M)
        if not matches:
            raise RuntimeError(f"Required OpenSSL library is missing: {soname}")
        source = pathlib.Path(matches[0]).resolve()
        destination = libdir / soname
        shutil.copy2(source, destination)
        sources[soname] = source
        pending.append((destination, source))

    visited = set()
    bundled = {}
    while pending:
        destination, original = pending.pop()
        destination = destination.resolve()
        if destination in visited:
            continue
        visited.add(destination)
        output = run("ldd", str(original))
        if "not found" in output:
            raise RuntimeError(f"Unresolved build dependency: {original}\n{output}")
        for name, filename in re.findall(r"^\s*(\S+) => (/\S+) \(", output, re.M):
            if BASE_LIBRARIES.fullmatch(name):
                continue
            source = pathlib.Path(filename).resolve()
            if appdir in source.parents:
                # CMake's private codec install lives in lib/purr-pause. Flatten
                # these into the same directory as Qt to make dlopen predictable.
                if source.parent == libdir:
                    continue
            target = libdir / name
            if name in sources and sources[name] != source:
                if hashlib.sha256(sources[name].read_bytes()).digest() != hashlib.sha256(source.read_bytes()).digest():
                    raise RuntimeError(f"Conflicting library implementations for {name}")
            if not target.exists():
                shutil.copy2(source, target)
                sources[name] = source
                bundled[name] = str(source)
                pending.append((target, source))
        relative = os.path.relpath(libdir, destination.parent)
        rpath = "$ORIGIN" if relative == "." else "$ORIGIN/" + relative
        subprocess.check_call(["patchelf", "--set-rpath", rpath, str(destination)])

    # A portable package may depend on glibc and the kernel, but every other
    # linked library must resolve inside AppDir, even on the build machine.
    env = dict(os.environ, LD_LIBRARY_PATH=str(libdir))
    for binary in sorted(visited):
        glibc_versions = re.findall(r"\bGLIBC_([0-9]+)\.([0-9]+)\b", run("readelf", "--version-info", str(binary)))
        if any((int(major), int(minor)) > (2, 35) for major, minor in glibc_versions):
            raise RuntimeError(f"{binary} requires glibc newer than Ubuntu 22.04; build AppImages on Ubuntu 22.04")
        output = run("ldd", str(binary), env=env)
        if "not found" in output:
            raise RuntimeError(f"Unresolved packaged dependency: {binary}\n{output}")
        for name, filename in re.findall(r"^\s*(\S+) => (/\S+) \(", output, re.M):
            if not BASE_LIBRARIES.fullmatch(name) and appdir not in pathlib.Path(filename).resolve().parents:
                raise RuntimeError(f"External dependency survived bundling: {binary}: {filename}")
        if "/opt/" in run("patchelf", "--print-rpath", str(binary)):
            raise RuntimeError(f"Build prefix survived bundling: {binary}")

    licenses = appdir / "usr/share/doc/purr-pause/appimage-libraries"
    licenses.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name, source in sorted(sources.items()):
        owners = subprocess.run(["dpkg-query", "-S", str(source)], text=True, capture_output=True)
        package = owners.stdout.split(": ", 1)[0] if owners.returncode == 0 else "source-built"
        details = run("dpkg-query", "-W", "-f=${Version}\t${source:Package}\t${source:Version}", package) if package != "source-built" else "-\t-\t-"
        manifest.append(f"{name}\t{package}\t{details}\t{hashlib.sha256(source.read_bytes()).hexdigest()}")
        if package != "source-built":
            copyright_file = pathlib.Path("/usr/share/doc") / package.split(":")[0] / "copyright"
            if copyright_file.exists():
                shutil.copy2(copyright_file, licenses / (package.replace(":", "-") + "-copyright"))
    (licenses / "MANIFEST.tsv").write_text("Library\tUbuntu binary package\tVersion\tSource package\tSource version\tSHA256\n" + "\n".join(manifest) + "\n")
    print(f"Verified {len(visited)} ELF files; bundled dependencies resolve inside AppDir")


if __name__ == "__main__":
    main()
