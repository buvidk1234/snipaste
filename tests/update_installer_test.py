"""Exercise the real Windows installer against isolated temporary packages."""

import ctypes
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
import unittest
import uuid
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/install-windows.ps1"
POWERSHELL = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"


class WindowsInstallerTest(unittest.TestCase):
    def binary_version(self, path):
        version = ctypes.WinDLL("version", use_last_error=True)
        version.GetFileVersionInfoSizeW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p]
        version.GetFileVersionInfoW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32,
                                              ctypes.c_uint32, ctypes.c_void_p]
        version.VerQueryValueW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p,
                                          ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint32)]
        size = version.GetFileVersionInfoSizeW(str(path), None)
        self.assertGreater(size, 0, f"Missing version information: {path}")
        buffer = ctypes.create_string_buffer(size)
        self.assertTrue(version.GetFileVersionInfoW(str(path), 0, size, buffer))
        pointer, length = ctypes.c_void_p(), ctypes.c_uint32()
        self.assertTrue(version.VerQueryValueW(buffer, "\\", ctypes.byref(pointer), ctypes.byref(length)))
        fixed = ctypes.cast(pointer, ctypes.POINTER(ctypes.c_uint32 * 6)).contents
        return fixed[2] >> 16, fixed[2] & 0xffff, fixed[3] >> 16, fixed[3] & 0xffff

    @unittest.skipUnless(os.environ.get("SNIPASTE_TEST_SETUP"), "No packaged EXE supplied")
    def test_packaged_installer_can_be_launched_by_legacy_updater(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
        kernel.LoadLibraryExW.restype = ctypes.c_void_p
        kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        kernel.FindResourceW.restype = ctypes.c_void_p
        kernel.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        kernel.SizeofResource.restype = ctypes.c_uint32
        kernel.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        kernel.LoadResource.restype = ctypes.c_void_p
        kernel.LockResource.argtypes = [ctypes.c_void_p]
        kernel.LockResource.restype = ctypes.c_void_p
        kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
        kernel.FreeLibrary.restype = ctypes.c_int
        # Read PE resources as data, without executing the installer or showing UAC.
        module = kernel.LoadLibraryExW(os.environ["SNIPASTE_TEST_SETUP"], None, 0x22)
        self.assertTrue(module, ctypes.get_last_error())
        try:
            resource = kernel.FindResourceW(module, 1, 24)  # RT_MANIFEST
            self.assertTrue(resource, ctypes.get_last_error())
            size = kernel.SizeofResource(module, resource)
            pointer = kernel.LockResource(kernel.LoadResource(module, resource))
            self.assertTrue(pointer, ctypes.get_last_error())
            manifest = ET.fromstring(ctypes.string_at(pointer, size))
            level = manifest.find(".//{urn:schemas-microsoft-com:asm.v3}requestedExecutionLevel")
            self.assertIsNotNone(level)
            self.assertEqual(level.get("level"), "asInvoker")
        finally:
            kernel.FreeLibrary(module)

    @unittest.skipUnless(os.environ.get("SNIPASTE_TEST_SETUP"), "No packaged EXE supplied")
    def test_exe_installs_and_updates_portable_without_registration(self):
        import winreg

        def registration():
            directories = []
            for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
                try:
                    with winreg.OpenKey(hive, r"Software\snipaste-open-source",
                                        access=winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
                        directories.append(winreg.QueryValueEx(key, "InstallDir")[0])
                except FileNotFoundError:
                    directories.append(None)
            return directories

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
            self.assertEqual(self.binary_version(destination / "snipaste.exe"), self.binary_version(setup))
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

    @unittest.skipUnless(os.environ.get("NSIS_ROOT"), "No NSIS compiler supplied")
    def test_legacy_user_install_upgrades_in_place_and_keeps_its_scope(self):
        import winreg

        # Compile the production installer with isolated registry and shortcut
        # names so this test never changes a real Snipaste installation.
        name = "snipaste-installer-test-" + uuid.uuid4().hex
        app_key = "Software\\" + name
        uninstall_key = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + name
        access = winreg.KEY_ALL_ACCESS | winreg.KEY_WOW64_64KEY
        version = re.search(r"VERSION\s+(\d+\.\d+\.\d+)",
                            (SCRIPT.parents[1] / "CMakeLists.txt").read_text(encoding="utf-8"))[1]
        shortcuts = Path(os.environ["APPDATA"]) / "Microsoft/Windows/Start Menu/Programs" / name
        try:
            with tempfile.TemporaryDirectory(prefix="snipaste-update-legacy-test-") as folder:
                root = Path(folder).resolve()
                self.assertTrue(root.is_relative_to(Path(tempfile.gettempdir()).resolve()))
                destination = root / "普通 用户" / "Snipaste"
                destination.mkdir(parents=True)
                (destination / "snipaste.exe").write_bytes(b"old executable")
                (destination / "config.ini").write_bytes(b"keep my settings")
                with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, app_key, access=access) as key:
                    winreg.SetValueEx(key, "InstallDir", 0, winreg.REG_SZ, str(destination))
                with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, uninstall_key, access=access) as key:
                    winreg.SetValueEx(key, "DisplayVersion", 0, winreg.REG_SZ, "0.1.3")
                archive = root / "payload.zip"
                with zipfile.ZipFile(archive, "w") as package:
                    package.writestr("snipaste.exe", b"new executable")
                    package.writestr("Qt6Core.dll", b"runtime")
                uninstall_files = root / "uninstall-files.nsh"
                uninstall_files.write_text('Delete "$INSTDIR\\snipaste.exe"\n'
                                           'Delete "$INSTDIR\\Qt6Core.dll"\n', encoding="utf-8")
                setup = root / "setup.exe"
                command = [str(Path(os.environ["NSIS_ROOT"]) / "makensis.exe"), "/V2", "/INPUTCHARSET", "UTF8",
                           f"/DAPP_KEY={app_key}", f"/DUNINSTALL_KEY={uninstall_key}",
                           f"/DSTART_MENU_FOLDER={name}", f"/DVERSION={version}",
                           f"/DPRODUCT_VERSION={version}.0", f"/DARCHIVE={archive}",
                           f"/DOUTPUT_FILE={setup}", f"/DSCRIPT_DIRECTORY={SCRIPT.parent}",
                           f"/DUNINSTALL_FILES={uninstall_files}", "/DINSTALLED_SIZE=1",
                           str(SCRIPT.parent / "installer.nsi")]
                built = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
                self.assertEqual(built.returncode, 0, built.stdout.decode(errors="replace"))
                # CreateProcess (not ShellExecute/runas), as used by v0.1.3.
                # No /D: discover its HKCU install even on a fresh installer.
                result = subprocess.run([str(setup), "/S"], capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout.decode(errors="replace"))
                self.assertEqual((destination / "snipaste.exe").read_bytes(), b"new executable")
                self.assertEqual((destination / "config.ini").read_bytes(), b"keep my settings")
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, uninstall_key, access=access) as key:
                    self.assertEqual(winreg.QueryValueEx(key, "DisplayVersion")[0], version)
                self.assertTrue((shortcuts / "snipaste.lnk").is_file())
                with self.assertRaises(FileNotFoundError):
                    winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, app_key,
                                   access=winreg.KEY_READ | winreg.KEY_WOW64_64KEY)
                # Also exercise the real updater's EXE path, including /D and
                # the wait/exit-status handoff, without any machine registry.
                (destination / "snipaste.exe").write_bytes(b"old executable")
                manifest = root / "install.json"
                manifest.write_text(json.dumps({
                    "archive": str(setup), "destination": str(destination),
                    "sha256": hashlib.sha256(setup.read_bytes()).hexdigest(),
                    "processId": 0, "restart": False, "portable": False,
                }), encoding="utf-8")
                result = subprocess.run([str(POWERSHELL), "-NoProfile", "-NonInteractive",
                                         "-ExecutionPolicy", "Bypass", "-File", str(SCRIPT),
                                         "-Manifest", str(manifest)], capture_output=True, timeout=30)
                status = json.loads((root / "status.json").read_text(encoding="utf-8-sig"))
                self.assertEqual(result.returncode, 0, status)
                self.assertEqual(status["state"], "installed")
                self.assertEqual((destination / "snipaste.exe").read_bytes(), b"new executable")
                # The generated uninstaller must use the same user scope.
                result = subprocess.run(f'"{destination / "Uninstall.exe"}" /S _?={destination}',
                                        capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0)
                self.assertFalse((destination / "snipaste.exe").exists())
                self.assertEqual((destination / "config.ini").read_bytes(), b"keep my settings")
                with self.assertRaises(FileNotFoundError):
                    winreg.OpenKey(winreg.HKEY_CURRENT_USER, app_key, access=access)
        finally:
            for key in (app_key, uninstall_key):
                try:
                    winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, key, winreg.KEY_WOW64_64KEY)
                except FileNotFoundError:
                    pass
            if shortcuts.exists():
                self.assertEqual(shortcuts.resolve().parent,
                                 (Path(os.environ["APPDATA"]) / "Microsoft/Windows/Start Menu/Programs").resolve())
                shutil.rmtree(shortcuts)

    def test_setup_installs_and_upgrades_in_a_unicode_path(self):
        with tempfile.TemporaryDirectory(prefix="snipaste-setup-test-") as folder:
            root = Path(folder)
            destination = root / "普通 用户" / "Snipaste"
            archive = root / "package.zip"
            setup = root / SCRIPT.name
            shutil.copy2(SCRIPT, setup)
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
