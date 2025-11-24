//
// Created by Haoran Gan on 11/14/25.
//

#pragma once

#include "storage/buffer_pool_manager.hpp"
#include "storage/page.hpp"
#include "common/types.hpp"

#include <cstdint>
#include <vector>

// Disk-backed B+ tree mapping int32 keys to RIDs. Duplicate keys are allowed
// (the index is on a non-unique column). Every node is one buffer-pool page.
class BPlusTreeIndex {
public:
    // leaf_max / internal_max cap the number of keys per node. 0 means "as many
    // as fit in a page"; small values are useful in tests to force deep trees.
    explicit BPlusTreeIndex(BufferPoolManager *bpm,
                            uint16_t leaf_max = 0,
                            uint16_t internal_max = 0);

    // Allocate and initialize the root leaf page (call once when creating index)
    void Init();

    // Attach to an existing tree. The root page id never changes after Init().
    void Open(page_id_t root_page_id);

    // Insert a single (key, rid) into the index, splitting nodes as needed.
    bool Insert(int32_t key, const RID &rid);

    // Find all RIDs with key == search_key
    void SearchEqual(int32_t search_key, std::vector<RID> &result);

    // Find all RIDs with low_key <= key <= high_key
    void SearchRange(int32_t low_key, int32_t high_key, std::vector<RID> &result);

    page_id_t GetRootPageId() const { return root_page_id_; }

    // Number of levels (1 = root is a leaf)
    int GetHeight();

    // Check structural invariants (sorted nodes, separator bounds, uniform leaf
    // depth, sorted leaf chain). Writes the total entry count if non-null.
    bool Validate(size_t *num_entries = nullptr);

private:
    BufferPoolManager *bpm_;
    page_id_t root_page_id_{-1};
    uint16_t leaf_max_;
    uint16_t internal_max_;

    // ---- On-page layout ----
    //
    // Common header (8 bytes):
    // [0]    uint8_t   is_leaf
    // [1]    uint8_t   reserved
    // [2..3] uint16_t  num_keys
    // [4..7] page_id_t next_leaf  (leaves only; -1 = last leaf)
    //
    // Leaf:     Entry[num_keys] starting at HEADER_SIZE, sorted by key.
    //
    // Internal: page_id_t children[INTERNAL_CAP + 1] starting at HEADER_SIZE,
    //           then int32_t keys[INTERNAL_CAP]. Node with n keys has n+1
    //           children; child i holds keys k with keys[i-1] <= k <= keys[i].
    //           (Both ends inclusive because duplicates can straddle a split.)

    struct Entry {
        int32_t   key;
        page_id_t page_id;
        uint16_t  slot_id;
        uint16_t  pad;     // unused, just for alignment
    };

    struct SplitResult {
        bool split = false;
        int32_t separator = 0;     // first key of the new right node
        page_id_t right = -1;      // newly allocated right sibling
    };

    static constexpr uint16_t HEADER_SIZE  = 8;
    static constexpr uint16_t ENTRY_SIZE   = sizeof(Entry);
    static constexpr uint16_t LEAF_CAP     = (PAGE_SIZE - HEADER_SIZE) / ENTRY_SIZE;  // 340
    static constexpr uint16_t INTERNAL_CAP =
        (PAGE_SIZE - HEADER_SIZE - sizeof(page_id_t)) / (sizeof(int32_t) + sizeof(page_id_t));  // 510

    // Header accessors
    static uint8_t   &IsLeaf(Page *page);
    static uint16_t  &NumKeys(Page *page);
    static page_id_t &NextLeaf(Page *page);

    // Node accessors
    static Entry     *LeafEntry(Page *page, uint16_t index);
    static page_id_t *Children(Page *page);
    static int32_t   *Keys(Page *page);

    static void InitLeafPage(Page *page);
    static void InitInternalPage(Page *page);

    // Binary search helpers over a node's keys
    static int LeafLowerBound(Page *page, int32_t key);
    static int LeafUpperBound(Page *page, int32_t key);
    static int InternalLowerBound(Page *page, int32_t key);
    static int InternalUpperBound(Page *page, int32_t key);

    // Insert helpers. On split, `out` describes the new right sibling that the
    // caller must link into the parent.
    bool InsertRecursive(page_id_t pid, int32_t key, const RID &rid, SplitResult &out);
    bool InsertIntoLeaf(Page *leaf, int32_t key, const RID &rid, SplitResult &out);
    bool InsertIntoInternal(Page *node, int child_idx, int32_t separator,
                            page_id_t right_child, SplitResult &out);

    // Descend to the leftmost leaf that may contain `key`. Returns -1 on error.
    page_id_t FindLeaf(int32_t key);

    bool ValidateNode(page_id_t pid, int depth, int &leaf_depth,
                      bool has_lo, int32_t lo, bool has_hi, int32_t hi);
};
