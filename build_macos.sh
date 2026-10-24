#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

QT_PREFIX_PATH="${QT_PREFIX_PATH:-}"
CMAKE_GENERATOR="${CMAKE_GENERATOR:-Ninja}"

for arg in "$@"; do
  case "${arg}" in
    --clean) rm -rf "${REPO_ROOT}/build" ;;
    *) echo "Usage: $0 [--clean]" >&2; exit 1 ;;
  esac
done

# Auto-detect Qt prefix from macdeployqt if not explicitly set
if [[ -z "${QT_PREFIX_PATH}" ]]; then
  if command -v macdeployqt >/dev/null 2>&1; then
    QT_PREFIX_PATH="$(cd "$(dirname "$(command -v macdeployqt)")/.." && pwd)"
  fi
fi

if [[ -z "${QT_PREFIX_PATH}" ]]; then
  echo "Error: QT_PREFIX_PATH is not set and could not be inferred from macdeployqt." >&2
  echo "Set QT_PREFIX_PATH to your Qt macOS prefix (e.g. ~/Qt/6.x/macos)." >&2
  exit 1
fi

if [[ ! -d "${QT_PREFIX_PATH}" ]]; then
  echo "Error: QT_PREFIX_PATH does not exist: ${QT_PREFIX_PATH}" >&2
  exit 1
fi

# Optionally pass Qt6_DIR if available
CMAKE_QT6_DIR_ARGS=()
if [[ -n "${Qt6_DIR:-}" && -d "${Qt6_DIR}" ]]; then
  CMAKE_QT6_DIR_ARGS=( -DQt6_DIR="${Qt6_DIR}" )
elif [[ -d "${QT_PREFIX_PATH}/lib/cmake/Qt6" ]]; then
  CMAKE_QT6_DIR_ARGS=( -DQt6_DIR="${QT_PREFIX_PATH}/lib/cmake/Qt6" )
fi

cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" \
  -DCMAKE_PREFIX_PATH="${QT_PREFIX_PATH}" \
  -DCMAKE_BUILD_TYPE=Release \
  -G "${CMAKE_GENERATOR}" \
  "${CMAKE_QT6_DIR_ARGS[@]}"

cmake --build "${REPO_ROOT}/build" --config Release

echo "Build complete. Run: ./build/Seiche.app/Contents/MacOS/Seiche"
