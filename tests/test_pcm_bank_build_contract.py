#!/usr/bin/env python3
"""Static guard for LB's release-only CPython PCM-bank selector."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SETUP = (ROOT / "setup.py").read_text(encoding="utf-8")


def main() -> None:
    required = (
        "AMY_PCM_BANK",
        "use_gamma9001",
        "comp_args.append(\"-DGAMMA9001\")",
        "class AmyBuildExt(build_ext):",
        "self.force = True",
        "cmdclass={'build_ext': AmyBuildExt}",
    )
    missing = [value for value in required if value not in SETUP]
    if missing:
        raise AssertionError(f"missing PCM-bank build contract: {missing}")
    print("PCM-bank build contract OK: tiny is selectable, Gamma9001 stays default")


if __name__ == "__main__":
    main()
