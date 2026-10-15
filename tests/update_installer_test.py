"""Exercise the real Windows installer against isolated temporary packages."""

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time
import unittest
import zipfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/updates/install-windows.ps1"
POWERSHELL = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
SETUP = SCRIPT.parents[1] / "windows/setup-install.ps1"


class WindowsInstallerTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("SNIPASTE_TEST_SETUP"), "No packaged EXE supplied")
    def test_exe_installs_and_updates_portable_without_registration(self):
        import winreg

        def registration():
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\snipaste-open-source",
                                    access=winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
                    return winreg.QueryValueEx(key, "InstallDir")[0]
            except FileNotFoundError:
                return None

        before = registration()
        with tempfile.TemporaryDirectory(prefix="snipaste-update-exe-test-") as folder:
            root = Path(folder)
            destination = root / "普通 用户" / "Snipaste"
            setup = root / "update.exe"
            shutil.copy2(os.environ["SNIPASTE_TEST_SETUP"], setup)
            # NSIS /D must be last and unquoted, including paths with spaces.
            result = subprocess.run(f'"{setup}" /S /PORTABLE /D={destination}',
                                    capture_output=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout.decode(errors="replace"))
            self.assertTrue((destination / "Qt6Core.dll").is_file())
            installed = (destination / "snipaste.exe").read_bytes()
            (destination / "snipaste.exe").write_bytes(b"old executable")
            (destination / "config.ini").write_bytes(b"keep my settings")
            manifest = root / "install.json"
            manifest.write_text(json.dumps({
                "archive": str(setup), "destination": str(destination),
                "sha256": hashlib.sha256(setup.read_bytes()).hexdigest(),
                "processId": 0, "restart": False, "portable": True,
            }), encoding="utf-8")
            result = subprocess.run([
                str(POWERSHELL), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                "-File", str(SCRIPT), "-Manifest", str(manifest),
            ], capture_output=True, timeout=60)
            status = json.loads((root / "status.json").read_text(encoding="utf-8-sig"))
            self.assertEqual(result.returncode, 0, status)
            self.assertEqual(status["state"], "installed")
            self.assertEqual((destination / "snipaste.exe").read_bytes(), installed)
            self.assertEqual((destination / "config.ini").read_bytes(), b"keep my settings")
            self.assertFalse((destination / "Uninstall.exe").exists())
            self.assertEqual(registration(), before)

    def test_setup_installs_and_upgrades_in_a_unicode_path(self):
        with tempfile.TemporaryDirectory(prefix="snipaste-setup-test-") as folder:
            root = Path(folder)
            destination = root / "普通 用户" / "Snipaste"
            archive = root / "package.zip"
            # Match the two scripts embedded side by side in the EXE.
            setup = root / SETUP.name
            shutil.copy2(SETUP, setup)
            shutil.copy2(SCRIPT, root / SCRIPT.name)
            for version in (b"first", b"second"):
                with zipfile.ZipFile(archive, "w") as package:
                    package.writestr("snipaste.exe", version)
                    package.writestr("Qt6Core.dll", b"runtime")
                result = subprocess.run([
                    str(POWERSHELL), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                    "-File", str(setup), "-Archive", str(archive), "-Destination", str(destination),
                ], capture_output=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout.decode(errors="replace"))
                self.assertEqual((destination / "snipaste.exe").read_bytes(), version)
                if version == b"first":
                    (destination / "config.ini").write_bytes(b"keep my settings")
            self.assertEqual((destination / "config.ini").read_bytes(), b"keep my settings")

    def test_restarts_with_config_arguments_and_without_sdk_plugins(self):
        with tempfile.TemporaryDirectory(prefix="snipaste-update-test-") as folder:
            root = Path(folder)
            probe = root / "restart-probe.exe"
            compiler = root / "compile.ps1"
            compiler.write_text("""param([string]$Output)
Add-Type -OutputAssembly $Output -OutputType WindowsApplication -TypeDefinition @'
using System;
using System.IO;
class RestartProbe {
    static void Main(string[] args) {
        string root = AppDomain.CurrentDomain.BaseDirectory;
        File.WriteAllLines(Path.Combine(root, "restart.txt"), args);
        File.WriteAllText(Path.Combine(root, "plugin-path.txt"),
            Environment.GetEnvironmentVariable("QT_PLUGIN_PATH") ?? "<unset>");
    }
}
'@
""", encoding="utf-8")
            compiled = subprocess.run([str(POWERSHELL), "-NoProfile", "-NonInteractive",
                                       "-ExecutionPolicy", "Bypass", "-File", str(compiler),
                                       "-Output", str(probe)], capture_output=True, timeout=20)
            self.assertEqual(compiled.returncode, 0, compiled.stderr.decode(errors="replace"))
            arguments = ["--snipaste-restart", "--config", str(root / "普通 用户" / "settings.ini")]
            environment = dict(os.environ, QT_PLUGIN_PATH="invalid-sdk-plugin-directory")
            destination, code, status = self.run_update(root, {
                "snipaste.exe": probe.read_bytes(), "Qt6Core.dll": b"runtime",
            }, restart_args=arguments, environment=environment)
            self.assertEqual(code, 0, status)
            marker = destination / "restart.txt"
            deadline = time.monotonic() + 5
            while not marker.exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertEqual(marker.read_text(encoding="utf-8-sig").splitlines(), arguments)
            self.assertEqual((destination / "plugin-path.txt").read_text(), "<unset>")

    def run_update(self, root, entries, expected_hash=None, without_hash_cmdlet=False,
                   restart_args=None, environment=None):
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
            "processId": 2147483647, "restart": restart_args is not None,
            "arguments": restart_args or [],
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
        result = subprocess.run(command, capture_output=True, timeout=20, env=environment)
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
