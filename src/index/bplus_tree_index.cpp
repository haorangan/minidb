//
// Created by Haoran Gan on 11/14/25.
//

#include "index/bplus_tree_index.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_set>

// ---- Header field accessors ----

uint8_t &BPlusTreeIndex::IsLeaf(Page *page) {
    return *reinterpret_cast<uint8_t *>(page->GetData() + 0);
}

uint16_t &BPlusTreeIndex::NumKeys(Page *page) {
    return *reinterpret_cast<uint16_t *>(page->GetData() + 2);
}

page_id_t &BPlusTreeIndex::NextLeaf(Page *page) {
    return *reinterpret_cast<page_id_t *>(page->GetData() + 4);
}

// ---- Node accessors ----

BPlusTreeIndex::Entry *BPlusTreeIndex::LeafEntry(Page *page, uint16_t index) {
    return reinterpret_cast<Entry *>(page->GetData() + HEADER_SIZE + index * ENTRY_SIZE);
}

page_id_t *BPlusTreeIndex::Children(Page *page) {
    return reinterpret_cast<page_id_t *>(page->GetData() + HEADER_SIZE);
}

int32_t *BPlusTreeIndex::Keys(Page *page) {
    return reinterpret_cast<int32_t *>(page->GetData() + HEADER_SIZE +
                                       (INTERNAL_CAP + 1) * sizeof(page_id_t));
}

void BPlusTreeIndex::InitLeafPage(Page *page) {
    page->ResetMemory();
    IsLeaf(page) = 1;
    NumKeys(page) = 0;
    NextLeaf(page) = -1;
}

void BPlusTreeIndex::InitInternalPage(Page *page) {
    page->ResetMemory();
    IsLeaf(page) = 0;
    NumKeys(page) = 0;
    NextLeaf(page) = -1;
}

// ---- Binary search helpers ----

// First index i where entry_i.key >= key, or num_keys if none
int BPlusTreeIndex::LeafLowerBound(Page *page, int32_t key) {
    Entry *begin = LeafEntry(page, 0);
    Entry *end = begin + NumKeys(page);
    return static_cast<int>(std::lower_bound(begin, end, key,
        [](const Entry &e, int32_t k) { return e.key < k; }) - begin);
}

// First index i where entry_i.key > key, or num_keys if none
int BPlusTreeIndex::LeafUpperBound(Page *page, int32_t key) {
    Entry *begin = LeafEntry(page, 0);
    Entry *end = begin + NumKeys(page);
    return static_cast<int>(std::upper_bound(begin, end, key,
        [](int32_t k, const Entry &e) { return k < e.key; }) - begin);
}

int BPlusTreeIndex::InternalLowerBound(Page *page, int32_t key) {
    int32_t *keys = Keys(page);
    return static_cast<int>(std::lower_bound(keys, keys + NumKeys(page), key) - keys);
}

int BPlusTreeIndex::InternalUpperBound(Page *page, int32_t key) {
    int32_t *keys = Keys(page);
    return static_cast<int>(std::upper_bound(keys, keys + NumKeys(page), key) - keys);
}

// ---- Public interface ----

BPlusTreeIndex::BPlusTreeIndex(BufferPoolManager *bpm, uint16_t leaf_max, uint16_t internal_max)
    : bpm_(bpm),
      leaf_max_(leaf_max == 0 ? LEAF_CAP : std::clamp<uint16_t>(leaf_max, 3, LEAF_CAP)),
      internal_max_(internal_max == 0 ? INTERNAL_CAP
                                      : std::clamp<uint16_t>(internal_max, 3, INTERNAL_CAP)) {}

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

void BPlusTreeIndex::Open(page_id_t root_page_id) {
    root_page_id_ = root_page_id;
}

