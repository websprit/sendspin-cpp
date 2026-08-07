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

#include "audio_utils.h"
#include "windowed_sinc_resampler.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace sendspin {
namespace {

// Signal lengths, chunk boundaries, and sample patterns are deliberately concrete test vectors.
// NOLINTBEGIN(readability-magic-numbers)

AudioSampleBuffer process_and_drain(WindowedSincResampler& resampler,
                                    const std::vector<int32_t>& input) {
    AudioSampleBuffer output;
    AudioSampleBuffer tail;
    EXPECT_TRUE(resampler.process(input.data(), input.size(), output));
    EXPECT_TRUE(resampler.drain(tail));
    EXPECT_TRUE(output.append(tail.data(), tail.size()));
    return output;
}

void expect_buffers_equal(const AudioSampleBuffer& actual, const AudioSampleBuffer& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    EXPECT_TRUE(std::equal(actual.data(), actual.data() + actual.size(), expected.data()));
}

TEST(WindowedSincResampler, StreamingChunksMatchSingleBuffer) {
    std::vector<int32_t> input(2048);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<int32_t>((static_cast<int64_t>(i) * 7919) % 1000000 - 500000);
    }

    WindowedSincResampler whole(1);
    whole.set_correction_ppm(317.0);
    const auto whole_output = process_and_drain(whole, input);

    WindowedSincResampler split(1);
    split.set_correction_ppm(317.0);
    AudioSampleBuffer split_output;
    for (size_t offset = 0; offset < input.size(); offset += 37) {
        const size_t count = std::min<size_t>(37, input.size() - offset);
        AudioSampleBuffer block;
        ASSERT_TRUE(split.process(input.data() + offset, count, block));
        ASSERT_TRUE(split_output.append(block.data(), block.size()));
    }
    AudioSampleBuffer tail;
    ASSERT_TRUE(split.drain(tail));
    ASSERT_TRUE(split_output.append(tail.data(), tail.size()));

    expect_buffers_equal(split_output, whole_output);
}

TEST(WindowedSincResampler, CorrectionDirectionChangesOutputFrameCount) {
    const std::vector<int32_t> input(48000, 1000000);

    WindowedSincResampler faster(1);
    faster.set_correction_ppm(500.0);
    const auto faster_output = process_and_drain(faster, input);

    WindowedSincResampler slower(1);
    slower.set_correction_ppm(-500.0);
    const auto slower_output = process_and_drain(slower, input);

    EXPECT_LT(faster_output.size(), input.size());
    EXPECT_GT(slower_output.size(), input.size());
    EXPECT_GT(slower_output.size(), faster_output.size());
}

TEST(WindowedSincResampler, PreservesConstantSignalAwayFromStreamEdges) {
    const int32_t level = 0x12340000;
    const std::vector<int32_t> input(4096, level);
    WindowedSincResampler resampler(1);
    resampler.set_correction_ppm(400.0);

    const auto output = process_and_drain(resampler, input);
    ASSERT_GT(output.size(), 200U);
    for (size_t i = 100; i + 100 < output.size(); ++i) {
        EXPECT_NEAR(output[i], level, 2);
    }
}

TEST(WindowedSincResampler, ResetRemovesPriorStreamHistory) {
    WindowedSincResampler reused(1);
    const std::vector<int32_t> loud(256, 0x40000000);
    const std::vector<int32_t> silence(256, 0);
    (void)process_and_drain(reused, loud);
    reused.reset();
    const auto reused_output = process_and_drain(reused, silence);

    WindowedSincResampler fresh(1);
    const auto fresh_output = process_and_drain(fresh, silence);
    expect_buffers_equal(reused_output, fresh_output);
}

TEST(WindowedSincResampler, PreservesInterleavedPcmWidthsAndChannels) {
    struct FormatCase {
        size_t bytes_per_sample;
        int32_t left;
        int32_t right;
    };
    const FormatCase cases[] = {
        {2, 0x12340000, -0x23450000},
        {3, 0x12345600, -0x23456700},
        {4, 0x12345678, -0x23456700},
    };

    for (const auto& test_case : cases) {
        constexpr size_t FRAMES = 512;
        std::vector<uint8_t> pcm(FRAMES * 2 * test_case.bytes_per_sample);
        for (size_t frame = 0; frame < FRAMES; ++frame) {
            pack_q31_as_audio_sample(test_case.left,
                                     pcm.data() + (frame * 2) * test_case.bytes_per_sample,
                                     test_case.bytes_per_sample);
            pack_q31_as_audio_sample(test_case.right,
                                     pcm.data() + (frame * 2 + 1) * test_case.bytes_per_sample,
                                     test_case.bytes_per_sample);
        }

        WindowedSincResampler resampler(2);
        resampler.set_correction_ppm(300.0);
        AudioSampleBuffer output;
        AudioSampleBuffer tail;
        ASSERT_TRUE(resampler.process_pcm(pcm.data(), FRAMES, test_case.bytes_per_sample, output));
        ASSERT_TRUE(resampler.drain(tail));
        ASSERT_TRUE(output.append(tail.data(), tail.size()));

        ASSERT_GT(output.size(), 200U);
        for (size_t sample = 200; sample + 200 < output.size(); sample += 2) {
            EXPECT_NEAR(output[sample], test_case.left, 2);
            EXPECT_NEAR(output[sample + 1], test_case.right, 2);
        }
    }
}

// NOLINTEND(readability-magic-numbers)

}  // namespace
}  // namespace sendspin
