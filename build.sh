#!/usr/bin/env bash
# Build the DzPythonBridge DSS plugin DLL.
#
# Unlike daz-script-server, this repo's CMakeLists gates the DSS plugin
# behind BUILD_DSS_PLUGIN (OFF by default, so DazPythonBridgeCore can be
# built/tested standalone without the DAZ Studio SDK) -- this script always
# turns it on, since building the plugin is the whole point of running it.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    cat <<EOF
Usage: ./build.sh [command] [options]

Commands:
  build              Configure (if needed) and build (default)
  install            Build and copy plugin to DAZ Studio plugins folder
  clean              Delete the build directory and exit

Options:
  --sdk-version <v>  Target DAZ Studio SDK major version: 4 or 6; default: 4
  --clean            Wipe the build directory before building
  --reconfigure      Force CMake configure even if a cache already exists
  --debug            Build Debug config instead of Release
  --verbose          Pass --verbose to the CMake build step
  --help             Show this help message and exit

Examples:
  ./build.sh
  ./build.sh build --sdk-version 6 --clean
  ./build.sh install
  ./build.sh install --sdk-version 6
  ./build.sh clean

Required environment variables (loaded from .env if present):
  DAZ_SDK_DIR          Path to the DAZStudio4.5+ SDK (--sdk-version 4, default)
  DAZ_SDK_DIR_V6       Path to the Daz Studio 6.25+ SDK (--sdk-version 6)
  QT6_DIR              Path to the Qt6 cmake dir, e.g. <qt-install>/6.10.3/msvc2022_64/lib/cmake/Qt6
                       -- required for every build: DazPythonBridgeCore is always
                       built against plain Qt6 regardless of --sdk-version
                       (see README/CLAUDE.md for the aqtinstall command)

Optional environment variables:
  DAZ_STUDIO_EXE_DIR      Path to DAZ Studio 4 executable folder (required for install, --sdk-version 4)
  DAZ_STUDIO_EXE_DIR_V6   Path to DAZ Studio 6 executable folder (required for install, --sdk-version 6)
EOF
}

# ── Parse command ─────────────────────────────────────────────────────────────
COMMAND="build"

if [[ $# -gt 0 && "$1" != --* && "$1" != "-h" ]]; then
    COMMAND="$1"
    shift
fi

# ── Parse options ─────────────────────────────────────────────────────────────
OPT_CLEAN=0
OPT_RECONFIGURE=0
OPT_DEBUG=0
OPT_VERBOSE=0
OPT_SDK_VERSION="4"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)       OPT_CLEAN=1;                                      shift ;;
        --reconfigure) OPT_RECONFIGURE=1;                                shift ;;
        --debug)       OPT_DEBUG=1;                                      shift ;;
        --verbose)     OPT_VERBOSE=1;                                    shift ;;
        --sdk-version) OPT_SDK_VERSION="${2:?'--sdk-version requires 4 or 6'}"; shift 2 ;;
        --help|-h)     usage; exit 0 ;;
        *)
            echo "Error: unknown option '$1'" >&2
            echo "Run './build.sh --help' for usage." >&2
            exit 1
            ;;
    esac
done

# Validate command
case "$COMMAND" in
    build|install|clean) ;;
    *)
        echo "Error: unknown command '$COMMAND'" >&2
        echo "Run './build.sh --help' for usage." >&2
        exit 1
        ;;
esac

# Validate SDK version, pick a separate build dir per version so switching
# never reuses a stale CMakeCache.txt configured for the other SDK.
case "$OPT_SDK_VERSION" in
    4) BUILD_DIR="$SCRIPT_DIR/build" ;;
    6) BUILD_DIR="$SCRIPT_DIR/build-sdk6" ;;
    *)
        echo "Error: --sdk-version must be '4' or '6', got '$OPT_SDK_VERSION'" >&2
        exit 1
        ;;
esac

# ── Environment ───────────────────────────────────────────────────────────────
if [ -f "$SCRIPT_DIR/.env" ]; then
    . "$SCRIPT_DIR/.env"
fi

echo "SDK version: $OPT_SDK_VERSION"
if [ "$OPT_SDK_VERSION" = "6" ]; then
    echo "DAZ_SDK_DIR_V6: ${DAZ_SDK_DIR_V6:-<not set>}"
    echo "QT6_DIR: ${QT6_DIR:-<not set>}"
    DAZ_STUDIO_EXE_DIR="$DAZ_STUDIO_EXE_DIR_V6"
else
    echo "DAZ_SDK_DIR: ${DAZ_SDK_DIR:-<not set>}"
fi
echo "DAZ_STUDIO_EXE_DIR: ${DAZ_STUDIO_EXE_DIR:-<not set>}"

# Locate cmake — check PATH first, then known Windows location
if command -v cmake &>/dev/null; then
    CMAKE=cmake
elif [ -f "/x/apps/CMake/bin/cmake.exe" ]; then
    CMAKE="/x/apps/CMake/bin/cmake"
else
    echo "Error: cmake not found. Add it to PATH or install it." >&2
    exit 1
fi

# Artifact path (used by the build output message)
# SDK6 requires the "dsp_" filename prefix for DAZ Studio to recognize the
# plugin DLL at all (see src/CMakeLists.txt) — SDK4 keeps the plain name.
ARTIFACT_NAME="DazPythonBridge"
[ "$OPT_SDK_VERSION" = "6" ] && ARTIFACT_NAME="dsp_DazPythonBridge"

