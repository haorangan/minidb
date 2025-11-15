//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include <cstdint>

static constexpr uint32_t PAGE_SIZE = 4096;

using page_id_t = int32_t;

struct RID {
    page_id_t page_id;
    uint16_t slot_id;

    RID(page_id_t pid = -1, uint16_t sid = 0) : page_id(pid), slot_id(sid) {}
    bool IsValid() const { return page_id >= 0; }
};