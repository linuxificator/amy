#!/usr/bin/env python3
"""Source-level guard for the Android AAR's public integration contract."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def require(pattern: str, text: str, label: str) -> None:
    if re.search(pattern, text, flags=re.MULTILINE) is None:
        raise AssertionError(f"Android service contract is missing {label}")


def main() -> None:
    engine = (ROOT / "android/amy-service/src/main/cpp/amy_android.cpp").read_text()
    capture = (
        ROOT / "android/amy-service/src/main/cpp/amy_android_capture.cpp"
    ).read_text()
    gradle = (ROOT / "android/amy-service/build.gradle.kts").read_text()
    cmake = (
        ROOT / "android/amy-service/src/main/cpp/CMakeLists.txt"
    ).read_text()
    manifest = (ROOT / "android/amy-service/src/main/AndroidManifest.xml").read_text()
    hello = (ROOT / "android/hello-world/src/main/java/org/amy/hello/MainActivity.java").read_text()

    require(r"kIntegrationMaxOscillators\s*=\s*336\s*;", engine,
            "the 336-oscillator host capacity")
    require(r"kIntegrationMaxBuses\s*=\s*11\s*;", engine,
            "the 11-bus host capacity")
    require(r"config\.max_oscs\s*=\s*kIntegrationMaxOscillators\s*;", engine,
            "runtime oscillator configuration")
    require(r"config\.max_buses\s*=\s*kIntegrationMaxBuses\s*;", engine,
            "runtime bus configuration")
    require(r"config\.max_patterns\s*=\s*1024\s*;", engine,
            "LB fill-library pattern capacity")
    require(r"config\.max_pattern_tags\s*=\s*64\s*;", engine,
            "per-pattern event capacity")
    require(r"config\.max_pattern_instances\s*=\s*32\s*;", engine,
            "active pattern-instance capacity")
    require(r"kCaptureSeconds\s*=\s*8\s*;", capture,
            "the framework-safe eight-second audio capture window")
    require(r'ndkVersion\s*=\s*"27\.2\.12479018"', gradle,
            "the PySide-compatible Android NDK r27c")
    require(r"gamma9001-blob-c", cmake,
            "per-ABI Gamma9001 blob generation")
    require(r"GAMMA9001=1", cmake,
            "the Gamma9001 AMY compile profile")
    require(r"\$\{GAMMA9001_PCM_C\}", cmake,
            "the linked Gamma9001 PCM source")
    require(r"amy_set_gamma9001_pcm\(gamma9001_pcm_data\)", engine,
            "Gamma9001 PCM registration before AMY starts")
    require(r"android:process=\":amy\"", manifest, "the separate :amy process")
    require(r"android:exported=\"false\"", manifest, "a private Android component")
    require(r"\$\{applicationId\}\.amy-autostart", manifest,
            "an application-scoped provider authority")

    forbidden_client_symbols = ("AmyService", "System.loadLibrary", "native ")
    for symbol in forbidden_client_symbols:
        if symbol in hello:
            raise AssertionError(
                f"transport-only hello-world unexpectedly contains {symbol!r}"
            )

    print("Android service contract OK: private :amy process, socket-only client, "
          "Gamma9001 PCM, 336 oscillators, 11 buses, 1024 patterns, "
          "8-second test capture")


if __name__ == "__main__":
    main()
