#pragma once
#include "plate_algorithms.h"
#include <deque>
#include <map>
#include <cstdint>

namespace plate {
struct ConsensusConfig {
    std::size_t window = 5, votes = 3;
    std::int64_t target_timeout_ms = 3000, max_history_ms = 8000;
    float association_iou = .3f;
};
struct ConsensusState {
    std::string raw, confirmed;
    std::size_t support = 0, observations = 0;
    bool active = false;
};
// One selected target only. UI deliberately refuses multi-candidate voting.
// Call expire() on an independent timer, including while inference is busy.
class Consensus {
public:
    explicit Consensus(ConsensusConfig config = {}) : config_(config) {
        if (!config.window || !config.votes || config.votes > config.window
            || config.votes <= config.window / 2 || config.target_timeout_ms <= 0
            || config.max_history_ms <= 0 || !std::isfinite(config.association_iou)
            || config.association_iou < 0 || config.association_iou > 1)
            throw std::runtime_error("Invalid consensus configuration");
    }
    void reset() { history_.clear(); state_ = {}; last_seen_ = -1; }
    bool expire(std::int64_t now) {
        check_time(now);
        if (last_seen_ >= 0 && now-last_seen_ >= config_.target_timeout_ms) {
            reset(); return true;
        }
        prune(now);
        state_.observations = history_.size();
        state_.support = 0;
        for (const auto& sample : history_) if (sample.text == state_.raw) ++state_.support;
        if (state_.support < config_.votes) state_.confirmed.clear();
        return false;
    }
    ConsensusState observe(const Detection& box, const std::string& raw, bool valid,
                           std::int64_t now) {
        expire(now);
        for (float v : box.box) if (!std::isfinite(v)) throw std::runtime_error("Invalid track box");
        if (box.box[2] <= box.box[0] || box.box[3] <= box.box[1])
            throw std::runtime_error("Empty track box");
        if (state_.active && overlap(last_box_, box) < config_.association_iou) reset();
        last_seen_ = now; last_box_ = box; state_.active = true;
        state_.raw = raw; state_.confirmed.clear(); state_.support = 0;
        if (valid && !raw.empty()) history_.push_back({now, raw});
        prune(now);
        while (history_.size() > config_.window) history_.pop_front();
        std::map<std::string, std::size_t> counts;
        for (const auto& item : history_) ++counts[item.text];
        state_.observations = history_.size();
        state_.support = counts[raw];
        // A conflicting latest observation returns to review instead of latching old text.
        if (valid && state_.support >= config_.votes) state_.confirmed = raw;
        return state_;
    }
    const ConsensusState& state() const { return state_; }
private:
    struct Sample { std::int64_t time; std::string text; };
    void check_time(std::int64_t now) {
        if (now < 0 || (last_clock_ >= 0 && now < last_clock_))
            throw std::runtime_error("Consensus requires a monotonic nonnegative clock");
        last_clock_ = now;
    }
    void prune(std::int64_t now) {
        while (!history_.empty() && now-history_.front().time >= config_.max_history_ms)
            history_.pop_front();
    }
    ConsensusConfig config_;
    ConsensusState state_;
    Detection last_box_;
    std::deque<Sample> history_;
    std::int64_t last_seen_ = -1, last_clock_ = -1;
};
} // namespace plate
