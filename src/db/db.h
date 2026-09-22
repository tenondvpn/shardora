#pragma once

#include <mutex>
#include <memory>
#include <iostream>
#include <string>

#include "common/global_info.h"
#include "common/log.h"
#include "common/utils.h"

#ifdef LEVELDB
#include "leveldb/options.h"
#include "leveldb/slice.h"
#include "leveldb/status.h"
#include "leveldb/write_batch.h"
#include "leveldb/cache.h"
#include "leveldb/filter_policy.h"
#include "leveldb/db.h"
#else
#include "rocksdb/db.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/status.h"
#include "rocksdb/table.h"
#include "rocksdb/write_batch.h"
#endif

namespace shardora {

namespace db {

#ifdef LEVELDB
    typedef leveldb::WriteBatch TmpDbWriteBatch;
    typedef leveldb::Status DbStatus;
    typedef leveldb::DB DickDb;
    typedef leveldb::WriteOptions DbWriteOptions;
    typedef leveldb::ReadOptions DbReadOptions;
    typedef leveldb::Slice DbSlice;
    typedef leveldb::Iterator DbIterator;
#else
    typedef rocksdb::WriteBatch TmpDbWriteBatch;
    typedef rocksdb::Status DbStatus;
    typedef rocksdb::DB DickDb;
    typedef rocksdb::WriteOptions DbWriteOptions;
    typedef rocksdb::ReadOptions DbReadOptions;
    typedef rocksdb::Slice DbSlice;
    typedef rocksdb::Iterator DbIterator;



using namespace ROCKSDB_NAMESPACE;

// Custom Handler to apply operations from one WriteBatch to another
class BatchMerger : public WriteBatch::Handler {
public:
    explicit BatchMerger(WriteBatch* target) : target_batch_(target) {}

    Status PutCF(uint32_t column_family_id, const Slice& key, const Slice& value) override {
        target_batch_->Put(0, key, value);
        return Status::OK();
    }

    Status DeleteCF(uint32_t column_family_id, const Slice& key) override {
        target_batch_->Delete(0, key);
        return Status::OK();
    }

    Status MergeCF(uint32_t column_family_id, const Slice& key, const Slice& value) override {
        target_batch_->Merge(0, key, value);
        return Status::OK();
    }

    // If you need to support other operations (e.g. SingleDelete), continue to override corresponding methods

private:
    WriteBatch* target_batch_;
};

// Merge two WriteBatches
inline static Status MergeWriteBatches(WriteBatch& target, const WriteBatch& source) {
    BatchMerger merger(&target);
    return source.Iterate(&merger);
}

#endif // LEVELDB

class DbWriteBatch {
public:
    DbWriteBatch() {
        common::GlobalInfo::Instance()->AddSharedObj(5);
    }
    DbWriteBatch(const DbWriteBatch&) = default;
    DbWriteBatch& operator =(const DbWriteBatch&) = default;
    ~DbWriteBatch() {
        Clear();
        common::GlobalInfo::Instance()->DecSharedObj(5);
    }

    void Put(const std::string& key, const std::string& value) {
#ifndef NDEBUG
        if (thread_id_ == std::thread::id()) {
            thread_id_ = std::this_thread::get_id();
        } else if (thread_id_ != std::this_thread::get_id()) {
            //assert(false);
        }
        if (data_map_.find(key) == data_map_.end()) {
            data_map_[key] = value;
        }
#endif

        db_batch_.Put(key, value);
        count_ += key.size() + value.size();
    }

    void Delete(const std::string& key) {
#ifndef NDEBUG
        if (thread_id_ == std::thread::id()) {
            thread_id_ = std::this_thread::get_id();
        } else if (thread_id_ != std::this_thread::get_id()) {
            //assert(false);
        }

        auto iter = data_map_.find(key);
        if (iter != data_map_.end()) {
            data_map_.erase(iter);
        }
#endif
        db_batch_.Delete(key);
    }

    void Clear() {
#ifndef NDEBUG
        data_map_.clear();
#endif
        db_batch_.Clear();
        count_ = 0;
    }

    void Append(DbWriteBatch& other) {
#ifndef NDEBUG
        if (thread_id_ == std::thread::id()) {
            thread_id_ = std::this_thread::get_id();
        } else if (thread_id_ != std::this_thread::get_id()) {
            //assert(false);
        }
#endif
#ifdef LEVELDB
        db_batch_.Append(other.db_batch_);
#else
    // Merge batch2 into batch1
        MergeWriteBatches(db_batch_, other.db_batch_);
#endif
    }

