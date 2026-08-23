// Copyright 2026 Sendspin Contributors
// SPDX-License-Identifier: Apache-2.0

#include "sendspin/playout_observation.h"

namespace sendspin {

void PlayoutTimeline::reset(uint32_t generation) {
    this->generation_ = generation;
    this->total_frames_ = 0;
    this->underrun_count_ = 0;
    this->latest_.reset();
}

bool PlayoutTimeline::observe(const PlayoutObservation& observation) {
    if (observation.generation != this->generation_ || !this->quality_is_valid(observation) ||
        (observation.frames_played == 0 && !observation.underrun)) {
        return false;
    }
    if (this->latest_.has_value() &&
        (observation.observed_at_us < this->latest_->observed_at_us ||
         observation.finish_timestamp_us < this->latest_->finish_timestamp_us)) {
        return false;
    }

    this->total_frames_ += observation.frames_played;
    if (observation.underrun) {
        ++this->underrun_count_;
    }
    this->latest_ = observation;
    return true;
}

std::optional<PlayoutObservation> PlayoutTimeline::snapshot() const {
    return this->latest_;
}

uint64_t PlayoutTimeline::total_frames() const {
    return this->total_frames_;
}

uint32_t PlayoutTimeline::underrun_count() const {
    return this->underrun_count_;
}

bool PlayoutTimeline::quality_is_valid(const PlayoutObservation& observation) const {
    if ((observation.source == PlayoutClockSource::ESTIMATED) !=
        (observation.quality == PlayoutClockQuality::ESTIMATED)) {
        return false;
    }
    switch (observation.quality) {
        case PlayoutClockQuality::EXACT:
            return observation.error_bound_us == 0;
        case PlayoutClockQuality::BOUNDED:
            return observation.error_bound_us >= 0;
        case PlayoutClockQuality::ESTIMATED:
            return observation.error_bound_us >= -1;
    }
    return false;
}

void QueuedPlayoutTracker::reset(uint32_t generation) {
    this->generation_ = generation;
    this->submitted_frames_ = 0;
    this->reported_frames_ = 0;
    this->last_observed_at_us_ = 0;
    this->has_observation_ = false;
}

void QueuedPlayoutTracker::submit(uint32_t frames) {
    this->submitted_frames_ += frames;
}

std::optional<PlayoutObservation> QueuedPlayoutTracker::observe_queue(
    const PlayoutQueueSnapshot& snapshot) {
    if (snapshot.generation != this->generation_ ||
        (this->has_observation_ && snapshot.observed_at_us < this->last_observed_at_us_)) {
        return std::nullopt;
    }

    const uint64_t queued_frames =
        snapshot.queued_frames > this->submitted_frames_ ? this->submitted_frames_
                                                        : snapshot.queued_frames;
    const uint64_t consumed_frames = this->submitted_frames_ - queued_frames;
    if (consumed_frames < this->reported_frames_) {
        return std::nullopt;
    }
    const uint64_t delta = consumed_frames - this->reported_frames_;
    if (delta == 0 && !snapshot.underrun) {
        return std::nullopt;
    }
    if (delta > UINT32_MAX) {
        return std::nullopt;
    }

    PlayoutObservation observation{
        .generation = snapshot.generation,
        .frames_played = static_cast<uint32_t>(delta),
        .observed_at_us = snapshot.observed_at_us,
        .finish_timestamp_us = snapshot.observed_at_us,
        .error_bound_us = snapshot.error_bound_us,
        .source = snapshot.source,
        .quality = snapshot.quality,
        .underrun = snapshot.underrun,
    };
    PlayoutTimeline validator;
    validator.reset(snapshot.generation);
    if (!validator.observe(observation)) {
        return std::nullopt;
    }

    this->reported_frames_ = consumed_frames;
    this->last_observed_at_us_ = snapshot.observed_at_us;
    this->has_observation_ = true;
    return observation;
}

}  // namespace sendspin
