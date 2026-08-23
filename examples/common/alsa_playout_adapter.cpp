// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0

#include "alsa_playout_adapter.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#ifndef ESTRPIPE
#define ESTRPIPE EPIPE
#endif

namespace sendspin {
namespace {

int64_t htimestamp_to_monotonic_us(const snd_htimestamp_t& timestamp) {
    const int64_t seconds = static_cast<int64_t>(timestamp.tv_sec);
    const int64_t nanoseconds = static_cast<int64_t>(timestamp.tv_nsec);
    return seconds * INT64_C(1'000'000) + nanoseconds / INT64_C(1'000);
}

bool valid_htimestamp(const snd_htimestamp_t& timestamp) {
    return timestamp.tv_sec > 0 || timestamp.tv_nsec > 0;
}

}  // namespace

AlsaPlayoutAdapter::AlsaPlayoutAdapter(Config config)
    : pcm_(config.pcm),
      channels_(config.channels),
      bytes_per_sample_(config.bytes_per_sample),
      write_timeout_ms_(config.write_timeout_ms < 0 ? 0 : config.write_timeout_ms),
      timestamp_error_bound_us_(config.timestamp_error_bound_us),
      on_observation_(std::move(config.on_observation)) {
    tracker_.reset(generation_);
}

int AlsaPlayoutAdapter::configure_monotonic_timestamps() {
    if (pcm_ == nullptr) {
        return -EINVAL;
    }

    snd_pcm_sw_params_t* params = nullptr;
    snd_pcm_sw_params_alloca(&params);
    int rc = snd_pcm_sw_params_current(pcm_, params);
    if (rc < 0) {
        return rc;
    }
    rc = snd_pcm_sw_params_set_tstamp_mode(pcm_, params, SND_PCM_TSTAMP_ENABLE);
    if (rc < 0) {
        return rc;
    }
#ifdef SND_PCM_TSTAMP_TYPE_MONOTONIC
    rc = snd_pcm_sw_params_set_tstamp_type(pcm_, params, SND_PCM_TSTAMP_TYPE_MONOTONIC);
    if (rc < 0) {
        return rc;
    }
#endif
    return snd_pcm_sw_params(pcm_, params);
}

void AlsaPlayoutAdapter::reset(uint32_t generation) {
    generation_ = generation;
    tracker_.reset(generation_);
}

snd_pcm_sframes_t AlsaPlayoutAdapter::write_interleaved(const void* frames,
                                                       snd_pcm_uframes_t frame_count) {
    if (pcm_ == nullptr || frames == nullptr || frame_count == 0 || frame_bytes() == 0) {
        return -EINVAL;
    }

    const auto* bytes = static_cast<const uint8_t*>(frames);
    snd_pcm_uframes_t total_written = 0;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(write_timeout_ms_);
    while (total_written < frame_count) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return total_written > 0 ? static_cast<snd_pcm_sframes_t>(total_written) : -EAGAIN;
        }
        const auto remaining_timeout =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int ready = snd_pcm_wait(pcm_, static_cast<int>(remaining_timeout + 1));
        if (ready <= 0) {
            if (ready < 0 && ready != -EAGAIN) {
                const int recovered = recover_stream_error(ready);
                if (recovered < 0) {
                    return total_written > 0 ? static_cast<snd_pcm_sframes_t>(total_written)
                                             : recovered;
                }
                continue;
            }
            return total_written > 0 ? static_cast<snd_pcm_sframes_t>(total_written) : -EAGAIN;
        }
        const snd_pcm_sframes_t available = snd_pcm_avail_update(pcm_);
        if (available < 0) {
            const int recovered = recover_stream_error(static_cast<int>(available));
            if (recovered < 0) {
                return total_written > 0 ? static_cast<snd_pcm_sframes_t>(total_written)
                                         : recovered;
            }
            continue;
        }
        if (available == 0) {
            continue;
        }
        const snd_pcm_uframes_t remaining = frame_count - total_written;
        const snd_pcm_uframes_t writable =
            std::min(remaining, static_cast<snd_pcm_uframes_t>(available));
        const void* cursor = bytes + static_cast<size_t>(total_written) * frame_bytes();
        const snd_pcm_sframes_t written = snd_pcm_writei(pcm_, cursor, writable);
        if (written > 0) {
            total_written += static_cast<snd_pcm_uframes_t>(written);
            auto submitted = static_cast<uint64_t>(written);
            while (submitted > 0) {
                const uint32_t chunk =
                    submitted > std::numeric_limits<uint32_t>::max()
                        ? std::numeric_limits<uint32_t>::max()
                        : static_cast<uint32_t>(submitted);
                tracker_.submit(chunk);
                submitted -= chunk;
            }
            poll_status();
            continue;
        }
        if (written == 0) {
            return total_written > 0 ? static_cast<snd_pcm_sframes_t>(total_written) : -EAGAIN;
        }
        const int recovered = recover_stream_error(static_cast<int>(written));
        if (recovered < 0) {
            return total_written > 0 ? static_cast<snd_pcm_sframes_t>(total_written) : recovered;
        }
    }
    return static_cast<snd_pcm_sframes_t>(total_written);
}

