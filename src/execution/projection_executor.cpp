//
// Created by Haoran Gan on 11/16/25.
//

#include "execution/projection_executor.hpp"

bool ProjectionExecutor::Next(Tuple &out, RID &rid) {
    Tuple t;
    if (!child_->Next(t, rid)) {
        return false;
    }
    std::vector<int64_t> vals;
    vals.reserve(columns_.size());
    for (size_t col : columns_) {
        vals.push_back(t.GetInt(col));
    }
    out = Tuple(std::move(vals));
    return true;
}
