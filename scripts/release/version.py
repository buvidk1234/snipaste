"""Validate a release tag against CMake and emit its semantic version."""

import re
import sys
from pathlib import Path

tag = sys.argv[1]
match = re.fullmatch(r"v((0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?)", tag)
if not match:
    raise SystemExit("Use a semantic version tag such as v0.1.1 or v0.1.1-beta.1")
version = match[1]
base = version.split("-", 1)[0]
cmake = (Path(__file__).resolve().parents[2] / "CMakeLists.txt").read_text(encoding="utf-8")
project = re.search(r"project\(snipaste\s+VERSION\s+(\d+\.\d+\.\d+)", cmake)
if not project or project[1] != base:
    raise SystemExit("The release tag must match the VERSION in CMakeLists.txt")
if "-" in version:
    for part in version.split("-", 1)[1].split("."):
        if part.isdigit() and len(part) > 1 and part.startswith("0"):
            raise SystemExit("Numeric prerelease identifiers cannot have leading zeroes")
print(version)