bool BPlusTreeIndex::Insert(int32_t key, const RID &rid) {
    if (root_page_id_ == -1) {
        // index not initialized
        return false;
    }

    SplitResult split;
    if (!InsertRecursive(root_page_id_, key, rid, split)) {
        return false;
    }
    if (!split.split) {
        return true;
    }

    // Root split: grow the tree by one level. The root keeps its page id (the
    // catalog stores it), so its left half moves to a new page instead.
    page_id_t moved_pid;
    Page *moved = bpm_->NewPage(moved_pid);
    if (!moved) {
        return false;
    }
    Page *root = bpm_->FetchPage(root_page_id_);
    if (!root) {
        bpm_->UnpinPage(moved_pid, /*is_dirty=*/false);
        return false;
    }
    std::memcpy(moved->GetData(), root->GetData(), PAGE_SIZE);

    InitInternalPage(root);
    NumKeys(root) = 1;
    Children(root)[0] = moved_pid;
    Children(root)[1] = split.right;
    Keys(root)[0] = split.separator;

    bpm_->UnpinPage(moved_pid, /*is_dirty=*/true);
    bpm_->UnpinPage(root_page_id_, /*is_dirty=*/true);
    return true;
}

// Only one page per level is pinned at a time: we unpin a node before
// descending and re-fetch it only if the child split.
bool BPlusTreeIndex::InsertRecursive(page_id_t pid, int32_t key, const RID &rid,
                                     SplitResult &out) {
    Page *page = bpm_->FetchPage(pid);
    if (!page) {
        return false;
    }

    if (IsLeaf(page)) {
        bool ok = InsertIntoLeaf(page, key, rid, out);
        bpm_->UnpinPage(pid, /*is_dirty=*/ok);
        return ok;
    }

    // Equal keys go right, so new duplicates land after existing ones
    int child_idx = InternalUpperBound(page, key);
    page_id_t child = Children(page)[child_idx];
    bpm_->UnpinPage(pid, /*is_dirty=*/false);

    SplitResult child_split;
    if (!InsertRecursive(child, key, rid, child_split)) {
        return false;
    }
    if (!child_split.split) {
        return true;
    }

    page = bpm_->FetchPage(pid);
    if (!page) {
        return false;
    }
    bool ok = InsertIntoInternal(page, child_idx, child_split.separator, child_split.right, out);
    bpm_->UnpinPage(pid, /*is_dirty=*/ok);
    return ok;
}

bool BPlusTreeIndex::InsertIntoLeaf(Page *leaf, int32_t key, const RID &rid, SplitResult &out) {
    uint16_t n = NumKeys(leaf);
    int pos = LeafUpperBound(leaf, key);
    Entry new_entry{key, rid.page_id, rid.slot_id, 0};

    if (n < leaf_max_) {
        // Shift entries [pos..n-1] one step to the right
        std::memmove(LeafEntry(leaf, static_cast<uint16_t>(pos + 1)),
                     LeafEntry(leaf, static_cast<uint16_t>(pos)),
                     (n - pos) * ENTRY_SIZE);
        *LeafEntry(leaf, static_cast<uint16_t>(pos)) = new_entry;
        NumKeys(leaf) = static_cast<uint16_t>(n + 1);
        return true;
    }

    // Full: gather n+1 entries in order, keep the left half, move the right half
    std::vector<Entry> entries(LeafEntry(leaf, 0), LeafEntry(leaf, n));
    entries.insert(entries.begin() + pos, new_entry);

    page_id_t right_pid;
    Page *right = bpm_->NewPage(right_pid);
    if (!right) {
        return false;
    }
    InitLeafPage(right);

    const auto total = static_cast<uint16_t>(entries.size());
    const auto left_n = static_cast<uint16_t>(total / 2);
    const auto right_n = static_cast<uint16_t>(total - left_n);

    std::memcpy(LeafEntry(leaf, 0), entries.data(), left_n * ENTRY_SIZE);
    std::memcpy(LeafEntry(right, 0), entries.data() + left_n, right_n * ENTRY_SIZE);
    NumKeys(leaf) = left_n;
    NumKeys(right) = right_n;

    // Splice the new leaf into the sibling chain
    NextLeaf(right) = NextLeaf(leaf);
    NextLeaf(leaf) = right_pid;

    bpm_->UnpinPage(right_pid, /*is_dirty=*/true);

    out.split = true;
    out.separator = entries[left_n].key;
    out.right = right_pid;
    return true;
}

