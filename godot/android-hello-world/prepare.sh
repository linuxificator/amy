#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
GRADLE_BIN="${GRADLE_BIN:-gradle}"

# Android intentionally builds/packages only the independent AMY service AAR.
# Do not build or copy the Godot AmySynth GDExtension for Android.
(
  cd "${ROOT}/android"
  "${GRADLE_BIN}" :amy-service:assembleDebug
)

ADDON="${HERE}/addons/amy_android"
mkdir -p "${ADDON}"
cp "${ROOT}/android/amy-service/build/outputs/aar/amy-service-debug.aar" \
   "${ADDON}/amy-service-debug.aar"

# Exercise the exact shared high-level Godot API in this Android proof rather
# than maintaining a second Android-specific copy of the dictionary/wire code.
cp "${ROOT}/godot/amy.gd" "${HERE}/amy.gd"

printf 'Prepared AMY service AAR and shared Amy.gd for Godot in %s\n' "${HERE}"
