//
// Created by Haoran Gan on 11/14/25.
//

#include "catalog/table_heap.hpp"

#include <stdexcept>

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

TableHeap::TableHeap(BufferPoolManager *bpm, const std::vector<page_id_t> &pages)
    : bpm_(bpm), pages_(pages) {}

bool TableHeap::InsertTuple(const char *data, uint16_t size, RID &rid) {
    if (bpm_ == nullptr) {
        return false;
    }
    if (pages_.empty()) {
        return false;  // should not happen if created via Create()
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

    // No space in last page → unpin it (no change made)
    bpm_->UnpinPage(last_pid, /*is_dirty=*/false);

    // Allocate a new page
    page_id_t new_pid;
    Page *new_page = bpm_->NewPage(new_pid);
    if (new_page == nullptr) {
        return false;
    }

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
