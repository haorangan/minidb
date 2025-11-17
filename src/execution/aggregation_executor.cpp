//
// Created by Haoran Gan on 11/16/25.
//

#include "execution/aggregation_executor.hpp"

#include <algorithm>

size_t AggregationExecutor::KeyHash::operator()(const std::vector<int64_t> &key) const {
    size_t h = 0;
    for (int64_t v : key) {
        h ^= std::hash<int64_t>{}(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    return h;
}

void AggregationExecutor::Init() {
    child_->Init();
    computed_ = false;
}

int64_t AggregationExecutor::Finalize(const AggregateSpec &spec, const AggState &state) const {
    switch (spec.type) {
        case AggType::COUNT: return state.count;
        case AggType::SUM:   return state.sum;
        case AggType::MIN:   return state.min;
        case AggType::MAX:   return state.max;
        case AggType::AVG:   return state.count == 0 ? 0 : state.sum / state.count;  // integer average
    }
    return 0;
}

void AggregationExecutor::Compute() {
    std::unordered_map<std::vector<int64_t>, std::vector<AggState>, KeyHash> groups;

    Tuple t;
    RID rid;
    std::vector<int64_t> key;
    while (child_->Next(t, rid)) {
        key.clear();
        for (size_t col : group_by_) key.push_back(t.GetInt(col));

        auto [it, inserted] = groups.try_emplace(key, aggregates_.size());
        std::vector<AggState> &states = it->second;

        for (size_t i = 0; i < aggregates_.size(); ++i) {
            AggState &s = states[i];
            int64_t v = aggregates_[i].column >= 0 ? t.GetInt(static_cast<size_t>(aggregates_[i].column)) : 0;
            if (s.count == 0) {
                s.min = s.max = v;
            } else {
                s.min = std::min(s.min, v);
                s.max = std::max(s.max, v);
            }
            s.sum += v;
            ++s.count;
        }
    }

    // A global aggregate (no GROUP BY) always produces one row
    if (groups.empty() && group_by_.empty()) {
        groups.try_emplace(std::vector<int64_t>{}, aggregates_.size());
    }

    results_.clear();
    results_.reserve(groups.size());
    for (const auto &[group_key, states] : groups) {
        std::vector<int64_t> row = group_key;
        for (size_t i = 0; i < aggregates_.size(); ++i) {
            row.push_back(Finalize(aggregates_[i], states[i]));
        }
        results_.emplace_back(std::move(row));
    }

    result_idx_ = 0;
    computed_ = true;
}

bool AggregationExecutor::Next(Tuple &out, RID &rid) {
    if (!computed_) {
        Compute();
    }
    if (result_idx_ >= results_.size()) {
        return false;
    }
    out = results_[result_idx_++];
    rid = RID();
    return true;
}
