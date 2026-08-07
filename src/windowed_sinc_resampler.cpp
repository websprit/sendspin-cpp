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

#include "windowed_sinc_resampler.h"

#include "audio_utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace sendspin {
namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double SINC_CUTOFF = 0.94;
constexpr double PARTS_PER_MILLION = 1000000.0;
constexpr size_t INITIAL_SAMPLE_CAPACITY = 64U;

double sinc(double value) {
    if (std::abs(value) < 1e-12) {
        return 1.0;
    }
    value *= PI;
    return std::sin(value) / value;
}

double blackman_harris(double offset) {
    constexpr double A0 = 0.35875;
    constexpr double A1 = 0.48829;
    constexpr double A2 = 0.14128;
    constexpr double A3 = 0.01168;
    constexpr double SUPPORT = static_cast<double>(WindowedSincResampler::TAPS) / 2.0;
    if (std::abs(offset) >= SUPPORT) {
        return 0.0;
    }
    const double angle = PI * offset / SUPPORT;
    return A0 + A1 * std::cos(angle) + A2 * std::cos(2.0 * angle) + A3 * std::cos(3.0 * angle);
}

}  // namespace

bool AudioSampleBuffer::reserve(size_t sample_capacity) {
    const size_t current_capacity = this->storage_.size() / sizeof(int32_t);
    if (sample_capacity <= current_capacity) {
        return true;
    }
    if (sample_capacity > std::numeric_limits<size_t>::max() / sizeof(int32_t)) {
        return false;
    }

    const size_t doubled_capacity = current_capacity > std::numeric_limits<size_t>::max() / 2U
                                        ? std::numeric_limits<size_t>::max()
                                        : current_capacity * 2U;
    const size_t new_capacity =
        std::max(sample_capacity, std::max(INITIAL_SAMPLE_CAPACITY, doubled_capacity));
    if (new_capacity > std::numeric_limits<size_t>::max() / sizeof(int32_t)) {
        return false;
    }
    const size_t byte_capacity = new_capacity * sizeof(int32_t);
    return this->storage_ ? this->storage_.realloc(byte_capacity)
                          : this->storage_.allocate(byte_capacity);
}

bool AudioSampleBuffer::resize(size_t sample_count) {
    if (!this->reserve(sample_count)) {
        return false;
    }
    this->size_ = sample_count;
    return true;
}

bool AudioSampleBuffer::append(const int32_t* samples, size_t sample_count) {
    if (sample_count == 0U) {
        return true;
    }
    if (samples == nullptr || sample_count > std::numeric_limits<size_t>::max() - this->size_) {
        return false;
    }
    const size_t previous_size = this->size_;
    if (!this->resize(previous_size + sample_count)) {
        return false;
    }
    std::memcpy(this->data() + previous_size, samples, sample_count * sizeof(int32_t));
    return true;
}

bool AudioSampleBuffer::append_zeros(size_t sample_count) {
    if (sample_count == 0U) {
        return true;
    }
    if (sample_count > std::numeric_limits<size_t>::max() - this->size_) {
        return false;
    }
    const size_t previous_size = this->size_;
    if (!this->resize(previous_size + sample_count)) {
        return false;
    }
    std::memset(this->data() + previous_size, 0, sample_count * sizeof(int32_t));
    return true;
}

void AudioSampleBuffer::erase_prefix(size_t sample_count) {
    const size_t erased = std::min(sample_count, this->size_);
    const size_t remaining = this->size_ - erased;
    if (remaining > 0U) {
        std::memmove(this->data(), this->data() + erased, remaining * sizeof(int32_t));
    }
    this->size_ = remaining;
}

WindowedSincResampler::WindowedSincResampler(uint8_t channels)
    : channels_(std::max<uint8_t>(channels, 1U)) {
    std::call_once(coefficients_once_, build_coefficients);
    this->reset();
}

WindowedSincResampler::CoefficientTable WindowedSincResampler::coefficients_{};
std::once_flag WindowedSincResampler::coefficients_once_;

void WindowedSincResampler::set_correction_ppm(double ppm) {
    ppm = std::clamp(ppm, -WindowedSincResampler::MAX_CORRECTION_PPM,
                     WindowedSincResampler::MAX_CORRECTION_PPM);
    const double input_per_output = 1.0 + ppm / PARTS_PER_MILLION;
    this->step_ =
        static_cast<uint64_t>(std::llround(input_per_output * static_cast<double>(PHASE_UNIT)));
}

bool WindowedSincResampler::process(const int32_t* input, size_t input_samples,
                                    AudioSampleBuffer& output) {
    output.clear();
    if (!this->ready_) {
        return false;
    }
    const size_t complete_samples = input_samples - input_samples % this->channels_;
    if (!this->pending_.append(input, complete_samples)) {
        return false;
    }
    return this->produce(output);
}

bool WindowedSincResampler::process_pcm(const uint8_t* input, size_t input_frames,
                                        size_t bytes_per_sample, AudioSampleBuffer& output) {
    output.clear();
    if (!this->ready_ || input_frames > std::numeric_limits<size_t>::max() / this->channels_) {
        return false;
    }
    const size_t input_samples = input_frames * this->channels_;
    if (input_samples > std::numeric_limits<size_t>::max() - this->pending_.size()) {
        return false;
    }
    const size_t previous_size = this->pending_.size();
    if (!this->pending_.resize(previous_size + input_samples)) {
        return false;
    }
    for (size_t sample = 0; sample < input_samples; ++sample) {
        this->pending_[previous_size + sample] =
            unpack_audio_sample_to_q31(input + sample * bytes_per_sample, bytes_per_sample);
    }
    return this->produce(output);
}

