//
// Created by Haoran Gan on 11/14/25.
//

#include "catalog/table_heap.hpp"

#include <stdexcept>
#include <unordered_set>

TableHeap *TableHeap::Create(BufferPoolManager *bpm) {
    if (bpm == nullptr) {
        throw std::runtime_error("TableHeap::Create: bpm is null");
    }

    // Allocate the first page of the table
    page_id_t first_pid;
    Page *page = bpm->NewPage(first_pid);

    if (page == nullptr) {
        throw std::runtime_error("TableHeap::Create: failed to allocate first page");
    }

    // Initialize as an empty slotted page
    SlottedPage sp(page);
    sp.Init();

    // Mark page as dirty because we wrote header
    bpm->UnpinPage(first_pid, /*is_dirty=*/true);

    std::vector<page_id_t> pages;
    pages.push_back(first_pid);

    // Return a new TableHeap
    return new TableHeap(bpm, pages);
}

TableHeap *TableHeap::Open(BufferPoolManager *bpm, page_id_t first_page_id) {
    std::vector<page_id_t> pages;
    std::unordered_set<page_id_t> seen;
    for (page_id_t pid = first_page_id; pid != -1;) {
        if (!seen.insert(pid).second) {
            throw std::runtime_error("TableHeap::Open: corrupt page chain (cycle)");
        }
        Page *page = bpm->FetchPage(pid);
        if (page == nullptr) {
            throw std::runtime_error("TableHeap::Open: failed to fetch page");
        }
        pages.push_back(pid);
        page_id_t next = SlottedPage(page).GetNextPageId();
        bpm->UnpinPage(pid, /*is_dirty=*/false);
        pid = next;
    }
    return new TableHeap(bpm, pages);
}

TableHeap::TableHeap(BufferPoolManager *bpm, const std::vector<page_id_t> &pages)
    : bpm_(bpm), pages_(pages) {}

bool TableHeap::InsertTuple(const char *data, uint16_t size, RID &rid) {
    if (bpm_ == nullptr) {
        return false;
    }
    if (pages_.empty()) {
        return false;  // should not happen if created via Create()
    }
    if (size > SlottedPage::MAX_TUPLE_SIZE) {
        return false;  // would not fit even on an empty page
    }

    // Try to insert into the last page
    page_id_t last_pid = pages_.back();
    Page *page = bpm_->FetchPage(last_pid);
    if (page == nullptr) {
        return false;
    }

    {
        SlottedPage sp(page);
        if (sp.HasSpace(size)) {
            bool ok = sp.InsertTuple(data, size, rid);
            bpm_->UnpinPage(last_pid, /*is_dirty=*/ok);
            return ok;
        }
    }

    // Allocate a new page while the last page is still pinned, so we can link it
    page_id_t new_pid;
    Page *new_page = bpm_->NewPage(new_pid);
    if (new_page == nullptr) {
        bpm_->UnpinPage(last_pid, /*is_dirty=*/false);
        return false;
    }
    SlottedPage(page).SetNextPageId(new_pid);
    bpm_->UnpinPage(last_pid, /*is_dirty=*/true);

    // Initialize as an empty slotted page
    {
        SlottedPage sp(new_page);
        sp.Init();

        // Insert tuple into new page
        bool ok = sp.InsertTuple(data, size, rid);
        // Update table’s page list
        if (ok) {
            pages_.push_back(new_pid);
        }

        bpm_->UnpinPage(new_pid, /*is_dirty=*/ok);
        return ok;
    }
}
