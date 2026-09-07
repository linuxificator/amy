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
        max_reverb_rooms=2,
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
    amy.send(reverb_room=[1, 0.35, 0.8, 0.5, 3000])
    amy.send(bus=0, reverb_send=[1, 0.5])
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

    # CPython validates this runtime allocation dimension before stopping an
    # already-running engine, just like the other live() sizing arguments.
    try:
        c_amy.live(audio=False, max_reverb_rooms=-1)
    except ValueError as exc:
        if "max_reverb_rooms" not in str(exc):
            raise AssertionError(f"unclear shared-reverb validation: {exc}") from exc
    else:
        raise AssertionError("negative max_reverb_rooms was accepted")

    # The rejected call above must not have stopped or replaced this engine.
    if not c_amy.render_to_list():
        raise AssertionError("rejected live() call stopped the current engine")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
