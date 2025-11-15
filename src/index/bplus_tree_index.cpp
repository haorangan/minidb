//
// Created by Haoran Gan on 11/14/25.
//

#include "index/bplus_tree_index.hpp"

#include <cstring>

// ---- Helper field accessors ----

// num_keys stored at offset 2..3
uint16_t &BPlusTreeIndex::NumKeys(Page *page) const {
    auto *data = const_cast<char *>(page->GetData());
    return *reinterpret_cast<uint16_t *>(data + 2);
}

// is_leaf stored at offset 0
uint8_t &BPlusTreeIndex::IsLeaf(Page *page) const {
    auto *data = const_cast<char *>(page->GetData());
    return *reinterpret_cast<uint8_t *>(data + 0);
}

// Entry i lives at HEADER_SIZE + i * ENTRY_SIZE
BPlusTreeIndex::Entry *BPlusTreeIndex::GetEntry(Page *page, uint16_t index) const {
    auto *data = const_cast<char *>(page->GetData());
    return reinterpret_cast<Entry *>(data + HEADER_SIZE + index * ENTRY_SIZE);
}

void BPlusTreeIndex::InitLeafPage(Page *page) {
    // Zero the page
    page->ResetMemory();
    IsLeaf(page) = 1;
    NumKeys(page) = 0;
}

// Binary search: first index i where key_i >= key, or num_keys if none
int BPlusTreeIndex::FindLowerBound(Page *page, int32_t key) const {
    uint16_t n = NumKeys(page);
    int lo = 0;
    int hi = n; // half-open [lo, hi)

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        Entry *e = GetEntry(page, static_cast<uint16_t>(mid));
        if (e->key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo; // in [0, n]
}

// ---- Public interface ----

BPlusTreeIndex::BPlusTreeIndex(BufferPoolManager *bpm)
    : bpm_(bpm) {}

void BPlusTreeIndex::Init() {
    if (bpm_ == nullptr) {
        return;
    }

    page_id_t pid;
    Page *page = bpm_->NewPage(pid);
    if (!page) {
        return;
    }

    InitLeafPage(page);
    root_page_id_ = pid;

    bpm_->UnpinPage(pid, /*is_dirty=*/true);
}

bool BPlusTreeIndex::Insert(int32_t key, const RID &rid) {
    if (root_page_id_ == -1) {
        // index not initialized
        return false;
    }

    Page *page = bpm_->FetchPage(root_page_id_);
    if (!page) {
        return false;
    }

    if (IsLeaf(page) != 1) {
        // in this simple version, root must be a leaf
        bpm_->UnpinPage(root_page_id_, /*is_dirty=*/false);
        return false;
    }

    uint16_t &n = NumKeys(page);
    if (n >= MAX_ENTRIES) {
        // No room in this simple single-leaf index
        bpm_->UnpinPage(root_page_id_, /*is_dirty=*/false);
        return false;
    }

    // Find insertion position (keep keys sorted)
    int pos = FindLowerBound(page, key);

    // Shift entries [pos..n-1] one step to the right
    for (int i = n - 1; i >= pos; --i) {
        Entry *src = GetEntry(page, static_cast<uint16_t>(i));
        Entry *dst = GetEntry(page, static_cast<uint16_t>(i + 1));
        *dst = *src;
    }

    // Insert new entry at position pos
    Entry *e = GetEntry(page, static_cast<uint16_t>(pos));
    e->key = key;
    e->page_id = rid.page_id;
    e->slot_id = rid.slot_id;
    e->pad = 0;

    n = static_cast<uint16_t>(n + 1);

    bpm_->UnpinPage(root_page_id_, /*is_dirty=*/true);
    return true;
}

void BPlusTreeIndex::SearchEqual(int32_t search_key, std::vector<RID> &result) {
    result.clear();
    if (root_page_id_ == -1) {
        return;
    }

    Page *page = bpm_->FetchPage(root_page_id_);
    if (!page) {
        return;
    }

    if (IsLeaf(page) != 1) {
        bpm_->UnpinPage(root_page_id_, /*is_dirty=*/false);
        return;
    }

    uint16_t n = NumKeys(page);
    int pos = FindLowerBound(page, search_key);

    // Walk forward while key == search_key
    for (int i = pos; i < n; ++i) {
        Entry *e = GetEntry(page, static_cast<uint16_t>(i));
        if (e->key != search_key) {
            break;
        }
        result.emplace_back(e->page_id, e->slot_id);
    }

    bpm_->UnpinPage(root_page_id_, /*is_dirty=*/false);
}

void BPlusTreeIndex::SearchRange(int32_t low_key, int32_t high_key,
                                 std::vector<RID> &result) {
    result.clear();
    if (root_page_id_ == -1) {
        return;
    }

    Page *page = bpm_->FetchPage(root_page_id_);
    if (!page) {
        return;
    }

    if (IsLeaf(page) != 1) {
        bpm_->UnpinPage(root_page_id_, /*is_dirty=*/false);
        return;
    }

    uint16_t n = NumKeys(page);
    int pos = FindLowerBound(page, low_key);

    for (int i = pos; i < n; ++i) {
        Entry *e = GetEntry(page, static_cast<uint16_t>(i));
        if (e->key > high_key) {
            break;
        }
        result.emplace_back(e->page_id, e->slot_id);
    }

    bpm_->UnpinPage(root_page_id_, /*is_dirty=*/false);
}