bool BPlusTreeIndex::InsertIntoInternal(Page *node, int child_idx, int32_t separator,
                                        page_id_t right_child, SplitResult &out) {
    uint16_t n = NumKeys(node);
    int32_t *keys = Keys(node);
    page_id_t *children = Children(node);

    // The new separator goes at child_idx and the new child right after it
    if (n < internal_max_) {
        std::memmove(keys + child_idx + 1, keys + child_idx, (n - child_idx) * sizeof(int32_t));
        std::memmove(children + child_idx + 2, children + child_idx + 1,
                     (n - child_idx) * sizeof(page_id_t));
        keys[child_idx] = separator;
        children[child_idx + 1] = right_child;
        NumKeys(node) = static_cast<uint16_t>(n + 1);
        return true;
    }

    // Full: build the n+1 keys / n+2 children, then push the middle key up
    std::vector<int32_t> all_keys(keys, keys + n);
    std::vector<page_id_t> all_children(children, children + n + 1);
    all_keys.insert(all_keys.begin() + child_idx, separator);
    all_children.insert(all_children.begin() + child_idx + 1, right_child);

    page_id_t right_pid;
    Page *right = bpm_->NewPage(right_pid);
    if (!right) {
        return false;
    }
    InitInternalPage(right);

    const auto total = static_cast<uint16_t>(all_keys.size());
    const auto mid = static_cast<uint16_t>(total / 2);
    const auto right_n = static_cast<uint16_t>(total - mid - 1);

    // Left keeps keys [0, mid) and children [0, mid]
    std::memcpy(keys, all_keys.data(), mid * sizeof(int32_t));
    std::memcpy(children, all_children.data(), (mid + 1) * sizeof(page_id_t));
    NumKeys(node) = mid;

    // Right gets keys (mid, total) and children (mid, total]
    std::memcpy(Keys(right), all_keys.data() + mid + 1, right_n * sizeof(int32_t));
    std::memcpy(Children(right), all_children.data() + mid + 1, (right_n + 1) * sizeof(page_id_t));
    NumKeys(right) = right_n;

    bpm_->UnpinPage(right_pid, /*is_dirty=*/true);

    out.split = true;
    out.separator = all_keys[mid];
    out.right = right_pid;
    return true;
}

// Equal keys go left here: duplicates of `key` may end at the tail of the left
// child, and the leaf-chain walk picks up any that continue to the right.
page_id_t BPlusTreeIndex::FindLeaf(int32_t key) {
    page_id_t pid = root_page_id_;
    while (pid != -1) {
        Page *page = bpm_->FetchPage(pid);
        if (!page) {
            return -1;
        }
        if (IsLeaf(page)) {
            bpm_->UnpinPage(pid, /*is_dirty=*/false);
            return pid;
        }
        page_id_t child = Children(page)[InternalLowerBound(page, key)];
        bpm_->UnpinPage(pid, /*is_dirty=*/false);
        pid = child;
    }
    return -1;
}

void BPlusTreeIndex::SearchEqual(int32_t search_key, std::vector<RID> &result) {
    SearchRange(search_key, search_key, result);
}

void BPlusTreeIndex::SearchRange(int32_t low_key, int32_t high_key,
                                 std::vector<RID> &result) {
    result.clear();
    if (root_page_id_ == -1 || low_key > high_key) {
        return;
    }

    page_id_t pid = FindLeaf(low_key);
    bool first_leaf = true;

    while (pid != -1) {
        Page *page = bpm_->FetchPage(pid);
        if (!page) {
            return;
        }

        uint16_t n = NumKeys(page);
        int pos = first_leaf ? LeafLowerBound(page, low_key) : 0;
        first_leaf = false;

        for (int i = pos; i < n; ++i) {
            Entry *e = LeafEntry(page, static_cast<uint16_t>(i));
            if (e->key > high_key) {
                bpm_->UnpinPage(pid, /*is_dirty=*/false);
                return;
            }
            result.emplace_back(e->page_id, e->slot_id);
        }

        page_id_t next = NextLeaf(page);
        bpm_->UnpinPage(pid, /*is_dirty=*/false);
        pid = next;
    }
}

