// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "sendspin/playout_observation.h"

#include <alsa/asoundlib.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace sendspin {

/// @brief Linux ALSA timing adapter for PlayerRole::notify_playout_observed().
///
/// The adapter does not own the PCM device. The application opens and configures `snd_pcm_t`,
/// then passes it here so successful writes and ALSA htimestamp/delay snapshots can be converted
/// through QueuedPlayoutTracker into platform-neutral PlayoutObservation values.
class AlsaPlayoutAdapter {
public:
    using ObservationCallback = std::function<void(const PlayoutObservation&)>;

    struct Config {
        snd_pcm_t* pcm{};
        uint16_t channels{};
        uint16_t bytes_per_sample{};
        int write_timeout_ms{100};
        int64_t timestamp_error_bound_us{1000};
        ObservationCallback on_observation{};
    };

    explicit AlsaPlayoutAdapter(Config config);

    AlsaPlayoutAdapter(const AlsaPlayoutAdapter&) = delete;
    AlsaPlayoutAdapter& operator=(const AlsaPlayoutAdapter&) = delete;

    /// @brief Enables ALSA monotonic hardware timestamps on an already configured PCM handle.
    /// @return 0 on success, otherwise a negative ALSA error code.
    int configure_monotonic_timestamps();

    /// @brief Clears queued counters after stream start, seek, clear, or restart.
    void reset(uint32_t generation);

    /// @brief Writes interleaved frames and records every partial write accepted by ALSA.
    /// @return Accepted frames, or a negative ALSA error code when no frame was accepted.
    snd_pcm_sframes_t write_interleaved(const void* frames, snd_pcm_uframes_t frame_count);

    /// @brief Samples ALSA htimestamp/delay and emits a playout observation when progress changed.
    std::optional<PlayoutObservation> poll_status();

    uint32_t generation() const;

private:
    int recover_stream_error(int error_code);
    void emit_if_present(const std::optional<PlayoutObservation>& observation) const;
    size_t frame_bytes() const;

    snd_pcm_t* pcm_{};
    uint16_t channels_{};
    uint16_t bytes_per_sample_{};
    int write_timeout_ms_{};
    int64_t timestamp_error_bound_us_{};
    ObservationCallback on_observation_{};
    QueuedPlayoutTracker tracker_{};
    uint32_t generation_{};
};

}  // namespace sendspin
