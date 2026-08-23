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
#include <gtest/gtest.h>

#include <cmath>

namespace sendspin {
namespace {

// Controller tests intentionally use concrete timing/error magnitudes to make the expected PI
// response visible at the assertion site.
// NOLINTBEGIN(readability-magic-numbers)

TEST(AudioDriftController, PositiveEndpointErrorProducesMoreOutputFrames) {
    AudioDriftController controller;

    EXPECT_DOUBLE_EQ(controller.update(1000, 0), 0.0);
    EXPECT_NEAR(controller.update(1000, 250000), -2.25, 0.001);
}

TEST(AudioDriftController, CorrectionIsSlewLimitedAndBounded) {
    AudioDriftController controller;

    controller.update(100000, 0);
    EXPECT_DOUBLE_EQ(controller.update(100000, 250000), -10.0);

    double correction = 0.0;
    for (int i = 2; i <= 100; ++i) {
        correction = controller.update(100000, i * 250000LL);
    }
    EXPECT_DOUBLE_EQ(correction, -500.0);
}

TEST(AudioDriftController, DeadbandDoesNotChaseTimestampNoise) {
    AudioDriftController controller;

    controller.update(50, 0);
    EXPECT_DOUBLE_EQ(controller.update(-50, 250000), 0.0);
}

TEST(AudioDriftController, ResetReturnsToNominalRate) {
    AudioDriftController controller;

    controller.update(-100000, 0);
    ASSERT_DOUBLE_EQ(controller.update(-100000, 250000), 10.0);
    controller.reset();

    EXPECT_DOUBLE_EQ(controller.current_ppm(), 0.0);
    EXPECT_DOUBLE_EQ(controller.update(-100000, 500000), 0.0);
}

TEST(AudioDriftController, InvalidPublicLimitsFallBackToSafeDefaults) {
    AudioDriftControllerConfig config;
    config.maximum_correction_ppm = -1.0;
    config.maximum_step_ppm = -1.0;
    AudioDriftController controller(config);

    controller.update(-100000, 0);
    EXPECT_DOUBLE_EQ(controller.update(-100000, 250000), 10.0);
}

double simulate_device_drift(double hardware_ppm, int64_t duration_us) {
    AudioDriftController controller;
    constexpr int64_t step_us = 250000;
    double endpoint_error_us = 0.0;
    double correction_ppm = controller.update(0, 0);
    for (int64_t now_us = step_us; now_us <= duration_us; now_us += step_us) {
        const double elapsed_seconds = static_cast<double>(step_us) / 1'000'000.0;
        endpoint_error_us += (hardware_ppm + correction_ppm) * elapsed_seconds;
        correction_ppm = controller.update(static_cast<int64_t>(endpoint_error_us), now_us);
    }
    return endpoint_error_us;
}

TEST(AudioDriftController, TwoOppositeHardwareClocksStayWithinSubMillisecondAfterTenMinutes) {
    const double fast_device_error = simulate_device_drift(40.0, 600'000'000);
    const double slow_device_error = simulate_device_drift(-35.0, 600'000'000);

    EXPECT_LT(std::abs(fast_device_error - slow_device_error), 500.0)
        << "fast=" << fast_device_error << " slow=" << slow_device_error;
}

// NOLINTEND(readability-magic-numbers)

}  // namespace
}  // namespace sendspin
