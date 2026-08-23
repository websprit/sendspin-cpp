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

/// @file audio_drift_controller.h
/// @brief PI controller that turns DAC playout-endpoint error into a smooth ASRC rate correction

#pragma once

#include <cstdint>

namespace sendspin {

struct AudioDriftControllerConfig {
    static constexpr int64_t DEFAULT_DEADBAND_US = 100;
    static constexpr int64_t DEFAULT_UPDATE_INTERVAL_US = 250000;
    double proportional_gain{2000.0};
    double integral_gain{1000.0};
    double maximum_correction_ppm{500.0};
    double maximum_step_ppm{10.0};
    int64_t deadband_us{DEFAULT_DEADBAND_US};
    int64_t update_interval_us{DEFAULT_UPDATE_INTERVAL_US};
};

/// @brief Keeps the predicted DAC queue endpoint aligned with incoming audio timestamps.
///
/// A positive endpoint error means the next audio starts later than the DAC queue currently ends.
/// The ASRC must therefore produce more output frames, represented by a negative correction ppm.
class AudioDriftController {
public:
    explicit AudioDriftController(AudioDriftControllerConfig config = {});

    /// @brief Observes an endpoint error and returns the current ASRC input/output rate correction.
    /// @param endpoint_error_us Target chunk start minus predicted DAC queue endpoint.
    /// @param now_us Monotonic observation time in microseconds.
    /// @return Correction in ppm. Positive consumes input faster and produces fewer output frames.
    double update(int64_t endpoint_error_us, int64_t now_us);

    double current_ppm() const {
        return this->current_ppm_;
    }

    void reset();

private:
    AudioDriftControllerConfig config_;
    double integral_seconds_{0.0};
    double current_ppm_{0.0};
    int64_t last_update_us_{0};
    bool has_update_{false};
};

}  // namespace sendspin
