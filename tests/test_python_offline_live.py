#!/usr/bin/env python3
"""Smoke-test configured CPython rendering without a competing audio thread."""

from __future__ import annotations

import time

import amy
import c_amy


def main() -> int:
    c_amy.live(
        audio=False,
        default_synths=0,
        max_patterns=1024,
        max_pattern_tags=64,
        max_pattern_instances=32,
    )

    before = amy.ticks_ms()
    time.sleep(0.05)
    after_sleep = amy.ticks_ms()
    if after_sleep != before:
        raise AssertionError(
            "audio=False advanced AMY without an explicit render: "
            f"{before} -> {after_sleep}"
        )

    amy.send(osc=0, wave=amy.SINE, freq=440, vel=1)
    peak = 0
    for _ in range(8):
        block = c_amy.render_to_list()
        peak = max(peak, max((abs(int(sample)) for sample in block), default=0))
    if peak <= 0:
        raise AssertionError("offline render produced no audio")
    if amy.ticks_ms() <= after_sleep:
        raise AssertionError("explicit offline renders did not advance AMY time")

    # A high pattern id proves that audio=False retained live()'s configurable
    # engine sizing instead of falling back to the import-time defaults.
    amy.pattern_begin(1000, 4)
    amy.pattern_event_wire(1000, 0, "v0l0Z", period=4, tag=0)
    amy.pattern_commit(1000)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
