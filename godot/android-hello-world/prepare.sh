#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
GRADLE_BIN="${GRADLE_BIN:-gradle}"

(
  cd "${ROOT}/android"
  "${GRADLE_BIN}" :amy-service:assembleDebug :amy-service:assembleRelease
)

ADDON="${HERE}/addons/amy_android"
mkdir -p "${ADDON}"
cp "${ROOT}/android/amy-service/build/outputs/aar/amy-service-debug.aar" \
   "${ADDON}/amy-service-debug.aar"
cp "${ROOT}/android/amy-service/build/outputs/aar/amy-service-release.aar" \
   "${ADDON}/amy-service-release.aar"

printf 'Prepared AMY service AAR for Godot in %s\n' "${ADDON}"
