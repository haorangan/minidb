#include "execution/filter_executor.hpp"

bool FilterExecutor::Next(Tuple &out, RID &rid) {
    Tuple tmp;
    RID r;

    while (child_->Next(tmp, r)) {
        if (pred_(tmp)) {
            out = tmp;
            rid = r;
            return true;
        }
    }
    return false;
}