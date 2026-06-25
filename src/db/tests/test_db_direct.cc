#include <stdlib.h>

#include <iostream>
#include <string>
#include <filesystem>

#include <gtest/gtest.h>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"

using ROCKSDB_NAMESPACE::DB;
using ROCKSDB_NAMESPACE::Options;
using ROCKSDB_NAMESPACE::PinnableSlice;
using ROCKSDB_NAMESPACE::ReadOptions;
using ROCKSDB_NAMESPACE::Status;
using ROCKSDB_NAMESPACE::WriteBatch;
using ROCKSDB_NAMESPACE::WriteOptions;

namespace seth {

namespace db {

namespace test {

class TestDbDirect : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}

    virtual void SetUp() {
        db_path_ = "/tmp/seth_rocksdb_direct_test";
        std::filesystem::remove_all(db_path_);
    }

    virtual void TearDown() {
        std::filesystem::remove_all(db_path_);
    }

protected:
    std::string db_path_;
};

TEST_F(TestDbDirect, BasicRocksDBOperations) {
    DB* db = nullptr;
    Options options;
    options.IncreaseParallelism();
    options.OptimizeLevelStyleCompaction();
    options.create_if_missing = true;

    Status s = DB::Open(options, db_path_, &db);
    ASSERT_TRUE(s.ok());
    ASSERT_NE(db, nullptr);

    // Put
    s = db->Put(WriteOptions(), "key1", "value1");
    ASSERT_TRUE(s.ok());

    // Get
    std::string value;
    s = db->Get(ReadOptions(), "key1", &value);
    ASSERT_TRUE(s.ok());
    ASSERT_EQ(value, "value1");

    // WriteBatch
    {
        WriteBatch batch;
        batch.Delete("key1");
        batch.Put("key2", "value2");
        s = db->Write(WriteOptions(), &batch);
        ASSERT_TRUE(s.ok());
    }

    // Verify batch effects
    s = db->Get(ReadOptions(), "key1", &value);
    ASSERT_TRUE(s.IsNotFound());

    s = db->Get(ReadOptions(), "key2", &value);
    ASSERT_TRUE(s.ok());
    ASSERT_EQ(value, "value2");

    // PinnableSlice
    {
        PinnableSlice pinnable_val;
        s = db->Get(ReadOptions(), db->DefaultColumnFamily(), "key2", &pinnable_val);
        ASSERT_TRUE(s.ok());
        ASSERT_EQ(pinnable_val.ToString(), "value2");
    }

    // Non-existent key
    {
        PinnableSlice pinnable_val;
        s = db->Get(ReadOptions(), db->DefaultColumnFamily(), "nonexistent", &pinnable_val);
        ASSERT_TRUE(s.IsNotFound());
    }

    delete db;
}

TEST_F(TestDbDirect, RocksDBIterator) {
    DB* db = nullptr;
    Options options;
    options.create_if_missing = true;

    Status s = DB::Open(options, db_path_, &db);
    ASSERT_TRUE(s.ok());

    // Insert ordered keys
    db->Put(WriteOptions(), "aaa", "1");
    db->Put(WriteOptions(), "bbb", "2");
    db->Put(WriteOptions(), "ccc", "3");
    db->Put(WriteOptions(), "ddd", "4");

    // Iterate with prefix seek
    auto* iter = db->NewIterator(ReadOptions());
    iter->Seek("b");
    ASSERT_TRUE(iter->Valid());
    ASSERT_EQ(iter->key().ToString(), "bbb");
    ASSERT_EQ(iter->value().ToString(), "2");

    iter->Next();
    ASSERT_TRUE(iter->Valid());
    ASSERT_EQ(iter->key().ToString(), "ccc");

    delete iter;
    delete db;
}

}  // namespace test

}  // namespace db

}  // namespace seth
