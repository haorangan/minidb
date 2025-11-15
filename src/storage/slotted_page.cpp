//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "../include/storage/page.hpp"
#include "../include/common/types.hpp"
#include <cstdint>

class SlottedPage {
public:
    explicit SlottedPage(Page *page) : page_(page) {}

    void Init();  // call for brand-new pages

    bool InsertTuple(const char *data, uint16_t size, RID &rid);
    bool GetTuple(const RID &rid, char *dest, uint16_t &size) const;

    bool HasSpace(uint16_t size) const;

private:
    Page *page_;

    struct Slot {
        uint16_t offset;
        uint16_t size;
        uint8_t deleted;
    };

    // header accessors
    [[nodiscard]] uint16_t &NumSlots() const;
    [[nodiscard]] uint16_t &FreeSpaceOffset() const;

    [[nodiscard]] Slot *GetSlot(uint16_t slot_id) const;
};
