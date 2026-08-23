// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file playout_observation.h
/// @brief Platform-neutral observations of audio consumed by a physical output device.

#pragma once

#include <cstdint>
#include <optional>

namespace sendspin {

/// @brief Hardware or fallback source used to determine a playout timestamp.
enum class PlayoutClockSource : uint8_t {
    DMA_COMPLETION,
    ALSA_HTIMESTAMP,
    PORTAUDIO_DAC_TIME,
    ESTIMATED,
};

/// @brief Strength of the timing guarantee carried by a playout observation.
enum class PlayoutClockQuality : uint8_t {
    EXACT,
    BOUNDED,
    ESTIMATED,
};

/// @brief One device-side observation of frames consumed by the audio output.
///
/// `finish_timestamp_us` is expressed in the client's monotonic clock domain. A stream clear,
/// seek, or restart starts a new generation so stale callbacks cannot affect the new stream.
/// Hardware underruns are flagged explicitly; the sync core re-enters alignment while the adapter
/// resets its queue counters. Estimated observations must remain explicitly marked as such.
struct PlayoutObservation {
    uint32_t generation{};
    uint32_t frames_played{};
    int64_t observed_at_us{};
    int64_t finish_timestamp_us{};
    int64_t error_bound_us{-1};
    PlayoutClockSource source{PlayoutClockSource::ESTIMATED};
    PlayoutClockQuality quality{PlayoutClockQuality::ESTIMATED};
    bool underrun{};
};

/// @brief Validates and accumulates a single-generation device playout timeline.
///
/// This small model is intentionally independent of ALSA, I2S, DMA, and PortAudio. Platform
/// adapters translate their native timing data into [`PlayoutObservation`] values. The class is
/// not internally synchronized; an adapter should update it from one audio thread and publish
/// snapshots through its platform-appropriate lock-free or synchronized handoff.
class PlayoutTimeline {
public:
    /// @brief Clears progress and selects the generation accepted by future observations.
    void reset(uint32_t generation);

    /// @brief Adds an observation when its generation, timing, and quality are consistent.
    /// @return true when accepted; false for a stale generation or invalid/regressing data.
    bool observe(const PlayoutObservation& observation);

    /// @brief Returns the most recently accepted observation, if any.
    std::optional<PlayoutObservation> snapshot() const;

    /// @brief Returns the total frames reported in the current generation.
    uint64_t total_frames() const;

    /// @brief Returns accepted underrun observations in the current generation.
    uint32_t underrun_count() const;

private:
    bool quality_is_valid(const PlayoutObservation& observation) const;

    uint32_t generation_{};
    uint64_t total_frames_{};
    uint32_t underrun_count_{};
    std::optional<PlayoutObservation> latest_{};
};

/// @brief Platform-neutral snapshot of a hardware or driver playback queue.
///
/// On Linux, `observed_at_us` and `queued_frames` map directly to ALSA's hardware timestamp and
/// delay fields. DMA-based adapters can provide the same information from a descriptor counter.
struct PlayoutQueueSnapshot {
    uint32_t generation{};
    int64_t observed_at_us{};
    uint64_t queued_frames{};
    int64_t error_bound_us{-1};
    PlayoutClockSource source{PlayoutClockSource::ESTIMATED};
    PlayoutClockQuality quality{PlayoutClockQuality::ESTIMATED};
    bool underrun{};
};

/// @brief Converts submitted-frame and device-queue counters into playout observations.
///
/// This is the reusable portion of an ALSA delay/htimestamp or DMA descriptor adapter. The
/// platform owns the audio handle and calls `submit()` after a successful write, then passes
/// queue snapshots from its native status API to `observe_queue()`.
class QueuedPlayoutTracker {
public:
    /// @brief Clears counters and selects the active stream generation.
    void reset(uint32_t generation);

    /// @brief Records frames successfully accepted by the platform output.
    void submit(uint32_t frames);

    /// @brief Converts a queue-depth sample into newly consumed frames.
    /// @return No value for stale, regressing, or unchanged samples without an underrun.
    std::optional<PlayoutObservation> observe_queue(const PlayoutQueueSnapshot& snapshot);

private:
    uint32_t generation_{};
    uint64_t submitted_frames_{};
    uint64_t reported_frames_{};
    int64_t last_observed_at_us_{};
    bool has_observation_{};
};

}  // namespace sendspin
