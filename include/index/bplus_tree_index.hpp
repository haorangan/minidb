//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "storage/buffer_pool_manager.hpp"
#include "storage/page.hpp"
#include "common/types.hpp"

#include <cstdint>
#include <vector>

class BPlusTreeIndex {
public:
    // Construct an index. Call Init() once to create the root leaf.
    explicit BPlusTreeIndex(BufferPoolManager *bpm);

    // Allocate and initialize root leaf page (call once when creating index)
    void Init();

    // Insert a single (key, rid) into the index.
    // Returns false if there is no space in the leaf (for this simple version).
    bool Insert(int32_t key, const RID &rid);

    // Find all RIDs with key == search_key
    void SearchEqual(int32_t search_key, std::vector<RID> &result);

    // Find all RIDs with low_key <= key <= high_key
    void SearchRange(int32_t low_key, int32_t high_key, std::vector<RID> &result);

    page_id_t GetRootPageId() const { return root_page_id_; }

private:
    BufferPoolManager *bpm_;
    page_id_t root_page_id_{-1};

    // ---- On-page layout for a single leaf node ----
    //
    // [0]    uint8_t  is_leaf      (always 1 for this simple index)
    // [1]    uint8_t  reserved
    // [2..3] uint16_t num_keys
    //
    // Entries array starts at offset HEADER_SIZE and grows upward:
    //
    // struct Entry {
    //   int32_t  key;
    //   page_id_t page_id;
    //   uint16_t slot_id;
    //   uint16_t pad;   // padding to keep struct size aligned
    // };
    //
    // Max entries chosen so everything fits in one 4KB page.

    struct Entry {
        int32_t   key;
        page_id_t page_id;
        uint16_t  slot_id;
        uint16_t  pad;     // unused, just for alignment
    };

    static constexpr uint16_t HEADER_SIZE = 4;            // bytes
    static constexpr uint16_t ENTRY_SIZE  = sizeof(Entry);
    static constexpr uint16_t MAX_ENTRIES =
        (PAGE_SIZE - HEADER_SIZE) / ENTRY_SIZE;          // ~341 entries

    // Helper accessors into page->GetData()
    void InitLeafPage(Page *page);
    uint16_t &NumKeys(Page *page) const;
    uint8_t  &IsLeaf(Page *page) const;
    Entry    *GetEntry(Page *page, uint16_t index) const;

    // Binary search helpers
    int FindLowerBound(Page *page, int32_t key) const;
};
