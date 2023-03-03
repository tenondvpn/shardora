#include "db/db.h"

#include <iostream>

#include "common/utils.h"
#include "common/log.h"

namespace zjchain {

namespace db {

Db::Db() {
}

Db::~Db() {
}

void Db::Destroy() {
    std::unique_lock<std::mutex> lock(mutex);
    if (inited_) {
        inited_ = false;
        if (db_ != nullptr) {
            delete db_;
            db_ = nullptr;
        }
    }
}

#ifdef LEVELDB
bool Db::Init(const std::string& db_path) {
    if (inited_) {
        ZJC_ERROR("storage db is inited![%s]", db_path.c_str());
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex);
    if (inited_) {
        ZJC_ERROR("storage db is inited![%s]", db_path.c_str());
        return false;
    }

    static const int32_t cache_size = 500;
    leveldb::Options options;
    options.create_if_missing = true;
    options.max_file_size = 32 * 1048576; // leveldb 1.20
    options.create_if_missing = true;
    int32_t max_open_files = cache_size / 1024 * 300;
    if (max_open_files < 500) {
        max_open_files = 500;
    }

    if (max_open_files > 1000) {
        max_open_files = 1000;
    }

    options.max_open_files = max_open_files;
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    options.block_cache = leveldb::NewLRUCache(cache_size * 1048576);
    options.block_size = 32 * 1024;
    options.write_buffer_size = 64 * 1024 * 1024;
    options.compression = leveldb::kSnappyCompression;
    DbStatus status = leveldb::DB::Open(options, db_path, &db_);
    if (!status.ok()) {
        ZJC_ERROR("open db[%s] failed, error[%s]", db_path.c_str(), status.ToString().c_str());
        return false;
    }

    inited_ = true;
    return true;
}

#else

bool Db::Init(const std::string& db_path) {
    if (inited_) {
        ZJC_ERROR("storage db is inited![%s]", db_path.c_str());
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex);
    if (inited_) {
        ZJC_ERROR("storage db is inited![%s]", db_path.c_str());
        return false;
    }

    rocksdb::Options options;
    options.compaction_style = rocksdb::kCompactionStyleUniversal;
    options.write_buffer_size = 67108864 / 64; // 64MB
    options.max_write_buffer_number = 3 / 3;
    options.target_file_size_base = 67108864 / 64; // 64MB
    options.max_background_compactions = 2;
    options.level0_file_num_compaction_trigger = 8;
    options.level0_slowdown_writes_trigger = 17;
    options.level0_stop_writes_trigger = 24;
    options.num_levels = 4;
    options.max_bytes_for_level_base = 536870912 / 64; // 512MB
    options.max_bytes_for_level_multiplier = 8 / 2;
    options.create_if_missing = true;
    options.keep_log_file_num = 1;
    options.max_open_files = 10 / 5;
    // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
    options.IncreaseParallelism();
    options.OptimizeLevelStyleCompaction();

//     options.prefix_extractor.reset(rocksdb::NewFixedPrefixTransform(3));
//     options.memtable_prefix_bloom_bits = 100000000;
//     options.memtable_prefix_bloom_probes = 6;
// 
//     // Enable prefix bloom for SST files
//     rocksdb::BlockBasedTableOptions table_options;
//     table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, true));
//     options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options)

    rocksdb::Status status = rocksdb::DB::Open(options, db_path, &db_);
    if (!status.ok()) {
        ZJC_ERROR("open db[%s] failed, error[%s]", db_path.c_str(), status.ToString().c_str());
        return false;
    }

    inited_ = true;
    return true;
}

#endif

}  // db

}  // zjchain
