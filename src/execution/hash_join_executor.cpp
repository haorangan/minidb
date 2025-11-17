//
// Created by Haoran Gan on 11/16/25.
//

#include "execution/hash_join_executor.hpp"

#include <tuple>

void HashJoinExecutor::Init() {
    left_->Init();
    built_ = false;  // the right side may have changed; rebuild on next use
}

void HashJoinExecutor::Build() {
    table_.clear();
    right_->Init();

    Tuple t;
    RID rid;
    while (right_->Next(t, rid)) {
        int64_t key = t.GetInt(right_key_);
        table_.emplace(key, t);
    }

    match_it_ = match_end_ = table_.end();
    built_ = true;
}

bool HashJoinExecutor::Next(Tuple &out, RID &rid) {
    if (!built_) {
        Build();
    }

    RID ignored;
    while (match_it_ == match_end_) {
        if (!left_->Next(left_tuple_, ignored)) {
            return false;
        }
        std::tie(match_it_, match_end_) = table_.equal_range(left_tuple_.GetInt(left_key_));
    }

    out = Tuple::Concat(left_tuple_, match_it_->second);
    rid = RID();
    ++match_it_;
    return true;
}
