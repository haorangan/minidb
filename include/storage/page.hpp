//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "common/types.hpp"
#include <atomic>
#include <cstring>

class Page {
public:
    Page() { ResetMemory(); }

    char *GetData() { return data_; }
    [[nodiscard]] const char *GetData() const { return data_; }

    [[nodiscard]] page_id_t GetPageId() const { return page_id_; }
    void SetPageId(page_id_t pid) { page_id_ = pid; }

    [[nodiscard]] int GetPinCount() const { return pin_count_; }
    void Pin() { pin_count_++; }
    void Unpin() { pin_count_--; }

    [[nodiscard]] bool IsDirty() const { return is_dirty_; }
    void SetDirty(const bool dirty) { is_dirty_ = dirty; }

    void ResetMemory() { std::memset(data_, 0, PAGE_SIZE); }

private:
    page_id_t page_id_{-1};
    int pin_count_{0};
    bool is_dirty_{false};
    char data_[PAGE_SIZE];
};
