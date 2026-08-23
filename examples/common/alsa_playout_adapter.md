# Linux/RK3308 ALSA playout adapter

`alsa_playout_adapter.{h,cpp}` is a Linux-only example adapter for RK3308-style
speaker builds. It is intentionally kept under `examples/common/` so the core
`sendspin-cpp` library does not gain a hard `libasound` dependency.

The adapter expects your application to open and configure an interleaved ALSA
PCM playback handle. It then:

- enables ALSA monotonic timestamps with `snd_pcm_sw_params_set_tstamp_mode()` and
  `snd_pcm_sw_params_set_tstamp_type(..., SND_PCM_TSTAMP_TYPE_MONOTONIC)` when
  available;
- records each partial `snd_pcm_writei()` success with `QueuedPlayoutTracker::submit()`;
- samples `snd_pcm_status_get_htstamp()` and `snd_pcm_status_get_delay()`;
- converts the status snapshot into `PlayoutObservation`;
- reports XRUN/suspend as an underrun observation and resets queue counters;
  stream generations remain owned by `PlayerRole` lifecycle events.

Minimal wiring:

```cpp
sendspin::AlsaPlayoutAdapter playout({
    .pcm = pcm,
    .channels = channels,
    .bytes_per_sample = bytes_per_sample,
    .write_timeout_ms = 100,
    .timestamp_error_bound_us = 1000,
    .on_observation = [&player](const sendspin::PlayoutObservation& observation) {
        player.notify_playout_observed(observation);
    },
});

if (playout.configure_monotonic_timestamps() < 0) {
    // Keep playing, but report the platform limitation in logs/telemetry.
}

playout.reset(player.playout_generation());
playout.write_interleaved(pcm_frames, frame_count);
```

Compile recipe on Linux:

```bash
cmake -S /path/to/sendspin-cpp -B build-rk3308 \
  -DBUILD_EXAMPLES=OFF \
  -DSENDSPIN_BUILD_ALSA_ADAPTER=ON
cmake --build build-rk3308 --target sendspin_alsa_playout_adapter
```

The option defaults to `OFF`, so `libasound` remains a dependency of the Linux/RK3308 adapter
target only and never becomes a core `sendspin` dependency.
