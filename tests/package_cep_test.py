#!/usr/bin/env python3
"""Synthetic tests for host runtime linkage and reproducible .cep packaging."""

import hashlib
import importlib.util
from pathlib import Path
import tarfile
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("package_cep", PROJECT / "tools" / "package_cep.py")
PACKAGE_CEP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE_CEP)


class PackageCepTests(unittest.TestCase):
    def test_sdk_dependency_is_rewritten_to_host_and_rpath_added(self):
        dependency = "/tmp/cyberether-sdk-install/lib/libjetstream.dylib"
        dependencies = [dependency, "/usr/lib/libc++.1.dylib"]
        rpaths = ["/tmp/cyberether-sdk-install/lib", "@loader_path"]
        commands = []

        def run_tool(command):
            commands.append(command)
            if command[1] == "-L":
                return "plugin.dylib:\n" + "".join(
                    f"\t{path} (compatibility version 1.0.0, current version 1.0.0)\n"
                    for path in dependencies
                )
            if command[1] == "-l":
                return "".join(
                    f"Load command {index}\n cmd LC_RPATH\n cmdsize 32\n path {path} (offset 12)\n"
                    for index, path in enumerate(rpaths, start=1)
                )
            if command[1] == "-change":
                self.assertEqual(command[2], dependency)
                self.assertEqual(command[3], "@rpath/libjetstream.dylib")
                dependencies[0] = command[3]
                return ""
            if command[1] == "-add_rpath":
                rpaths.append(command[2])
                return ""
            if command[1] == "-delete_rpath":
                rpaths.remove(command[2])
                return ""
            self.fail(f"unexpected tool invocation: {command}")

        PACKAGE_CEP.configure_host_jetstream_runtime(
            Path("plugin.dylib"), run_tool=run_tool,
            find_tool=lambda name: f"/mock-tools/{name}",
        )

        self.assertIn(
            ["/mock-tools/install_name_tool", "-change", dependency,
             "@rpath/libjetstream.dylib", "plugin.dylib"],
            commands,
        )
        self.assertIn(
            ["/mock-tools/install_name_tool", "-add_rpath", "@executable_path", "plugin.dylib"],
            commands,
        )
        self.assertIn(
            ["/mock-tools/install_name_tool", "-delete_rpath", "/tmp/cyberether-sdk-install/lib", "plugin.dylib"],
            commands,
        )
        self.assertEqual(dependencies[0], "@rpath/libjetstream.dylib")
        self.assertEqual(set(rpaths), {"@executable_path", "@loader_path"})

    def test_existing_host_dependency_is_idempotent(self):
        dependencies = ["@rpath/libjetstream.dylib"]
        rpaths = ["@executable_path"]
        mutations = []

        def run_tool(command):
            if command[1] == "-L":
                return "plugin.dylib:\n\t" + dependencies[0] + " (compatibility version 1.0.0, current version 1.0.0)\n"
            if command[1] == "-l":
                return "Load command 1\n cmd LC_RPATH\n cmdsize 32\n path " + rpaths[0] + " (offset 12)\n"
            mutations.append(command)
            return ""

        PACKAGE_CEP.configure_host_jetstream_runtime(
            Path("plugin.dylib"), run_tool=run_tool,
            find_tool=lambda name: f"/mock-tools/{name}",
        )
        self.assertEqual(mutations, [])

    def test_bundle_bytes_are_reproducible_for_identical_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "plugin.dylib"
            example = root / "example.yml"
            binary.write_bytes(b"synthetic plugin bytes")
            example.write_text("version: 2\n", encoding="utf-8")
            manifest = b"metadata:\n  name: 'fixture'\n"
            target_path = "targets/macos-arm64-cpu/plugin.dylib"
            examples = [(example, "examples/example.yml")]
            first = root / "first.cep"
            second = root / "second.cep"

            PACKAGE_CEP.package_bundle(first, manifest, binary, target_path, examples)
            PACKAGE_CEP.package_bundle(second, manifest, binary, target_path, examples)

            self.assertEqual(
                hashlib.sha256(first.read_bytes()).digest(),
                hashlib.sha256(second.read_bytes()).digest(),
            )
            with tarfile.open(first, "r:gz") as archive:
                self.assertEqual(archive.getnames(), [
                    "manifest.yml", target_path, "examples/example.yml",
                ])
                self.assertEqual(archive.extractfile(target_path).read(), binary.read_bytes())
                self.assertEqual(archive.extractfile("examples/example.yml").read(), b"version: 2\n")


if __name__ == "__main__":
    unittest.main()
