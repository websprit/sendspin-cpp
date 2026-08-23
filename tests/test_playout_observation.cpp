#include "sendspin/playout_observation.h"

#include <gtest/gtest.h>

namespace sendspin {
namespace {

PlayoutObservation exact_observation(uint32_t generation, uint32_t frames, int64_t finish_us) {
    return PlayoutObservation{
        .generation = generation,
        .frames_played = frames,
        .observed_at_us = finish_us,
        .finish_timestamp_us = finish_us,
        .error_bound_us = 0,
        .source = PlayoutClockSource::DMA_COMPLETION,
        .quality = PlayoutClockQuality::EXACT,
        .underrun = false,
    };
}

TEST(PlayoutTimelineTest, AcceptsMonotonicObservationsWithinGeneration) {
    PlayoutTimeline timeline;
    timeline.reset(7);

    EXPECT_TRUE(timeline.observe(exact_observation(7, 960, 1'020'000)));
    EXPECT_TRUE(timeline.observe(exact_observation(7, 960, 1'040'000)));

    const auto snapshot = timeline.snapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->generation, 7U);
    EXPECT_EQ(snapshot->finish_timestamp_us, 1'040'000);
    EXPECT_EQ(timeline.total_frames(), 1'920U);
}

TEST(PlayoutTimelineTest, RejectsStaleGenerationAndRegressingTimestamp) {
    PlayoutTimeline timeline;
    timeline.reset(4);
    ASSERT_TRUE(timeline.observe(exact_observation(4, 480, 510'000)));

    EXPECT_FALSE(timeline.observe(exact_observation(3, 480, 520'000)));
    EXPECT_FALSE(timeline.observe(exact_observation(4, 480, 509'999)));
    EXPECT_EQ(timeline.total_frames(), 480U);
}

TEST(PlayoutTimelineTest, ResetStartsANewGenerationWithoutLeakingOldProgress) {
    PlayoutTimeline timeline;
    timeline.reset(1);
    ASSERT_TRUE(timeline.observe(exact_observation(1, 960, 20'000)));

    timeline.reset(2);

    EXPECT_FALSE(timeline.snapshot().has_value());
    EXPECT_EQ(timeline.total_frames(), 0U);
    EXPECT_TRUE(timeline.observe(exact_observation(2, 480, 5'000)));
}

TEST(PlayoutTimelineTest, PreservesEstimatedQualityAndUnderrunTelemetry) {
    PlayoutTimeline timeline;
    timeline.reset(9);
    const PlayoutObservation estimated{
        .generation = 9,
        .frames_played = 0,
        .observed_at_us = 1'000'000,
        .finish_timestamp_us = 1'020'000,
        .error_bound_us = -1,
        .source = PlayoutClockSource::ESTIMATED,
        .quality = PlayoutClockQuality::ESTIMATED,
        .underrun = true,
    };

    ASSERT_TRUE(timeline.observe(estimated));
    const auto snapshot = timeline.snapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->quality, PlayoutClockQuality::ESTIMATED);
    EXPECT_TRUE(snapshot->underrun);
    EXPECT_EQ(timeline.underrun_count(), 1U);
}

TEST(PlayoutTimelineTest, RejectsContradictoryQualityBounds) {
    PlayoutTimeline timeline;
    timeline.reset(5);
    auto invalid_exact = exact_observation(5, 480, 10'000);
    invalid_exact.error_bound_us = 100;
    auto invalid_bounded = exact_observation(5, 480, 10'000);
    invalid_bounded.quality = PlayoutClockQuality::BOUNDED;
    invalid_bounded.error_bound_us = -1;
    auto estimated_as_exact = exact_observation(5, 480, 10'000);
    estimated_as_exact.source = PlayoutClockSource::ESTIMATED;

    EXPECT_FALSE(timeline.observe(invalid_exact));
    EXPECT_FALSE(timeline.observe(invalid_bounded));
    EXPECT_FALSE(timeline.observe(estimated_as_exact));
}

TEST(QueuedPlayoutTrackerTest, ConvertsAlsaDelaySnapshotsIntoConsumedFrameDeltas) {
    QueuedPlayoutTracker tracker;
    tracker.reset(3);
    tracker.submit(1'920);

    const auto first = tracker.observe_queue(PlayoutQueueSnapshot{
        .generation = 3,
        .observed_at_us = 1'000'000,
        .queued_frames = 1'440,
        .error_bound_us = 50,
        .source = PlayoutClockSource::ALSA_HTIMESTAMP,
        .quality = PlayoutClockQuality::BOUNDED,
        .underrun = false,
    });
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->frames_played, 480U);
    EXPECT_EQ(first->finish_timestamp_us, 1'000'000);

    tracker.submit(960);
    const auto second = tracker.observe_queue(PlayoutQueueSnapshot{
        .generation = 3,
        .observed_at_us = 1'010'000,
        .queued_frames = 1'920,
        .error_bound_us = 50,
        .source = PlayoutClockSource::ALSA_HTIMESTAMP,
        .quality = PlayoutClockQuality::BOUNDED,
        .underrun = false,
    });
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->frames_played, 480U);
}

TEST(QueuedPlayoutTrackerTest, IgnoresStaleSnapshotsAndSurfacesUnderrun) {
    QueuedPlayoutTracker tracker;
    tracker.reset(8);
    tracker.submit(480);

    EXPECT_FALSE(tracker.observe_queue(PlayoutQueueSnapshot{
        .generation = 7,
        .observed_at_us = 20'000,
        .queued_frames = 0,
        .error_bound_us = 0,
        .source = PlayoutClockSource::DMA_COMPLETION,
        .quality = PlayoutClockQuality::EXACT,
        .underrun = false,
    }));

    const auto underrun = tracker.observe_queue(PlayoutQueueSnapshot{
        .generation = 8,
        .observed_at_us = 25'000,
        .queued_frames = 0,
        .error_bound_us = 100,
        .source = PlayoutClockSource::ALSA_HTIMESTAMP,
        .quality = PlayoutClockQuality::BOUNDED,
        .underrun = true,
    });
    ASSERT_TRUE(underrun.has_value());
    EXPECT_EQ(underrun->frames_played, 480U);
    EXPECT_TRUE(underrun->underrun);
}

}  // namespace
}  // namespace sendspin