    size_t ApproximateSize() const {
        return count_;
    }

    
    TmpDbWriteBatch db_batch_;
    uint32_t count_ = 0;
#ifndef NDEBUG
    std::unordered_map<std::string, std::string> data_map_;
    std::thread::id thread_id_;
#endif
};

class Db {
public:
    Db();
    ~Db();
    bool Init(const std::string& db_path);
    void Destroy();
    bool Exist(const std::string& key) {
        DbReadOptions read_opt;
        std::string val;
        read_opt.fill_cache = false;
        read_opt.verify_checksums = false;
        auto status = db_->Get(read_opt, key, &val);
        return status.ok(); 
        // DbIterator* it = db_->NewIterator(DbReadOptions());
        // it->Seek(key);
        // bool res = false;
        // if (it->Valid() && it->key().size() == key.size() &&
        //         memcmp(it->key().data(), key.c_str(), key.size()) == 0) {
        //     res = true;
        // }

        // delete it;
        // return res;
    }

    DbStatus Put(DbWriteBatch& db_batch) {
        if (db_batch.ApproximateSize() <= 12) {
            return db::DbStatus();
        }

        SHARDORA_DEBUG("write to db datasize: %u", db_batch.ApproximateSize());
        DbWriteOptions write_opt;
#ifndef LEVELDB
        // write_opt.disableWAL = true;
#endif
        auto st = db_->Write(write_opt, &db_batch.db_batch_);
        if (!st.ok()) {
            SHARDORA_ERROR("write to db failed: %s", st.ToString().c_str());
        }
        db_batch.Clear();
        return st;
    }

    DbStatus Put(const std::string& key, const std::string& value) {
        DbWriteOptions write_opt;
        auto st = db_->Put(write_opt, DbSlice(key), DbSlice(value));
        if (!st.ok()) {
            SHARDORA_ERROR("write to db failed: %s", st.ToString().c_str());
        }
        return st;
    }

    DbStatus Put(const std::string& key, const char* value, size_t len) {
        DbWriteOptions write_opt;
        auto st = db_->Put(write_opt, DbSlice(key), DbSlice(value, len));
        if (!st.ok()) {
            SHARDORA_ERROR("write to db failed: %s", st.ToString().c_str());
        }
        return st;
    }

    DbStatus Get(const std::string& key, std::string* value) {
        DbReadOptions read_opt;
        return db_->Get(read_opt, DbSlice(key), value);
    }

    // Read bytes [offset, offset+length) from the stored value of key.
    // Always safe: offset and length are clamped to the actual value size.
    //   - Returns NotFound (and leaves *out unchanged) if key does not exist.
    //   - If offset >= value.size(), returns ok() with out->clear() (empty slice).
    //   - If offset+length > value.size(), length is silently clamped.
    // NOTE: reads the full value — use GetSubValueChunked for large values.
    DbStatus GetSubValue(const std::string& key,
                         size_t offset, size_t length,
                         std::string* out) {
        out->clear();
        std::string full_value;
        auto st = Get(key, &full_value);
        if (!st.ok()) return st;
        const size_t val_size = full_value.size();
        if (offset >= val_size || length == 0) return st;
        const size_t available = val_size - offset;
        const size_t safe_len  = (length <= available) ? length : available;
        out->assign(full_value.data() + offset, safe_len);
        return st;
    }

    // Write value as fixed kPoraChunkSize-byte chunks for efficient partial reads.
    // Chunk keys: key + uint32_le(chunk_index)
    // Size key:   key + uint32_le(0xFFFFFFFF) → uint64_le(total_size)
    // All entries are added to batch; flushed atomically with the caller's batch.
    static constexpr size_t kPoraChunkSize = 1024;

    void PutChunked(const std::string& key,
                    const std::string& value,
                    DbWriteBatch& batch) {
        const size_t val_size = value.size();
        constexpr uint32_t kSizeSentinel = 0xFFFFFFFFu;
        std::string size_key = key;
        size_key.append(reinterpret_cast<const char*>(&kSizeSentinel), 4);
        uint64_t sz64 = static_cast<uint64_t>(val_size);
        batch.Put(size_key, std::string(reinterpret_cast<const char*>(&sz64), 8));

        const uint32_t num_chunks = static_cast<uint32_t>(
            (val_size + kPoraChunkSize - 1) / kPoraChunkSize);
        for (uint32_t i = 0; i < num_chunks; ++i) {
            std::string chunk_key = key;
            chunk_key.append(reinterpret_cast<const char*>(&i), 4);
            const size_t start = i * kPoraChunkSize;
            const size_t len   = std::min(kPoraChunkSize, val_size - start);
            batch.Put(chunk_key, std::string(value.data() + start, len));
        }
    }

