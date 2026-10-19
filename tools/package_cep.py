#!/usr/bin/env python3
"""Package this plugin's already-built target using CyberEther's .cep manifest format."""

import argparse
import io
import os
from pathlib import Path, PurePosixPath
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
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as temporary:
        temporary_path = Path(temporary.name)
    try:
        with tarfile.open(temporary_path, "w:gz", format=tarfile.USTAR_FORMAT) as archive:
            add_bytes(archive, "manifest.yml", manifest)
            add_file(archive, binary, target_path)
            for source, name in example_paths:
                add_file(archive, source, name)
        temporary_path.replace(output)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


if __name__ == "__main__":
    main()
