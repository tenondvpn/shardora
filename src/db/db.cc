#include "db/db.h"

#include <iostream>

#include "common/utils.h"
#include "common/log.h"

namespace shardora {

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
    // options.max_file_size = 32 * 1048576; // leveldb 1.20
    // int32_t max_open_files = cache_size / 1024 * 300;
    // if (max_open_files < 500) {
    //     max_open_files = 500;
    // }

    // if (max_open_files > 1000) {
    //     max_open_files = 1000;
    // }

    // options.max_open_files = max_open_files;
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    // options.block_cache = leveldb::NewLRUCache(cache_size * 1048576);
    // options.block_size = 32 * 1024;
    // options.write_buffer_size = 64 * 1024 * 1024;
    // options.compression = leveldb::kSnappyCompression;
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
    // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
    options.IncreaseParallelism();
    options.OptimizeLevelStyleCompaction();
    // create the DB if it's not already present
    // options.create_if_missing = true;
    // options.max_bytes_for_level_multiplier = 5;
    // options.level_compaction_dynamic_level_bytes = true;
    // rocksdb::BlockBasedTableOptions table_option;
    // table_option.filter_policy.reset(rocksdb::NewBloomFilterPolicy(12, false));
    // table_option.block_cache = rocksdb::NewLRUCache(8 * 1024 * 1024 * 1024);
    // options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_option));
    // options.compression = rocksdb::kSnappyCompression;
    // options.bottommost_compression = rocksdb::kZlibCompression;
    // options.compaction_style = rocksdb::kCompactionStyleUniversal;
    // options.compaction_options_universal.size_ratio = 20;
    // options.level0_file_num_compaction_trigger = 8;
    // options.max_bytes_for_level_base = 512 * 1024 * 1024;
    // options.write_buffer_size = 512 * 1024 * 1024;
    // options.max_write_buffer_number = 4;
    // options.max_background_compactions  = 8;
    // options.optimize_filters_for_hits = false;

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