bool WindowedSincResampler::produce(AudioSampleBuffer& output) {
    output.clear();

    const size_t frames = this->pending_.size() / this->channels_;
    if (frames < TAPS) {
        return true;
    }

    const double input_per_output =
        static_cast<double>(this->step_) / static_cast<double>(PHASE_UNIT);
    const size_t estimated_frames = static_cast<size_t>(
        std::ceil(static_cast<double>(frames - TAPS + 1) / input_per_output) + 1.0);
    if (estimated_frames > std::numeric_limits<size_t>::max() / this->channels_) {
        return false;
    }
    if (!output.resize(estimated_frames * this->channels_)) {
        return false;
    }
    size_t output_samples = 0U;

    while (true) {
        const size_t base_frame = static_cast<size_t>(this->position_ >> 32);
        if (base_frame < HISTORY) {
            break;
        }
        const size_t left_frame = base_frame - HISTORY;
        if (left_frame + TAPS > frames) {
            break;
        }

        const uint32_t fractional_phase = static_cast<uint32_t>(this->position_);
        for (uint8_t channel = 0; channel < this->channels_; ++channel) {
            output[output_samples++] =
                this->interpolate_sample(left_frame, fractional_phase, channel);
        }
        this->position_ += this->step_;
    }

    const int64_t consumed_frames =
        static_cast<int64_t>(this->position_ >> 32) - static_cast<int64_t>(HISTORY);
    if (consumed_frames > 0) {
        const size_t consumed_samples = static_cast<size_t>(consumed_frames) * this->channels_;
        this->pending_.erase_prefix(consumed_samples);
        this->position_ -= static_cast<uint64_t>(consumed_frames) * PHASE_UNIT;
        this->pending_start_frame_ += consumed_frames;
    }
    (void)output.resize(output_samples);
    return true;
}

bool WindowedSincResampler::drain(AudioSampleBuffer& output) {
    output.clear();
    if (!this->ready_ || !this->pending_.append_zeros((TAPS / 2U) * this->channels_) ||
        !this->produce(output)) {
        return false;
    }
    this->reset();
    return this->ready_;
}

void WindowedSincResampler::reset() {
    this->step_ = PHASE_UNIT;
    this->position_ = static_cast<uint64_t>(HISTORY) * PHASE_UNIT;
    this->pending_start_frame_ = -static_cast<int64_t>(HISTORY);
    this->pending_.clear();
    this->ready_ = this->pending_.append_zeros(HISTORY * this->channels_);
}

double WindowedSincResampler::next_source_frame() const {
    return static_cast<double>(this->pending_start_frame_) +
           static_cast<double>(this->position_) / static_cast<double>(PHASE_UNIT);
}

void WindowedSincResampler::build_coefficients() {
    for (size_t phase = 0; phase <= PHASES; ++phase) {
        const double fraction = static_cast<double>(phase) / static_cast<double>(PHASES);
        std::array<double, TAPS> floating{};
        double sum = 0.0;
        for (size_t tap = 0; tap < TAPS; ++tap) {
            const double offset =
                static_cast<double>(tap) - static_cast<double>(HISTORY) - fraction;
            const double coefficient =
                SINC_CUTOFF * sinc(SINC_CUTOFF * offset) * blackman_harris(offset);
            floating[tap] = coefficient;
            sum += coefficient;
        }

        int64_t quantized_sum = 0;
        for (size_t tap = 0; tap < TAPS; ++tap) {
            const auto coefficient = static_cast<int32_t>(
                std::llround((floating[tap] / sum) * static_cast<double>(COEFFICIENT_SCALE)));
            coefficients_[phase][tap] = coefficient;
            quantized_sum += coefficient;
        }
        coefficients_[phase][HISTORY] += static_cast<int32_t>(COEFFICIENT_SCALE - quantized_sum);
    }
}

int32_t WindowedSincResampler::interpolate_sample(size_t left_frame, uint32_t fractional_phase,
                                                  uint8_t channel) const {
    const uint64_t scaled_phase = static_cast<uint64_t>(fractional_phase) * PHASES;
    const size_t phase = static_cast<size_t>(scaled_phase >> 32);
    const uint32_t mix = static_cast<uint32_t>(scaled_phase);

    int64_t accumulator = 0;
    int64_t coefficient_sum = 0;
    for (size_t tap = 0; tap < TAPS; ++tap) {
        const int64_t first = this->coefficients_[phase][tap];
        const int64_t delta = static_cast<int64_t>(this->coefficients_[phase + 1][tap]) - first;
        const int64_t coefficient = first + ((delta * mix) >> 32);
        const int32_t sample = this->pending_[(left_frame + tap) * this->channels_ + channel];
        accumulator += static_cast<int64_t>(sample) * coefficient;
        coefficient_sum += coefficient;
    }
    // Per-coefficient phase interpolation rounds toward an integer Q30 value. Correct the tiny
    // resulting sum error on the center sample so a DC signal remains exactly unity gain.
    const int32_t center_sample =
        this->pending_[(left_frame + HISTORY) * this->channels_ + channel];
    accumulator += static_cast<int64_t>(center_sample) * (COEFFICIENT_SCALE - coefficient_sum);

    const int64_t rounded = accumulator >= 0
                                ? (accumulator + COEFFICIENT_SCALE / 2) / COEFFICIENT_SCALE
                                : (accumulator - COEFFICIENT_SCALE / 2) / COEFFICIENT_SCALE;
    return static_cast<int32_t>(std::clamp<int64_t>(rounded, std::numeric_limits<int32_t>::min(),
                                                    std::numeric_limits<int32_t>::max()));
}

}  // namespace sendspin
