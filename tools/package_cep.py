#!/usr/bin/env python3
"""Package this plugin's already-built target using CyberEther's .cep manifest format."""

import argparse
import gzip
import io
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile


def quote(value):
    return "'" + value.replace("'", "''") + "'"


def version(value):
    parts = value.split(".")
    if len(parts) != 3 or any(not part.isascii() or not part.isdigit() or int(part) > 255 for part in parts):
        raise argparse.ArgumentTypeError("version must use x.y.z with components from 0 to 255")
    return value


def target(value):
    fields = {}
    for field in value.split(","):
        key, separator, item = field.partition("=")
        if not separator:
            raise argparse.ArgumentTypeError("target fields must use key=value")
        fields[key.strip()] = item.strip()
    if set(fields) != {"path", "system", "device", "arch"}:
        raise argparse.ArgumentTypeError("target needs path, system, device, and arch")
    system = {"darwin": "macos", "mac": "macos", "osx": "macos"}.get(fields["system"].lower(), fields["system"].lower())
    arch = {"aarch64": "arm64", "amd64": "x86_64", "x64": "x86_64"}.get(fields["arch"].lower(), fields["arch"].lower())
    return {"source": Path(fields["path"]), "system": system, "device": fields["device"].lower(), "arch": arch}


def add_bytes(tar, name, data):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = 0o644
    info.mtime = info.uid = info.gid = 0
    info.uname = info.gname = ""
    tar.addfile(info, io.BytesIO(data))


def add_file(tar, path, name):
    if not path.is_file():
        raise SystemExit(f"input file does not exist: {path}")
    info = tar.gettarinfo(str(path), name)
    info.mode = 0o755 if os.access(path, os.X_OK) else 0o644
    info.mtime = info.uid = info.gid = 0
    info.uname = info.gname = ""
    with path.open("rb") as source:
        tar.addfile(info, source)


def parse_macho_dependencies(output):
    """Return dylib install names reported by ``otool -L``."""
    dependencies = []
    for line in output.splitlines()[1:]:
        match = re.match(r"\s*(.+?)\s+\(compatibility version ", line)
        if match:
            dependencies.append(match.group(1))
    return dependencies


def parse_macho_rpaths(output):
    """Return LC_RPATH values reported by ``otool -l``."""
    paths = []
    lines = output.splitlines()
    for index, line in enumerate(lines):
        if line.strip() != "cmd LC_RPATH":
            continue
        for candidate in lines[index + 1:index + 4]:
            match = re.match(r"\s*path\s+(.+?)\s+\(offset \d+\)", candidate)
            if match:
                paths.append(match.group(1))
                break
    return paths


def _run_tool(command):
    try:
        return subprocess.run(command, check=True, text=True, capture_output=True).stdout
    except FileNotFoundError as error:
        raise SystemExit(f"required macOS packaging tool is missing: {command[0]}") from error
    except subprocess.CalledProcessError as error:
        detail = error.stderr.strip() or error.stdout.strip() or str(error)
        raise SystemExit(f"macOS runtime relocation failed: {detail}") from error


