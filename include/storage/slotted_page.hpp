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

    // Largest tuple that fits on an empty page (header + one slot + data)
    static constexpr uint16_t MAX_TUPLE_SIZE = PAGE_SIZE - 8 - 6;

    // Initialize a brand-new empty page (must be called on NewPage)
    void Init();

    // Check if there is enough space to insert a tuple of given size (in bytes)
    [[nodiscard]] bool HasSpace(uint16_t tuple_size) const;

    // Insert a tuple's raw bytes; returns false if not enough space
    bool InsertTuple(const char *data, uint16_t size, RID &rid);

    // Read a tuple at RID into dest; sets size; returns false if not present/deleted
    bool GetTuple(const RID &rid, char *dest, uint16_t &size) const;

    // Heap pages of a table form a singly linked list (-1 = last page)
    [[nodiscard]] page_id_t GetNextPageId() const;
    void SetNextPageId(page_id_t next);

    // ---- Debug / inspection helpers ----
    [[nodiscard]] uint16_t GetNumSlots() const;
    [[nodiscard]] uint16_t GetFreeSpaceOffset() const;  // where slot directory ends
    [[nodiscard]] uint16_t GetFreeSpaceEnd() const;     // where tuple data begins (from bottom)

private:
    Page *page_;

    // Layout:
    // [0..1] : uint16_t num_slots
    // [2..3] : uint16_t free_space_end (start of tuple data region from bottom)
    // [4..7] : page_id_t next_page_id  (next heap page of this table, -1 = none)
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

    static constexpr uint16_t HEADER_SIZE = 8;                 // bytes
    static constexpr uint16_t SLOT_SIZE   = sizeof(Slot);      // 6 bytes
    static_assert(MAX_TUPLE_SIZE == PAGE_SIZE - HEADER_SIZE - SLOT_SIZE);

    // Header accessors (stored in the first 8 bytes of the page)
    [[nodiscard]] uint16_t &NumSlots() const;
    [[nodiscard]] uint16_t &FreeSpaceEndRef() const;
    [[nodiscard]] page_id_t &NextPageRef() const;

    // Compute where the slot directory currently ends
    [[nodiscard]] uint16_t FreeSpaceOffsetInternal() const;

    // Get pointer to a particular slot
    [[nodiscard]] Slot *GetSlot(uint16_t slot_id) const;
};