int BPlusTreeIndex::GetHeight() {
    int height = 0;
    page_id_t pid = root_page_id_;
    while (pid != -1) {
        Page *page = bpm_->FetchPage(pid);
        if (!page) {
            return -1;
        }
        ++height;
        page_id_t child = IsLeaf(page) ? -1 : Children(page)[0];
        bpm_->UnpinPage(pid, /*is_dirty=*/false);
        pid = child;
    }
    return height;
}

// ---- Validation ----

bool BPlusTreeIndex::ValidateNode(page_id_t pid, int depth, int &leaf_depth,
                                  bool has_lo, int32_t lo, bool has_hi, int32_t hi) {
    Page *page = bpm_->FetchPage(pid);
    if (!page) {
        return false;
    }

    uint16_t n = NumKeys(page);
    bool ok = true;

    auto in_bounds = [&](int32_t k) {
        return (!has_lo || k >= lo) && (!has_hi || k <= hi);
    };

    if (IsLeaf(page)) {
        if (leaf_depth == -1) leaf_depth = depth;
        ok = (depth == leaf_depth) && n <= leaf_max_;
        for (uint16_t i = 0; ok && i < n; ++i) {
            int32_t k = LeafEntry(page, i)->key;
            ok = in_bounds(k) && (i == 0 || LeafEntry(page, i - 1)->key <= k);
        }
        bpm_->UnpinPage(pid, /*is_dirty=*/false);
        return ok;
    }

    // Copy what we need, then unpin, so validation pins only one page per level
    std::vector<int32_t> keys(Keys(page), Keys(page) + n);
    std::vector<page_id_t> children(Children(page), Children(page) + n + 1);
    bpm_->UnpinPage(pid, /*is_dirty=*/false);

    if (n == 0 || n > internal_max_) {
        return false;
    }
    for (uint16_t i = 0; i < n; ++i) {
        if (!in_bounds(keys[i]) || (i > 0 && keys[i - 1] > keys[i])) {
            return false;
        }
    }

    for (uint16_t i = 0; i <= n; ++i) {
        bool child_has_lo = (i > 0) || has_lo;
        int32_t child_lo = (i > 0) ? keys[i - 1] : lo;
        bool child_has_hi = (i < n) || has_hi;
        int32_t child_hi = (i < n) ? keys[i] : hi;
        if (!ValidateNode(children[i], depth + 1, leaf_depth,
                          child_has_lo, child_lo, child_has_hi, child_hi)) {
            return false;
        }
    }
    return true;
}

bool BPlusTreeIndex::Validate(size_t *num_entries) {
    if (root_page_id_ == -1) {
        return false;
    }

    int leaf_depth = -1;
    if (!ValidateNode(root_page_id_, 0, leaf_depth, false, 0, false, 0)) {
        return false;
    }

    // Walk the leaf chain: keys must be non-decreasing across leaves
    size_t count = 0;
    int32_t prev = std::numeric_limits<int32_t>::min();
    page_id_t pid = FindLeaf(std::numeric_limits<int32_t>::min());
    std::unordered_set<page_id_t> seen;
    while (pid != -1) {
        if (!seen.insert(pid).second) {
            return false;  // cycle in the leaf chain
        }
        Page *page = bpm_->FetchPage(pid);
        if (!page) {
            return false;
        }
        uint16_t n = NumKeys(page);
        for (uint16_t i = 0; i < n; ++i) {
            int32_t k = LeafEntry(page, i)->key;
            if (k < prev) {
                bpm_->UnpinPage(pid, /*is_dirty=*/false);
                return false;
            }
            prev = k;
        }
        count += n;
        page_id_t next = NextLeaf(page);
        bpm_->UnpinPage(pid, /*is_dirty=*/false);
        pid = next;
    }

    if (num_entries) *num_entries = count;
    return true;
}
