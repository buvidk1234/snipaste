"""Exercise the real Windows installer against isolated temporary packages."""

import hashlib
import json
import os
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/updates/install-windows.ps1"
POWERSHELL = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"


class WindowsInstallerTest(unittest.TestCase):
    def run_update(self, root, entries, expected_hash=None, without_hash_cmdlet=False):
        root = Path(root).resolve()
        self.assertTrue(root.is_relative_to(Path(tempfile.gettempdir()).resolve()))
        destination = root / "application"
        destination.mkdir(exist_ok=True)
        (destination / "snipaste.exe").write_bytes(b"original executable")
        (destination / "Qt6Core.dll").write_bytes(b"original runtime")
        (destination / "config.ini").write_bytes(b"user settings")
        archive = root / "update.zip"
        with zipfile.ZipFile(archive, "w") as package:
            for name, data in entries.items():
                package.writestr(name, data)
        manifest = root / "install.json"
        manifest.write_text(json.dumps({
            "archive": str(archive), "destination": str(destination),
            "sha256": expected_hash or hashlib.sha256(archive.read_bytes()).hexdigest(),
            "processId": 2147483647, "restart": False, "arguments": [],
        }), encoding="utf-8")
        command = [
            str(POWERSHELL), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
            "-File", str(SCRIPT), "-Manifest", str(manifest),
        ]
        if without_hash_cmdlet:
            wrapper = root / "without-filehash.ps1"
            wrapper.write_text(
                "param([string]$Installer, [string]$Manifest)\n"
                "Import-Module Microsoft.PowerShell.Utility -Global\n"
                "function global:Get-FileHash { throw 'Get-FileHash is unavailable.' }\n"
                "& $Installer -Manifest $Manifest\n"
                "exit $LASTEXITCODE\n", encoding="utf-8")
            command = command[:5] + ["-File", str(wrapper), "-Installer", str(SCRIPT),
                                     "-Manifest", str(manifest)]
        result = subprocess.run(command, capture_output=True, timeout=20)
        status = json.loads((root / "status.json").read_text(encoding="utf-8-sig"))
        return destination, result.returncode, status

    def test_installs_runtime_and_preserves_settings(self):
        with tempfile.TemporaryDirectory(prefix="snipaste-update-test-") as root:
            destination, code, status = self.run_update(root, {
                "snipaste.exe": b"new executable", "Qt6Core.dll": b"new runtime",
                "plugins/platforms/qwindows.dll": b"new plugin",
            })
            self.assertEqual(code, 0, status)
            self.assertEqual(status["state"], "installed")
            self.assertEqual((destination / "snipaste.exe").read_bytes(), b"new executable")
            self.assertEqual((destination / "Qt6Core.dll").read_bytes(), b"new runtime")
            self.assertEqual((destination / "config.ini").read_bytes(), b"user settings")
            self.assertEqual((destination / "plugins/platforms/qwindows.dll").read_bytes(), b"new plugin")

    def test_installs_without_file_hash_cmdlet(self):
        with tempfile.TemporaryDirectory(prefix="snipaste-update-test-") as root:
            destination, code, status = self.run_update(root, {
                "snipaste.exe": b"new executable", "Qt6Core.dll": b"new runtime",
            }, without_hash_cmdlet=True)
            self.assertEqual(code, 0, status)
            self.assertEqual(status["state"], "installed")
            self.assertEqual((destination / "snipaste.exe").read_bytes(), b"new executable")
            self.assertEqual((destination / "config.ini").read_bytes(), b"user settings")

    def test_rejects_changed_archive(self):
        with tempfile.TemporaryDirectory(prefix="snipaste-update-test-") as root:
            destination, code, status = self.run_update(root, {
                "snipaste.exe": b"new executable", "Qt6Core.dll": b"new runtime",
            }, expected_hash="0" * 64)
            self.assertEqual(code, 1)
            self.assertEqual(status["state"], "failed")
            self.assertIn("checksum has changed", status["message"])
            self.assertEqual((destination / "snipaste.exe").read_bytes(), b"original executable")

    def test_rejects_zip_path_traversal(self):
        with tempfile.TemporaryDirectory(prefix="snipaste-update-test-") as root:
            destination, code, status = self.run_update(root, {
                "snipaste.exe": b"new executable", "Qt6Core.dll": b"new runtime",
                "../escape.txt": b"outside",
            })
            self.assertEqual(code, 1)
            self.assertEqual(status["state"], "failed")
            self.assertIn("unsafe path", status["message"])
            self.assertFalse((Path(root) / "escape.txt").exists())
            self.assertEqual((destination / "snipaste.exe").read_bytes(), b"original executable")

    def test_restores_files_after_partial_install_failure(self):
        with tempfile.TemporaryDirectory(prefix="snipaste-update-test-") as root:
            destination = Path(root) / "application"
            destination.mkdir()
            (destination / "blocked").write_bytes(b"user file blocks a directory")
            destination, code, status = self.run_update(root, {
                "snipaste.exe": b"new executable", "Qt6Core.dll": b"new runtime",
                "new-runtime.dll": b"new file", "blocked/child.dll": b"cannot install",
            })
            self.assertEqual(code, 1)
            self.assertEqual(status["state"], "failed")
            self.assertEqual((destination / "snipaste.exe").read_bytes(), b"original executable")
            self.assertEqual((destination / "Qt6Core.dll").read_bytes(), b"original runtime")
            self.assertEqual((destination / "blocked").read_bytes(), b"user file blocks a directory")
            self.assertFalse((destination / "new-runtime.dll").exists())


if __name__ == "__main__":
    unittest.main()