    // Read bytes [offset, offset+length) from chunked storage written by PutChunked.
    // Only loads the 1-2 RocksDB entries that overlap the requested range.
    // Offset and length are clamped; no error is returned for out-of-range inputs.
    DbStatus GetSubValueChunked(const std::string& key,
                                 size_t offset, size_t length,
                                 std::string* out) {
        out->clear();
        if (length == 0) return DbStatus();

        constexpr uint32_t kSizeSentinel = 0xFFFFFFFFu;
        std::string size_key = key;
        size_key.append(reinterpret_cast<const char*>(&kSizeSentinel), 4);
        std::string size_str;
        auto st = Get(size_key, &size_str);
        if (!st.ok()) return st;

        const uint64_t val_size =
            *reinterpret_cast<const uint64_t*>(size_str.data());
        if (offset >= static_cast<size_t>(val_size)) return DbStatus();

        const size_t available = static_cast<size_t>(val_size) - offset;
        const size_t safe_len  = (length <= available) ? length : available;

        const uint32_t first_chunk =
            static_cast<uint32_t>(offset / kPoraChunkSize);
        const uint32_t last_chunk  =
            static_cast<uint32_t>((offset + safe_len - 1) / kPoraChunkSize);

        out->reserve(safe_len);
        size_t cur  = offset;
        size_t left = safe_len;
        for (uint32_t ci = first_chunk; ci <= last_chunk; ++ci) {
            std::string chunk_key = key;
            chunk_key.append(reinterpret_cast<const char*>(&ci), 4);
            std::string chunk_data;
            st = Get(chunk_key, &chunk_data);
            if (!st.ok()) return st;

            const size_t intra = cur - static_cast<size_t>(ci) * kPoraChunkSize;
            const size_t take  = std::min(chunk_data.size() - intra, left);
            out->append(chunk_data.data() + intra, take);
            cur  += take;
            left -= take;
        }
        return st;
    }

    std::vector<DbStatus> Get(const std::vector<DbSlice>& keys, std::vector<std::string>* value) {
        DbReadOptions read_opt;
        return std::vector<DbStatus>();
//         return db_->MultiGet(read_opt, keys, value);
    }

    DbStatus Delete(const std::string& key) {
        DbWriteOptions write_opt;
        auto st = db_->Delete(write_opt, DbSlice(key));
        if (!st.ok()) {
            SHARDORA_ERROR("write to db failed: %s", st.ToString().c_str());
        }
        return st;
    }

    // void CompactRange(const std::string& start_key, const std::string& end_key) {
    //     auto s = DbSlice(start_key);
    //     auto e = DbSlice(end_key);
    //     db_->CompactRange(&s, &e);
    // }

    void ClearPrefix(const std::string& prefix) {
        DbReadOptions option;
        auto iter = db_->NewIterator(option);
        iter->Seek(prefix);
        int32_t valid_count = 0;
        while (iter->Valid()) {
            if (memcmp(prefix.c_str(), iter->key().data(), prefix.size()) != 0) {
                break;
            }

            DbWriteOptions write_opt;
            db_->Delete(write_opt, iter->key());
            ++valid_count;
            iter->Next();
        }

        delete iter;
    }

    void GetAllPrefix(const std::string& prefix, std::map<std::string, std::string>& res_map) {
        DbReadOptions option;
        auto iter = db_->NewIterator(option);
        iter->Seek(prefix);
        while (iter->Valid()) {
            if (memcmp(prefix.c_str(), iter->key().data(), prefix.size()) != 0) {
                break;
            }

            res_map[iter->key().ToString()] = iter->value().ToString();
            iter->Next();
        }
    }

    DickDb* db() {
        return db_;
    }

    DickDb* db_ = nullptr;

private:
    Db(const Db&);
    Db(const Db&&);
    Db& operator=(const Db&);
    std::atomic<bool> inited_{ false };
    std::mutex mutex;
};

}  // db

}  // shardora
