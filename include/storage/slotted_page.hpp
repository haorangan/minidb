//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "storage/page.hpp"
#include "common/types.hpp"
#include <cstdint>

class SlottedPage {
public:
    explicit SlottedPage(Page *page) : page_(page) {}

    // Initialize a brand-new empty page (must be called on NewPage)
    void Init();

    // Check if there is enough space to insert a tuple of given size (in bytes)
    bool HasSpace(uint16_t tuple_size) const;

    // Insert a tuple's raw bytes; returns false if not enough space
    bool InsertTuple(const char *data, uint16_t size, RID &rid);

    // Read a tuple at RID into dest; sets size; returns false if not present/deleted
    bool GetTuple(const RID &rid, char *dest, uint16_t &size) const;

private:
    Page *page_;

    // Layout:
    // [0..1] : uint16_t num_slots
    // [2..3] : uint16_t free_space_end (start of tuple data region from bottom)
    //
    // Slot directory starts at offset HEADER_SIZE and grows upward:
    // Slot i is at: HEADER_SIZE + i * sizeof(Slot)
    //
    // Tuple data grows downward from free_space_end toward PAGE_SIZE.
    //
    // Free space is [header + slots .. free_space_end).

    struct Slot {
        uint16_t offset;   // offset INTO PAGE where tuple bytes begin
        uint16_t size;     // length in bytes
        uint8_t  deleted;  // 0 = alive, 1 = deleted
        uint8_t  pad;      // padding to make struct size 6 bytes
    };

    static constexpr uint16_t HEADER_SIZE = 4;                 // bytes
    static constexpr uint16_t SLOT_SIZE   = sizeof(Slot);      // 6 bytes

    // Header accessors (stored in the first 4 bytes of the page)
    [[nodiscard]] uint16_t &NumSlots() const;
    [[nodiscard]] uint16_t &FreeSpaceEnd() const;

    // Compute where the slot directory currently ends
    [[nodiscard]] uint16_t FreeSpaceOffset() const;

    // Get pointer to a particular slot
    [[nodiscard]] Slot *GetSlot(uint16_t slot_id) const;
};