def configure_host_jetstream_runtime(binary, run_tool=_run_tool, find_tool=shutil.which):
    """Make the packaged plugin resolve Jetstream from CyberEther's app bundle.

    ``@executable_path`` is CyberEther.app/Contents/MacOS for a loaded plugin,
    where the host's ``libjetstream.dylib`` is installed. Never carry the SDK's
    absolute build/install path into a distributable .cep.
    """
    otool = find_tool("otool")
    install_name_tool = find_tool("install_name_tool")
    if not otool or not install_name_tool:
        raise SystemExit("macOS .cep packaging requires otool and install_name_tool")

    dependencies = parse_macho_dependencies(run_tool([otool, "-L", str(binary)]))
    jetstream = [item for item in dependencies if PurePosixPath(item).name == "libjetstream.dylib"]
    if len(jetstream) != 1:
        raise SystemExit("plugin must link exactly one dynamic libjetstream.dylib dependency")
    host_install_name = "@rpath/libjetstream.dylib"
    if jetstream[0] != host_install_name:
        run_tool([install_name_tool, "-change", jetstream[0], host_install_name, str(binary)])

    rpaths = parse_macho_rpaths(run_tool([otool, "-l", str(binary)]))
    for path in rpaths:
        if path not in {"@executable_path", "@loader_path"}:
            run_tool([install_name_tool, "-delete_rpath", path, str(binary)])
    if "@executable_path" not in rpaths:
        run_tool([install_name_tool, "-add_rpath", "@executable_path", str(binary)])

    final_dependencies = parse_macho_dependencies(run_tool([otool, "-L", str(binary)]))
    final_jetstream = [item for item in final_dependencies if PurePosixPath(item).name == "libjetstream.dylib"]
    final_rpaths = parse_macho_rpaths(run_tool([otool, "-l", str(binary)]))
    if final_jetstream != [host_install_name] or "@executable_path" not in final_rpaths:
        raise SystemExit("packaged plugin does not resolve Jetstream through the CyberEther host app")
    if any(path not in {"@executable_path", "@loader_path"} for path in final_rpaths):
        raise SystemExit("packaged plugin retains a non-portable Mach-O runpath")
    allowed_dependency_prefixes = ("@rpath/", "@loader_path/", "@executable_path/", "/usr/lib/", "/System/Library/")
    unexpected = [
        path for path in final_dependencies
        if not path.startswith(allowed_dependency_prefixes)
    ]
    if unexpected:
        raise SystemExit("packaged plugin retains non-system absolute dependencies: " + ", ".join(unexpected))


def package_bundle(output, manifest, binary, target_path, example_paths):
    """Write a reproducible gzip-compressed USTAR .cep archive."""
    with output.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                add_bytes(archive, "manifest.yml", manifest)
                add_file(archive, binary, target_path)
                for source, name in example_paths:
                    add_file(archive, source, name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", required=True, type=version)
    parser.add_argument("--minimum-jetstream-version", required=True, type=version)
    parser.add_argument("--target", required=True, type=target)
    parser.add_argument("--example", action="append", default=[])
    args = parser.parse_args()

    output = Path(args.output)
    if output.suffix.lower() != ".cep":
        raise SystemExit("output path must end in .cep")
    binary = args.target["source"]
    if not binary.is_file():
        raise SystemExit(f"plugin target does not exist: {binary}")
    target_path = PurePosixPath("targets", f"{args.target['system']}-{args.target['arch']}-{args.target['device']}", binary.name).as_posix()
    example_paths = [(Path(item), PurePosixPath("examples", Path(item).name).as_posix()) for item in args.example]
    if len({name for _, name in example_paths}) != len(example_paths) or target_path in {name for _, name in example_paths}:
        raise SystemExit("bundle input paths must be unique")

    lines = [
        "metadata:",
        f"  name: {quote(args.name)}",
        f"  version: {quote(args.version)}",
        f"  minimumJetstreamVersion: {quote(args.minimum_jetstream_version)}",
        "",
        "targets:",
        f"  - path: {quote(target_path)}",
        f"    system: {quote(args.target['system'])}",
        f"    device: {quote(args.target['device'])}",
        f"    arch: {quote(args.target['arch'])}",
        "",
        "examples:",
    ]
    if example_paths:
        lines.extend(f"  - path: {quote(name)}" for _, name in example_paths)
    else:
        lines[-1] = "examples: []"
    manifest = ("\n".join(lines) + "\n").encode()

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent, prefix="cep-package-") as staging:
        staged_binary = Path(staging) / binary.name
        shutil.copy2(binary, staged_binary)
        if args.target["system"] == "macos":
            configure_host_jetstream_runtime(staged_binary)

        with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as temporary:
            temporary_path = Path(temporary.name)
        try:
            package_bundle(temporary_path, manifest, staged_binary, target_path, example_paths)
            temporary_path.replace(output)
        finally:
            if temporary_path.exists():
                temporary_path.unlink()


if __name__ == "__main__":
    main()
