//
// Created by Haoran Gan on 11/14/25.
//
#include "storage/slotted_page.hpp"

#include <cstring>  // std::memcpy, std::memset
#include <stdexcept>

// ---- Header field helpers ----

// num_slots is stored at offset 0..1
uint16_t &SlottedPage::NumSlots() const {
    auto *data = const_cast<char *>(page_->GetData());
    return *reinterpret_cast<uint16_t *>(data + 0);
}

// free_space_end is stored at offset 2..3
uint16_t &SlottedPage::FreeSpaceEndRef() const {
    auto *data = const_cast<char *>(page_->GetData());
    return *reinterpret_cast<uint16_t *>(data + 2);
}

// Slot directory occupies [HEADER_SIZE .. HEADER_SIZE + NumSlots()*sizeof(Slot))
uint16_t SlottedPage::FreeSpaceOffsetInternal() const {
    return static_cast<uint16_t>(HEADER_SIZE + NumSlots() * SLOT_SIZE);
}

// Slot i lives immediately after the header, in the directory
SlottedPage::Slot *SlottedPage::GetSlot(uint16_t slot_id) const {
    if (slot_id >= NumSlots()) {
        return nullptr;
    }
    auto *data = const_cast<char *>(page_->GetData());
    auto *slot_base = data + HEADER_SIZE + slot_id * SLOT_SIZE;
    return reinterpret_cast<Slot *>(slot_base);
}

// ---- Public methods ----

void SlottedPage::Init() {
    // Clear entire page to zeros
    page_->ResetMemory();

    // Initialize header
    NumSlots() = 0;
    FreeSpaceEndRef() = PAGE_SIZE;  // tuple data starts at the very end (no tuples yet)
}

// Free space is the gap between end of slot directory and FreeSpaceEnd
bool SlottedPage::HasSpace(uint16_t tuple_size) const {
    uint16_t dir_end = FreeSpaceOffsetInternal();
    uint16_t free_end = FreeSpaceEndRef();

    // Need room for both the new slot metadata and the tuple bytes
    uint32_t needed = static_cast<uint32_t>(SLOT_SIZE) + tuple_size;

    if (free_end < dir_end) {
        // Corrupt or no space
        return false;
    }

    auto free_space = static_cast<uint32_t>(free_end - dir_end);
    return free_space >= needed;
}

bool SlottedPage::InsertTuple(const char *data, uint16_t size, RID &rid) {
    if (!HasSpace(size)) {
        return false;
    }

    uint16_t dir_end = FreeSpaceOffsetInternal();
    uint16_t free_end = FreeSpaceEndRef();

    // Place tuple bytes at the end of free space: grow data downward
    auto tuple_offset = static_cast<uint16_t>(free_end - size);

    // Copy tuple bytes into the page
    char *page_data = page_->GetData();
    std::memcpy(page_data + tuple_offset, data, size);

    // Create new slot in the directory at the end
    uint16_t slot_id = NumSlots();
    Slot *slot = reinterpret_cast<Slot *>(page_data + dir_end);

    slot->offset  = tuple_offset;
    slot->size    = size;
    slot->deleted = 0;
    slot->pad     = 0;

    // Update header
    NumSlots() = static_cast<uint16_t>(NumSlots() + 1);
    FreeSpaceEndRef() = tuple_offset;

    // Return record ID
    rid.page_id = page_->GetPageId();
    rid.slot_id = slot_id;

    return true;
}

bool SlottedPage::GetTuple(const RID &rid, char *dest, uint16_t &size) const {
    if (!rid.IsValid()) {
        return false;
    }

    if (rid.slot_id >= NumSlots()) {
        return false;
    }

    Slot *slot = GetSlot(rid.slot_id);
    if (slot == nullptr) {
        return false;
    }

    if (slot->deleted != 0) {
        return false;  // logically deleted
    }

    uint16_t tuple_offset = slot->offset;
    uint16_t tuple_size   = slot->size;

    if (tuple_offset + tuple_size > PAGE_SIZE) {
        // Basic sanity check for corruption
        return false;
    }

    char *page_data = page_->GetData();
    std::memcpy(dest, page_data + tuple_offset, tuple_size);
    size = tuple_size;
    return true;
}

// ---- Debug / inspection helpers ----

uint16_t SlottedPage::GetNumSlots() const {
    return NumSlots();
}

uint16_t SlottedPage::GetFreeSpaceOffset() const {
    return FreeSpaceOffsetInternal();
}

uint16_t SlottedPage::GetFreeSpaceEnd() const {
    return FreeSpaceEndRef();
}
