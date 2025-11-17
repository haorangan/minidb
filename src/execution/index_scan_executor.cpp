//
// Created by Haoran Gan on 11/15/25.
//

#include "execution/index_scan_executor.hpp"

IndexScanExecutor::IndexScanExecutor(BufferPoolManager *bpm,
                                     TableHeap * /*table*/,
                                     BPlusTreeIndex *index,
                                     int32_t key,
                                     std::string label)
    : bpm_(bpm),
      index_(index),
      label_(std::move(label)),
      scan_type_(ScanType::POINT),
      point_key_(key),
      low_key_(0),
      high_key_(0),
      initialized_(false),
      match_idx_(0) {}

IndexScanExecutor::IndexScanExecutor(BufferPoolManager *bpm,
                                     TableHeap * /*table*/,
                                     BPlusTreeIndex *index,
                                     int32_t low_key,
                                     int32_t high_key,
                                     std::string label)
    : bpm_(bpm),
      index_(index),
      label_(std::move(label)),
      scan_type_(ScanType::RANGE),
      point_key_(0),
      low_key_(low_key),
      high_key_(high_key),
      initialized_(false),
      match_idx_(0) {}

void IndexScanExecutor::Init() {
    initialized_ = false;
    match_idx_ = 0;
}

void IndexScanExecutor::InitMatches() {
    if (initialized_) return;
    initialized_ = true;
    matches_.clear();

    if (!index_) return;

    if (scan_type_ == ScanType::POINT) {
        index_->SearchEqual(point_key_, matches_);
    } else {
        index_->SearchRange(low_key_, high_key_, matches_);
    }

    match_idx_ = 0;
}

bool IndexScanExecutor::Next(Tuple &out, RID &rid) {
    InitMatches();

    if (match_idx_ >= matches_.size()) {
        return false;
    }

    RID r = matches_[match_idx_++];
    rid = r;

    Page *page = bpm_->FetchPage(r.page_id);
    if (!page) {
        return false;
    }

    SlottedPage sp(page);
    char buf[PAGE_SIZE];  // a tuple can never be larger than a page
    uint16_t sz = 0;

    bool ok = sp.GetTuple(r, buf, sz);
    bpm_->UnpinPage(r.page_id, /*is_dirty=*/false);

    if (!ok || sz % sizeof(int32_t) != 0) {
        return false;
    }

    size_t count = sz / sizeof(int32_t);
    auto *vals = reinterpret_cast<int32_t *>(buf);
    out = Tuple::FromRawInts(vals, count);
    return true;
}