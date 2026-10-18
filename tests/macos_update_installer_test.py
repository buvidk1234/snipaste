"""Exercise the real macOS updater using signed, isolated application bundles."""

import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/install-macos.sh"


@unittest.skipUnless(sys.platform == "darwin", "macOS updater integration tests")
class MacInstallerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.probe_directory = tempfile.TemporaryDirectory(prefix="snipaste-installer-probe-")
        cls.probe = Path(cls.probe_directory.name) / "probe"
        source = cls.probe.with_suffix(".c")
        source.write_text(r'''
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    char path[4096];
    snprintf(path, sizeof(path), "%s.restart.txt", argv[0]);
    FILE *output = fopen(path, "w");
    if (!output) return 1;
    for (int index = 1; index < argc; ++index) fprintf(output, "%s\n", argv[index]);
    const char *keys[] = {"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"};
    for (int index = 0; index < 2; ++index) {
        const char *value = getenv(keys[index]);
        fprintf(output, "%s\n", value ? value : "<unset>");
    }
    return fclose(output) != 0;
}
''', encoding="utf-8")
        subprocess.run(["/usr/bin/clang", str(source), "-o", str(cls.probe)], check=True,
                       capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.probe_directory.cleanup()

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix="snipaste-update-test-")
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name).resolve()
        self.destination = self.root / "普通 用户" / "snipaste.app"
        self.make_bundle(self.destination, b"original")
        self.config = self.root / "普通 用户" / "settings.ini"
        self.config.write_bytes(b"user settings")

    def make_bundle(self, app, version, identifier="com.snipaste.local"):
        contents = app / "Contents"
        (contents / "MacOS").mkdir(parents=True)
        (contents / "Resources").mkdir()
        shutil.copy2(self.probe, contents / "MacOS/snipaste")
        (contents / "Resources/version.txt").write_bytes(version)
        (contents / "Resources/current").symlink_to("version.txt")
        with (contents / "Info.plist").open("wb") as output:
            plistlib.dump({"CFBundleIdentifier": identifier, "CFBundleExecutable": "snipaste",
                          "CFBundleName": "Snipaste Updater Test", "CFBundlePackageType": "APPL",
                          "CFBundleVersion": "1.0", "LSUIElement": True}, output)
        subprocess.run(["/usr/bin/codesign", "--force", "--deep", "--sign", "-", str(app)],
                       check=True, capture_output=True)

    def prepare_update(self, expected_hash=None, restart=False, process_id=2147483647,
                       tamper=False, identifier="com.snipaste.local"):
        source = self.root / "package/snipaste.app"
        self.make_bundle(source, b"updated", identifier)
        if tamper:
            (source / "Contents/Resources/version.txt").write_bytes(b"tampered")
        archive = self.root / "update.zip"
        subprocess.run(["/usr/bin/ditto", "-c", "-k", "--sequesterRsrc", "--keepParent",
                        str(source), str(archive)], check=True, capture_output=True)
        self.arguments = ["--snipaste-restart", "--config", str(self.config)]
        self.manifest = self.root / "install.json"
        self.manifest.write_text(json.dumps({
            "archive": str(archive), "destination": str(self.destination),
            "sha256": expected_hash or hashlib.sha256(archive.read_bytes()).hexdigest(),
            "processId": process_id, "config": str(self.config), "restart": restart,
            "arguments": self.arguments,
        }), encoding="utf-8")

    def run_update(self, environment=None):
        result = subprocess.run(["/bin/bash", str(SCRIPT), str(self.manifest)],
                                capture_output=True, timeout=20, env=environment)
        status = json.loads((self.root / "status.json").read_text(encoding="utf-8"))
        return result, status

    def version(self):
        return (self.destination / "Contents/Resources/version.txt").read_bytes()

    def test_installs_signed_bundle_in_unicode_path_and_preserves_settings(self):
        self.prepare_update()
        result, status = self.run_update()
        self.assertEqual(result.returncode, 0, (status, result.stderr))
        self.assertEqual(status["state"], "installed")
        self.assertEqual(self.version(), b"updated")
        self.assertEqual(self.config.read_bytes(), b"user settings")
        self.assertTrue((self.destination / "Contents/Resources/current").is_symlink())
        backup = Path(f"{self.destination}.previous-2147483647")
        self.assertEqual((backup / "Contents/Resources/version.txt").read_bytes(), b"original")
        self.assertFalse((self.destination / "Contents/MacOS/snipaste.restart.txt").exists())

    def test_rejects_changed_archive(self):
        self.prepare_update(expected_hash="0" * 64)
        result, status = self.run_update()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(status["state"], "failed")
        self.assertEqual(self.version(), b"original")

    def test_rejects_invalid_signature(self):
        self.prepare_update(tamper=True)
        result, status = self.run_update()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(status["state"], "failed")
        self.assertEqual(self.version(), b"original")

    def test_rejects_another_application(self):
        self.prepare_update(identifier="com.example.another-app")
        result, status = self.run_update()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(status["state"], "failed")
        self.assertEqual(self.version(), b"original")

    def test_restores_previous_bundle_after_replacement_failure(self):
        parent = subprocess.Popen(["/bin/sleep", "30"])
        self.addCleanup(lambda: parent.poll() is not None or parent.kill())
        self.prepare_update(process_id=parent.pid)
        updater = subprocess.Popen(["/bin/bash", str(SCRIPT), str(self.manifest)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(lambda: updater.poll() is not None or updater.kill())
        deadline = time.monotonic() + 10
        status_path = self.root / "status.json"
        while not status_path.exists() and updater.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertEqual(json.loads(status_path.read_text())["state"], "ready")
        shutil.rmtree(Path(f"{self.destination}.next-{parent.pid}"))
        parent.terminate()
        parent.wait(timeout=5)
        _, errors = updater.communicate(timeout=10)
        self.assertNotEqual(updater.returncode, 0, errors)
        self.assertEqual(json.loads(status_path.read_text())["state"], "failed")
        self.assertEqual(self.version(), b"original")

    def test_restarts_with_config_arguments_and_without_sdk_plugins(self):
        self.prepare_update(restart=True)
        self.addCleanup(subprocess.run, [
            "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister",
            "-u", str(self.destination),
        ], capture_output=True, timeout=10)
        environment = dict(os.environ, QT_PLUGIN_PATH="invalid-sdk-plugin-directory",
                           QT_QPA_PLATFORM_PLUGIN_PATH="invalid-sdk-platform-directory")
        result, status = self.run_update(environment)
        self.assertEqual(result.returncode, 0, (status, result.stderr))
        marker = self.destination / "Contents/MacOS/snipaste.restart.txt"
        deadline = time.monotonic() + 10
        while not marker.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertEqual(marker.read_text().splitlines(), self.arguments + ["<unset>", "<unset>"])


if __name__ == "__main__":
    unittest.main()
