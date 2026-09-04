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
        max_sequencer_tags=1280,
        max_sequence_events=64,
        max_sequence_executions=40,
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

    # A high sequence tag proves that audio=False retained live()'s configurable
    # engine sizing instead of falling back to the import-time defaults.
    amy.define_sequence(1000, [dict(ticks=(0,), osc=0, vel=0)])
    amy.send(sequence_control=(1000, amy.SEQUENCE_CONTROL_START))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
