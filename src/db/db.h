#pragma once

#include <mutex>
#include <memory>
#include <iostream>
#include <string>
#include "common/utils.h"
#include "common/log.h"

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

// 自定义 Handler 用于将一个 WriteBatch 的操作应用到另一个
class BatchMerger : public WriteBatch::Handler {
public:
    explicit BatchMerger(WriteBatch* target) : target_batch_(target) {}

    Status PutCF(uint32_t column_family_id, const Slice& key, const Slice& value) override {
        target_batch_->Put(column_family_id, key, value);
        return Status::OK();
    }

    Status DeleteCF(uint32_t column_family_id, const Slice& key) override {
        target_batch_->Delete(column_family_id, key);
        return Status::OK();
    }

    Status MergeCF(uint32_t column_family_id, const Slice& key, const Slice& value) override {
        target_batch_->Merge(column_family_id, key, value);
        return Status::OK();
    }

    // 如果需要支持其他操作（例如 SingleDelete），可以继续重写对应方法

private:
    WriteBatch* target_batch_;
};

// 合并两个 WriteBatch
Status MergeWriteBatches(WriteBatch& target, const WriteBatch& source) {
    BatchMerger merger(&target);
    return source.Iterate(&merger);
}

int main() {
    // 配置 RocksDB
    Options options;
    options.create_if_missing = true;

    // 打开数据库
    DB* db;
    Status status = DB::Open(options, "/tmp/rocksdb_test", &db);
    if (!status.ok()) {
        std::cerr << "Unable to open database: " << status.ToString() << std::endl;
        return 1;
    }

    // 创建两个 WriteBatch
    WriteBatch batch1;
    WriteBatch batch2;

    // 向 batch1 添加操作
    batch1.Put("key1", "value1");
    batch1.Delete("key2");

    // 向 batch2 添加操作
    batch2.Put("key3", "value3");
    batch2.Put("key1", "value1_overwritten"); // 会覆盖 batch1 中的 key1

    // 合并 batch2 到 batch1
    status = MergeWriteBatches(batch1, batch2);
    if (!status.ok()) {
        std::cerr << "Merge failed: " << status.ToString() << std::endl;
        return 1;
    }

    // 提交合并后的 batch1
    status = db->Write(WriteOptions(), &batch1);
    if (!status.ok()) {
        std::cerr << "Write failed: " << status.ToString() << std::endl;
        return 1;
    }

    // 验证结果
    std::string value;
    status = db->Get(ReadOptions(), "key1", &value);
    if (status.ok()) {
        std::cout << "key1: " << value << std::endl; // 应输出 "value1_overwritten"
    }

    status = db->Get(ReadOptions(), "key2", &value);
    if (status.IsNotFound()) {
        std::cout << "key2: deleted" << std::endl;
    }

    status = db->Get(ReadOptions(), "key3", &value);
    if (status.ok()) {
        std::cout << "key3: " << value << std::endl; // 应输出 "value3"
    }

    // 清理
    delete db;
    return 0;
}

#endif // LEVELDB

class DbWriteBatch {
public:
    DbWriteBatch() {}
    DbWriteBatch(const DbWriteBatch&) = default;
    DbWriteBatch& operator =(const DbWriteBatch&) = default;
    ~DbWriteBatch() {
        Clear();
    }

    void Put(const std::string& key, const std::string& value) {
        // if (data_map_.find(key) == data_map_.end()) {
        //     data_map_[key] = value;
        //     CHECK_MEMORY_SIZE(data_map_);
        // }

        db_batch_.Put(key, value);
        ++count_;
    }

    bool Exist(const std::string& key) {
        assert(false);
        return false;
        // return data_map_.find(key) != data_map_.end();
    }

    bool Get(const std::string& key, std::string* value) {
        // auto iter = data_map_.find(key);
        // if (iter == data_map_.end()) {
        //     return false;
        // }
        
        // *value = iter->second;
        assert(false);
        return false;
    }

    void Delete(const std::string& key) {
        // auto iter = data_map_.find(key);
        // if (iter != data_map_.end()) {
        //     data_map_.erase(iter);
        //     CHECK_MEMORY_SIZE(data_map_);
            db_batch_.Delete(key);
        // }
    }

    void Clear() {
        // data_map_.clear();
        db_batch_.Clear();
        count_ = 0;
    }

    void Append(DbWriteBatch& other) {
#ifdef LEVELDB
        db_batch_.Append(other);
#else
    // 合并 batch2 到 batch1
        MergeWriteBatches(db_batch_, other.db_batch_);
#endif
    }

    size_t ApproximateSize() const {
#ifdef LEVELDB
        return db_batch_.ApproximateSize();
#else
        return count_ > 0 ? 100 : 0;
#endif
    }

    
    TmpDbWriteBatch db_batch_;
    uint32_t count_ = 0;
    // std::unordered_map<std::string, std::string> data_map_;
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

        ZJC_INFO("write to db datasize: %u", db_batch.ApproximateSize());
        DbWriteOptions write_opt;
#ifndef LEVELDB
        // write_opt.disableWAL = true;
#endif
        auto st = db_->Write(write_opt, &db_batch.db_batch_);
        db_batch.Clear();
        return st;
    }

    DbStatus Put(const std::string& key, const std::string& value) {
        DbWriteOptions write_opt;
        return db_->Put(write_opt, DbSlice(key), DbSlice(value));
    }

    DbStatus Put(const std::string& key, const char* value, size_t len) {
        DbWriteOptions write_opt;
        return db_->Put(write_opt, DbSlice(key), DbSlice(value, len));
    }

    DbStatus Get(const std::string& key, std::string* value) {
        DbReadOptions read_opt;
        return db_->Get(read_opt, DbSlice(key), value);
    }

    std::vector<DbStatus> Get(const std::vector<DbSlice>& keys, std::vector<std::string>* value) {
        DbReadOptions read_opt;
        return std::vector<DbStatus>();
//         return db_->MultiGet(read_opt, keys, value);
    }

    DbStatus Delete(const std::string& key) {
        DbWriteOptions write_opt;
        return db_->Delete(write_opt, DbSlice(key));
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
    bool inited_{ false };
    std::mutex mutex;
};

}  // db

}  // zjchain
