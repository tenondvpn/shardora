#include "db/db.h"

#include <iostream>

#include "common/utils.h"
#include "common/log.h"

namespace shardora {

namespace db {

Db::Db() {
}

Db::~Db() {
    Destroy();
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
        SHARDORA_ERROR("storage db is inited![%s]", db_path.c_str());
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex);
    if (inited_) {
        SHARDORA_ERROR("storage db is inited![%s]", db_path.c_str());
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
        SHARDORA_ERROR("open db[%s] failed, error[%s]", db_path.c_str(), status.ToString().c_str());
        return false;
    }

    inited_ = true;
    return true;
}

#else

bool Db::Init(const std::string& db_path) {
    if (inited_) {
        SHARDORA_ERROR("storage db is inited![%s]", db_path.c_str());
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex);
    if (inited_) {
        SHARDORA_ERROR("storage db is inited![%s]", db_path.c_str());
        return false;
    }

    rocksdb::Options options;
    options.create_if_missing = true;
    // Memory budget.  These are per-process caps and every node runs many
    // shardora processes on one host, so the defaults below are sized for tens
    // of instances sharing the machine, not for one process owning it:
    //   block cache  : 128 MB  (was 512 MB)
    //   memtables    : 64 MB x 2 = 128 MB  (was 512 MB x 4 = 2 GB)
    // The previous settings allowed ~2.5 GB per process just for RocksDB, which
    // is why RSS climbed from ~300 MB to ~2 GB and swap filled up.
    rocksdb::BlockBasedTableOptions table_option;
    table_option.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
    table_option.block_cache = rocksdb::NewLRUCache(128 * 1024 * 1024);
    options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_option));
    options.compression = rocksdb::kSnappyCompression;
    options.bottommost_compression = rocksdb::kZlibCompression;
    options.compaction_style = rocksdb::kCompactionStyleUniversal;
    options.compaction_options_universal.size_ratio = 20;
    options.level0_file_num_compaction_trigger = 8;
    options.max_bytes_for_level_base = 128 * 1024 * 1024;
    options.write_buffer_size = 64 * 1024 * 1024;
    options.max_write_buffer_number = 2;
    options.max_background_compactions = 2;
    options.max_background_flushes = 1;
    // LevelStyleCompaction would rewrite the compaction knobs that were just
    // set for universal compaction, and IncreaseParallelism() sizes the thread
    // pools for a machine dedicated to one DB.  Both are deliberately not used.
    options.optimize_filters_for_hits = false;

    rocksdb::Status status = rocksdb::DB::Open(options, db_path, &db_);
    if (!status.ok()) {
        SHARDORA_ERROR("open db[%s] failed, error[%s]", db_path.c_str(), status.ToString().c_str());
        return false;
    }

    inited_ = true;
    return true;
}

#endif

}  // db

}  // shardora
