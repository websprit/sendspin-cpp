// Copyright 2026 Sendspin Contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/// @file windowed_sinc_resampler.h
/// @brief Streaming fixed-point polyphase windowed-sinc ASRC for small clock corrections

#pragma once

#include "platform/memory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace sendspin {

/// @brief Growable Q31 sample buffer using the platform's external-memory-preferring allocator.
/// Allocation failures are reported explicitly instead of throwing from the real-time sync task.
class AudioSampleBuffer {
public:
    AudioSampleBuffer() = default;
    AudioSampleBuffer(AudioSampleBuffer&&) noexcept = default;
    AudioSampleBuffer& operator=(AudioSampleBuffer&&) noexcept = default;
    AudioSampleBuffer(const AudioSampleBuffer&) = delete;
    AudioSampleBuffer& operator=(const AudioSampleBuffer&) = delete;

    bool reserve(size_t sample_capacity);
    bool resize(size_t sample_count);
    bool append(const int32_t* samples, size_t sample_count);
    bool append_zeros(size_t sample_count);
    void erase_prefix(size_t sample_count);
    void clear() {
        this->size_ = 0;
    }

    size_t size() const {
        return this->size_;
    }
    bool empty() const {
        return this->size_ == 0;
    }
    int32_t* data() {
        return this->storage_ ? this->storage_.as<int32_t>() : nullptr;
    }
    const int32_t* data() const {
        return this->storage_ ? this->storage_.as<int32_t>() : nullptr;
    }
    int32_t& operator[](size_t index) {
        return this->data()[index];
    }
    const int32_t& operator[](size_t index) const {
        return this->data()[index];
    }

private:
    PlatformBuffer storage_;
    size_t size_{0};
};

/// @brief Band-limited streaming resampler optimized for small continuous rate corrections.
///
/// Samples use the library's Q31 representation. Coefficients use Q30 fixed point so the inner
/// loop avoids floating-point work on embedded targets; floating point is used only once when an
/// instance builds its shared-size coefficient table.
class WindowedSincResampler {
public:
    static constexpr size_t TAPS = 32;
    static constexpr size_t PHASES = 128;
    static constexpr double MAX_CORRECTION_PPM = 5000.0;

    explicit WindowedSincResampler(uint8_t channels);

    /// @brief Sets the input-frames-per-output-frame correction.
    /// Positive ppm consumes input faster and therefore produces fewer output frames.
    void set_correction_ppm(double ppm);

    /// @brief Appends interleaved Q31 samples and emits every frame with sufficient look-ahead.
    bool process(const int32_t* input, size_t input_samples, AudioSampleBuffer& output);

    /// @brief Appends interleaved little-endian PCM without allocating an intermediate Q31 copy.
    bool process_pcm(const uint8_t* input, size_t input_frames, size_t bytes_per_sample,
                     AudioSampleBuffer& output);

    /// @brief Emits the finite stream tail using silence as missing future input, then resets.
    bool drain(AudioSampleBuffer& output);

    /// @brief Clears history and restores a 1:1 conversion ratio.
    void reset();

    /// @brief Source-frame coordinate represented by the next output frame.
    double next_source_frame() const;

    uint8_t channels() const {
        return this->channels_;
    }

private:
    static constexpr size_t HISTORY = TAPS / 2 - 1;
    static constexpr uint64_t PHASE_UNIT = UINT64_C(1) << 32;
    static constexpr int64_t COEFFICIENT_SCALE = INT64_C(1) << 30;

    static void build_coefficients();
    bool produce(AudioSampleBuffer& output);
    int32_t interpolate_sample(size_t left_frame, uint32_t fractional_phase, uint8_t channel) const;

    uint8_t channels_;
    using CoefficientTable = std::array<std::array<int32_t, TAPS>, PHASES + 1>;
    static CoefficientTable coefficients_;
    static std::once_flag coefficients_once_;

    AudioSampleBuffer pending_;
    uint64_t position_{0};
    uint64_t step_{PHASE_UNIT};
    int64_t pending_start_frame_{-static_cast<int64_t>(HISTORY)};
    bool ready_{false};
};

}  // namespace sendspin