BUILD_CONFIG="Release"
[ "$OPT_DEBUG" = 1 ] && BUILD_CONFIG="Debug"
ARTIFACT="$BUILD_DIR/plugin/$BUILD_CONFIG/$ARTIFACT_NAME.dll"

# ── clean command ─────────────────────────────────────────────────────────────
if [ "$COMMAND" = "clean" ]; then
    echo "Removing build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
    echo "Done."
    exit 0
fi

# ── Wipe before build (--clean option) ───────────────────────────────────────
if [ "$OPT_CLEAN" = 1 ]; then
    echo "Removing build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

# ── CMake configure ───────────────────────────────────────────────────────────
# DazPythonBridgeCore is always built against plain Qt6 regardless of
# DAZ_SDK_VERSION (see the top-level CMakeLists.txt) -- unlike
# daz-script-server, QT6_DIR is required for every build here, not just
# --sdk-version 6.
if [ -z "$QT6_DIR" ]; then
    echo "Error: QT6_DIR not set. Set it in .env or the environment (see ./build.sh --help)." >&2
    exit 1
fi
CMAKE_FLAGS=("-DBUILD_DSS_PLUGIN=ON" "-DDAZ_SDK_VERSION=$OPT_SDK_VERSION" "-DQt6_DIR=$QT6_DIR")
if [ "$OPT_SDK_VERSION" = "6" ]; then
    if [ -z "$DAZ_SDK_DIR_V6" ]; then
        echo "Error: DAZ_SDK_DIR_V6 not set. Set it in .env or the environment." >&2
        exit 1
    fi
    CMAKE_FLAGS+=("-DDAZ_SDK_DIR=$DAZ_SDK_DIR_V6")
else
    [ -n "$DAZ_SDK_DIR" ] && CMAKE_FLAGS+=("-DDAZ_SDK_DIR=$DAZ_SDK_DIR")
fi
[ -n "$DAZ_STUDIO_EXE_DIR" ] && CMAKE_FLAGS+=("-DDAZ_STUDIO_EXE_DIR=$DAZ_STUDIO_EXE_DIR")
[ "$COMMAND" = "install" ]   && CMAKE_FLAGS+=("-DINSTALL_TO_DAZ=ON")

STALE_CHECK="$BUILD_DIR/ALL_BUILD.vcxproj"

NEEDS_CONFIGURE=0
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] || [ ! -f "$STALE_CHECK" ]; then
    NEEDS_CONFIGURE=1
fi
# install changes the output directory at configure time, so always reconfigure
[ "$OPT_RECONFIGURE" = 1 ]   && NEEDS_CONFIGURE=1
[ "$COMMAND" = "install" ]   && NEEDS_CONFIGURE=1

if [ "$NEEDS_CONFIGURE" = 1 ]; then
    echo "Running CMake configure..."
    "$CMAKE" -B "$BUILD_DIR" -S "$SCRIPT_DIR" "${CMAKE_FLAGS[@]}"
fi

# ── Build ─────────────────────────────────────────────────────────────────────
BUILD_ARGS=("--build" "$BUILD_DIR" "--target" "DzPythonBridge" "--config" "$BUILD_CONFIG")
[ "$OPT_VERBOSE" = 1 ] && BUILD_ARGS+=("--verbose")

if [ "$COMMAND" = "install" ]; then
    # DAZ Studio locks the plugin while running — catch this before the linker does
    if tasklist //FI "IMAGENAME eq DAZStudio.exe" 2>/dev/null | grep -qi "DAZStudio.exe"; then
        echo "Error: DAZ Studio is currently running." >&2
        echo "Close DAZ Studio before installing, then re-run: ./build.sh install" >&2
        exit 1
    fi
    echo "Building ($BUILD_CONFIG) and installing to DAZ Studio plugins folder..."
    "$CMAKE" "${BUILD_ARGS[@]}"

    # Bundle the daemon's own Python source (daemon/ + pyproject.toml) at
    # resources/BlueMoonFoundry/DazPythonBridge/, sibling to plugins/ --
    # mirroring how other BlueMoonFoundry DSS plugins (e.g. ContentBrowser)
    # ship external resources next to the DLL. UvBootstrapper's install-deps
    # step `uv pip install`s from here (resolved at runtime via
    # dzApp->getResourcesPath()) so `daemon` is a real importable package
    # regardless of the daemon process's working directory.
    RESOURCES_DIR="$DAZ_STUDIO_EXE_DIR/resources/BlueMoonFoundry/DazPythonBridge"
    echo "Bundling daemon/ source into $RESOURCES_DIR..."
    rm -rf "$RESOURCES_DIR/daemon"
    mkdir -p "$RESOURCES_DIR"
    cp -r "$SCRIPT_DIR/daemon" "$RESOURCES_DIR/daemon"
    find "$RESOURCES_DIR/daemon" -name "__pycache__" -exec rm -rf {} + 2>/dev/null
    cp "$SCRIPT_DIR/pyproject.toml" "$RESOURCES_DIR/pyproject.toml"
else
    echo "Building ($BUILD_CONFIG)..."
    "$CMAKE" "${BUILD_ARGS[@]}"
    echo "Output: $ARTIFACT"
fi
