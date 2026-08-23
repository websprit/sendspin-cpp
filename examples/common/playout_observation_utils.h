// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "sendspin/player_role.h"

#include <chrono>
#include <atomic>
#include <cstdint>

inline int64_t sendspin_example_monotonic_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

class SendspinExamplePlayoutObserver {
public:
    explicit SendspinExamplePlayoutObserver(sendspin::PlayerRole& player) : player_(player) {}

    void reset_for_stream() {
        generation_.store(player_.playout_generation(), std::memory_order_release);
    }

    void notify_portaudio_played(uint32_t frames_played, int64_t finish_timestamp_us) {
        player_.notify_playout_observed(sendspin::PlayoutObservation{
            .generation = generation_.load(std::memory_order_acquire),
            .frames_played = frames_played,
            .observed_at_us = sendspin_example_monotonic_us(),
            .finish_timestamp_us = finish_timestamp_us,
            .error_bound_us = 0,
            .source = sendspin::PlayoutClockSource::PORTAUDIO_DAC_TIME,
            .quality = sendspin::PlayoutClockQuality::BOUNDED,
            .underrun = false,
        });
    }

private:
    sendspin::PlayerRole& player_;
    std::atomic<uint32_t> generation_{0};
};
