//
// Created by Haoran Gan on 11/23/25.
//

#include "catalog/catalog.hpp"

#include <cstring>
#include <stdexcept>

namespace {

// Bounds-checked cursor over the catalog page
class PageWriter {
public:
    explicit PageWriter(char *data) : data_(data) {}

    template <typename T>
    void Put(const T &v) { Raw(&v, sizeof(T)); }

    void PutString(const std::string &s) {
        if (s.size() > 255) throw std::runtime_error("Name too long: " + s);
        Put(static_cast<uint8_t>(s.size()));
        Raw(s.data(), s.size());
    }

private:
    char *data_;
    size_t pos_ = 8;  // after magic + num_tables

    void Raw(const void *src, size_t n) {
        if (pos_ + n > PAGE_SIZE) throw std::runtime_error("Catalog full: too many tables");
        std::memcpy(data_ + pos_, src, n);
        pos_ += n;
    }
};

class PageReader {
public:
    explicit PageReader(const char *data) : data_(data) {}

    template <typename T>
    T Get() {
        T v;
        Raw(&v, sizeof(T));
        return v;
    }

    std::string GetString() {
        std::string s(Get<uint8_t>(), '\0');
        Raw(s.data(), s.size());
        return s;
    }

private:
    const char *data_;
    size_t pos_ = 8;

    void Raw(void *dst, size_t n) {
        if (pos_ + n > PAGE_SIZE) throw std::runtime_error("Corrupt catalog page");
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
    }
};

}  // namespace

void Catalog::Initialize() {
    page_id_t pid;
    Page *page = bpm_->NewPage(pid);
    if (pid != CATALOG_PAGE_ID) {
        bpm_->UnpinPage(pid, /*is_dirty=*/false);
        throw std::logic_error("Catalog::Initialize: database is not empty");
    }
    std::memcpy(page->GetData(), &MAGIC, sizeof(MAGIC));
    bpm_->UnpinPage(pid, /*is_dirty=*/true);

    tables_.clear();
    order_.clear();
}

void Catalog::Load() {
    tables_.clear();
    order_.clear();

    // Copy the page out first: opening tables fetches other pages
    char data[PAGE_SIZE];
    Page *page = bpm_->FetchPage(CATALOG_PAGE_ID);
    std::memcpy(data, page->GetData(), PAGE_SIZE);
    bpm_->UnpinPage(CATALOG_PAGE_ID, /*is_dirty=*/false);

    uint32_t magic;
    uint16_t num_tables;
    std::memcpy(&magic, data, sizeof(magic));
    std::memcpy(&num_tables, data + 4, sizeof(num_tables));
    if (magic != MAGIC) {
        throw std::runtime_error("Not a MiniDB database (bad catalog page)");
    }

    PageReader in(data);
    for (uint16_t t = 0; t < num_tables; ++t) {
        TableEntry entry;
        entry.name = in.GetString();
        auto first_page = in.Get<page_id_t>();
        auto index_root = in.Get<page_id_t>();
        auto num_cols = in.Get<uint8_t>();

        std::vector<Column> cols;
        for (uint8_t c = 0; c < num_cols; ++c) {
            cols.emplace_back(in.GetString(), TypeId::INT);
        }
        entry.schema = Schema(std::move(cols));
        entry.table.reset(TableHeap::Open(bpm_, first_page));
        entry.index = std::make_unique<BPlusTreeIndex>(bpm_);
        entry.index->Open(index_root);

        order_.push_back(entry.name);
        tables_.emplace(entry.name, std::move(entry));
    }
}

void Catalog::Save() {
    char data[PAGE_SIZE] = {};
    std::memcpy(data, &MAGIC, sizeof(MAGIC));
    auto num_tables = static_cast<uint16_t>(order_.size());
    std::memcpy(data + 4, &num_tables, sizeof(num_tables));

    PageWriter out(data);
    for (const std::string &name : order_) {
        const TableEntry &entry = tables_.at(name);
        out.PutString(entry.name);
        out.Put(entry.table->GetFirstPageId());
        out.Put(entry.index->GetRootPageId());
        if (entry.schema.GetColumnCount() > 255) throw std::runtime_error("Too many columns");
        out.Put(static_cast<uint8_t>(entry.schema.GetColumnCount()));
        for (const Column &col : entry.schema.GetColumns()) {
            out.PutString(col.name);
        }
    }

    Page *page = bpm_->FetchPage(CATALOG_PAGE_ID);
    std::memcpy(page->GetData(), data, PAGE_SIZE);
    bpm_->UnpinPage(CATALOG_PAGE_ID, /*is_dirty=*/true);
}

TableHeap *Catalog::CreateTable(const std::string &name, const Schema &schema) {
    if (tables_.count(name) != 0) {
        // just throw, could also add "IF NOT EXISTS" later
        throw std::runtime_error("Table already exists: " + name);
    }
    if (schema.GetColumnCount() == 0) {
        throw std::runtime_error("Table must have at least one column");
    }

    TableEntry entry;
    entry.name = name;
    entry.schema = schema;
    entry.table.reset(TableHeap::Create(bpm_));
    entry.index = std::make_unique<BPlusTreeIndex>(bpm_);
    entry.index->Init();

    TableHeap *raw_ptr = entry.table.get();
    order_.push_back(name);
    tables_.emplace(name, std::move(entry));
    Save();
    return raw_ptr;
}

TableHeap *Catalog::GetTable(const std::string &name) {
    auto it = tables_.find(name);
    if (it == tables_.end()) return nullptr;
    return it->second.table.get();
}

const Schema *Catalog::GetSchema(const std::string &name) const {
    auto it = tables_.find(name);
    if (it == tables_.end()) return nullptr;
    return &it->second.schema;
}

BPlusTreeIndex *Catalog::GetIndex(const std::string &table_name,
                                  const std::string &column_name) const {
    auto it = tables_.find(table_name);
    if (it == tables_.end()) return nullptr;
    const TableEntry &entry = it->second;
    if (entry.schema.GetColumn(0).name != column_name) return nullptr;
    return entry.index.get();
}
