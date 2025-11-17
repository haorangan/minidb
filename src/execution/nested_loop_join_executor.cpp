//
// Created by Haoran Gan on 11/16/25.
//

#include "execution/nested_loop_join_executor.hpp"

void NestedLoopJoinExecutor::Init() {
    left_->Init();
    has_left_ = false;
}

bool NestedLoopJoinExecutor::Next(Tuple &out, RID &rid) {
    RID ignored;
    while (true) {
        if (!has_left_) {
            if (!left_->Next(left_tuple_, ignored)) {
                return false;
            }
            has_left_ = true;
            right_->Init();  // rescan the inner side for this outer row
        }

        Tuple right_tuple;
        while (right_->Next(right_tuple, ignored)) {
            Tuple joined = Tuple::Concat(left_tuple_, right_tuple);
            if (pred_(joined)) {
                out = std::move(joined);
                rid = RID();
                return true;
            }
        }
        has_left_ = false;  // inner side exhausted; advance the outer side
    }
}
