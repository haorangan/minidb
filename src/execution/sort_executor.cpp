//
// Created by Haoran Gan on 11/16/25.
//

#include "execution/sort_executor.hpp"

#include <algorithm>

void SortExecutor::Init() {
    child_->Init();
    sorted_ = false;
}

bool SortExecutor::Next(Tuple &out, RID &rid) {
    if (!sorted_) {
        rows_.clear();
        Tuple t;
        RID r;
        while (child_->Next(t, r)) {
            rows_.emplace_back(t, r);
        }

        std::stable_sort(rows_.begin(), rows_.end(),
                         [this](const auto &lhs, const auto &rhs) {
                             for (const SortKey &k : keys_) {
                                 int64_t lv = lhs.first.GetInt(k.column);
                                 int64_t rv = rhs.first.GetInt(k.column);
                                 if (lv != rv) return k.ascending ? lv < rv : lv > rv;
                             }
                             return false;
                         });
        row_idx_ = 0;
        sorted_ = true;
    }

    if (row_idx_ >= rows_.size()) {
        return false;
    }
    out = rows_[row_idx_].first;
    rid = rows_[row_idx_].second;
    ++row_idx_;
    return true;
}
