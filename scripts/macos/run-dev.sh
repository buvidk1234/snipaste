#!/usr/bin/env bash
# ==============================================================================
# macOS Developer Workflow Script for Snipaste
# ==============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIG="Debug"
BUILD_ONLY=0
RUN_TESTS=0
NO_BUILD=0
RECONFIGURE=0
KILL_EXISTING=0
PREFERENCES=0
SNIP=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--config)
            CONFIG="$2"
            shift 2
            ;;
        --debug)
            CONFIG="Debug"
            shift
            ;;
        --release)
            CONFIG="Release"
            shift
            ;;
        -b|--build-only)
            BUILD_ONLY=1
            shift
            ;;
        -t|--test)
            RUN_TESTS=1
            shift
            ;;
        --no-build)
            NO_BUILD=1
            shift
            ;;
        -r|--reconfigure)
            RECONFIGURE=1
            shift
            ;;
        -k|--kill-existing)
            KILL_EXISTING=1
            shift
            ;;
        -p|--preferences)
            PREFERENCES=1
            shift
            ;;
        -s|--snip)
            SNIP=1
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  -c, --config <Debug|Release>  Build configuration (default: Debug)"
            echo "      --debug                   Shortcut for Debug"
            echo "      --release                 Shortcut for Release"
            echo "  -b, --build-only              Compile without launching application"
            echo "  -t, --test                    Run CTest suite"
            echo "      --no-build                Skip build step and launch directly"
            echo "  -r, --reconfigure             Force CMake reconfiguration"
            echo "  -k, --kill-existing           Automatically kill running snipaste instance"
            echo "  -p, --preferences             Launch and open Preferences dialog"
            echo "  -s, --snip                    Launch directly into Snip mode"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [[ $PREFERENCES -eq 1 && $SNIP -eq 1 ]]; then
    echo "Error: Cannot use both --preferences and --snip." >&2
    exit 1
fi

CONFIG_LOWER="$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')"
BUILD_DIR="$REPO_ROOT/build/macos-$CONFIG_LOWER"

# Locate Qt 6 installation on macOS
if [[ -z "${QT_ROOT:-}" ]]; then
    CANDIDATES=(
        "/opt/homebrew/opt/qt6"
        "/opt/homebrew/opt/qt"
        "/opt/homebrew/opt/qtbase"
        "/usr/local/opt/qt6"
        "/usr/local/opt/qt"
        "/usr/local/opt/qtbase"
    )
    for candidate in "${CANDIDATES[@]}"; do
        if [[ -f "$candidate/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
            QT_ROOT="$candidate"
            break
        fi
    done

    # Check official installer directories (~/Qt/6.*/macos)
    if [[ -z "${QT_ROOT:-}" ]]; then
        OFFICIAL_QT="$(ls -d "$HOME"/Qt/6.*/macos 2>/dev/null | sort -V | tail -n 1 || true)"
        if [[ -n "$OFFICIAL_QT" && -f "$OFFICIAL_QT/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
            QT_ROOT="$OFFICIAL_QT"
        fi
    fi
fi

if [[ -z "${QT_ROOT:-}" || ! -f "$QT_ROOT/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    echo "Error: Qt 6 was not found. Please set QT_ROOT (e.g. export QT_ROOT=/opt/homebrew/opt/qt6)." >&2
    exit 1
fi

export PATH="$QT_ROOT/bin:$PATH"

# 自动关闭正在运行的旧版本，确保始终只运行最新的一个版本
if pgrep -x snipaste >/dev/null 2>&1; then
    echo "Stopping existing snipaste process..."
    pkill -x snipaste || true
    sleep 0.3
fi

# Configure & Build
CACHE_FILE="$BUILD_DIR/CMakeCache.txt"
if [[ $NO_BUILD -eq 0 && ( $RECONFIGURE -eq 1 || ! -f "$CACHE_FILE" ) ]]; then
    mkdir -p "$BUILD_DIR"
    echo "> cmake -S \"$REPO_ROOT\" -B \"$BUILD_DIR\" -G Ninja -DCMAKE_BUILD_TYPE=$CONFIG -DCMAKE_PREFIX_PATH=\"$QT_ROOT\" -DSNIPASTE_BUILD_TESTS=ON"
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE="$CONFIG" \
        -DCMAKE_PREFIX_PATH="$QT_ROOT" \
        -DSNIPASTE_BUILD_TESTS=ON
fi

if [[ $NO_BUILD -eq 0 ]]; then
    CPU_CORES="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    if [[ $RUN_TESTS -eq 1 ]]; then
        echo "> cmake --build \"$BUILD_DIR\" --parallel $CPU_CORES"
        cmake --build "$BUILD_DIR" --parallel "$CPU_CORES"
    else
        echo "> cmake --build \"$BUILD_DIR\" --target snipaste --parallel $CPU_CORES"
        cmake --build "$BUILD_DIR" --target snipaste --parallel "$CPU_CORES"
    fi
fi

if [[ $RUN_TESTS -eq 1 ]]; then
    echo "> ctest --test-dir \"$BUILD_DIR\" --output-on-failure"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
    exit 0
fi

if [[ $BUILD_ONLY -eq 1 ]]; then
    echo "Build complete."
    exit 0
fi

# Locate executable / app bundle
APP_BUNDLE="$BUILD_DIR/snipaste.app"
if [[ -d "$APP_BUNDLE" ]]; then
    EXECUTABLE="$APP_BUNDLE/Contents/MacOS/snipaste"
else
    EXECUTABLE="$BUILD_DIR/snipaste"
fi

if [[ ! -f "$EXECUTABLE" ]]; then
    echo "Error: Executable not found at $EXECUTABLE" >&2
    exit 1
fi

ARGS=()
if [[ $PREFERENCES -eq 1 ]]; then ARGS+=("open-preferences"); fi
if [[ $SNIP -eq 1 ]]; then ARGS+=("--snip"); fi

echo "Starting snipaste ($EXECUTABLE)..."
if [[ ${#ARGS[@]} -eq 0 ]]; then
    "$EXECUTABLE" &
else
    "$EXECUTABLE" "${ARGS[@]}" &
fi
echo "Started development build (PID: $!)."
