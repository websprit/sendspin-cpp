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

#include "audio_drift_controller.h"

#include "constants.h"

#include <algorithm>
#include <cmath>

namespace sendspin {

AudioDriftController::AudioDriftController(AudioDriftControllerConfig config) : config_(config) {
    const AudioDriftControllerConfig defaults;
    if (this->config_.maximum_correction_ppm <= 0.0) {
        this->config_.maximum_correction_ppm = defaults.maximum_correction_ppm;
    }
    if (this->config_.maximum_step_ppm <= 0.0) {
        this->config_.maximum_step_ppm = defaults.maximum_step_ppm;
    }
    this->config_.maximum_step_ppm =
        std::min(this->config_.maximum_step_ppm, this->config_.maximum_correction_ppm);
    if (this->config_.deadband_us < 0) {
        this->config_.deadband_us = defaults.deadband_us;
    }
    if (this->config_.update_interval_us <= 0) {
        this->config_.update_interval_us = defaults.update_interval_us;
    }
}

double AudioDriftController::update(int64_t endpoint_error_us, int64_t now_us) {
    if (!this->has_update_) {
        this->last_update_us_ = now_us;
        this->has_update_ = true;
        return this->current_ppm_;
    }

    const int64_t elapsed_us = now_us - this->last_update_us_;
    if (elapsed_us < this->config_.update_interval_us) {
        return this->current_ppm_;
    }
    this->last_update_us_ = now_us;

    if (std::abs(endpoint_error_us) <= this->config_.deadband_us) {
        endpoint_error_us = 0;
    }

    // Positive endpoint error requires more output frames, hence negative ppm under the
    // resampler's "positive consumes input faster" convention.
    const double error_seconds = -static_cast<double>(endpoint_error_us) / US_PER_SECOND;
    const double elapsed_seconds = static_cast<double>(elapsed_us) / US_PER_SECOND;
    const double candidate_integral = this->integral_seconds_ + error_seconds * elapsed_seconds;
    double wanted = this->config_.proportional_gain * error_seconds +
                    this->config_.integral_gain * candidate_integral;

    // Anti-windup: retain integration while unsaturated, or when the error pulls an already
    // saturated controller back toward the permitted range.
    if (std::abs(wanted) <= this->config_.maximum_correction_ppm || wanted * error_seconds < 0.0) {
        this->integral_seconds_ = candidate_integral;
        wanted = this->config_.proportional_gain * error_seconds +
                 this->config_.integral_gain * this->integral_seconds_;
    }

    wanted = std::clamp(wanted, -this->config_.maximum_correction_ppm,
                        this->config_.maximum_correction_ppm);
    const double delta = std::clamp(wanted - this->current_ppm_, -this->config_.maximum_step_ppm,
                                    this->config_.maximum_step_ppm);
    this->current_ppm_ += delta;
    return this->current_ppm_;
}

void AudioDriftController::reset() {
    this->integral_seconds_ = 0.0;
    this->current_ppm_ = 0.0;
    this->last_update_us_ = 0;
    this->has_update_ = false;
}

}  // namespace sendspin