std::optional<PlayoutObservation> AlsaPlayoutAdapter::poll_status() {
    if (pcm_ == nullptr) {
        return std::nullopt;
    }

    snd_pcm_status_t* status = nullptr;
    snd_pcm_status_alloca(&status);
    const int rc = snd_pcm_status(pcm_, status);
    if (rc < 0) {
        if (rc == -EPIPE || rc == -ESTRPIPE) {
            recover_stream_error(rc);
        }
        return std::nullopt;
    }

    const snd_pcm_state_t state = snd_pcm_status_get_state(status);
    const bool underrun = state == SND_PCM_STATE_XRUN;
    if (underrun) {
        recover_stream_error(-EPIPE);
        return std::nullopt;
    }

    snd_htimestamp_t timestamp{};
    snd_pcm_status_get_htstamp(status, &timestamp);
    if (!valid_htimestamp(timestamp)) {
        return std::nullopt;
    }

    const snd_pcm_sframes_t delay = snd_pcm_status_get_delay(status);
    const uint64_t queued_frames = delay > 0 ? static_cast<uint64_t>(delay) : 0;
    auto observation = tracker_.observe_queue(PlayoutQueueSnapshot{
        .generation = generation_,
        .observed_at_us = htimestamp_to_monotonic_us(timestamp),
        .queued_frames = queued_frames,
        .error_bound_us = timestamp_error_bound_us_,
        .source = PlayoutClockSource::ALSA_HTIMESTAMP,
        .quality = PlayoutClockQuality::BOUNDED,
        .underrun = underrun,
    });
    emit_if_present(observation);
    return observation;
}

uint32_t AlsaPlayoutAdapter::generation() const {
    return generation_;
}

int AlsaPlayoutAdapter::recover_stream_error(int error_code) {
    if (pcm_ == nullptr) {
        return -EINVAL;
    }
    if (error_code != -EPIPE && error_code != -ESTRPIPE) {
        return error_code;
    }

    const int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
    emit_if_present(PlayoutObservation{
        .generation = generation_,
        .frames_played = 0,
        .observed_at_us = now_us,
        .finish_timestamp_us = now_us,
        .error_bound_us = -1,
        .source = PlayoutClockSource::ESTIMATED,
        .quality = PlayoutClockQuality::ESTIMATED,
        .underrun = true,
    });
    const int rc = snd_pcm_recover(pcm_, error_code, 0);
    tracker_.reset(generation_);
    return rc;
}

void AlsaPlayoutAdapter::emit_if_present(
    const std::optional<PlayoutObservation>& observation) const {
    if (observation.has_value() && on_observation_) {
        on_observation_(*observation);
    }
}

size_t AlsaPlayoutAdapter::frame_bytes() const {
    return static_cast<size_t>(channels_) * static_cast<size_t>(bytes_per_sample_);
}

}  // namespace sendspin
