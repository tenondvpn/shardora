#include <stdlib.h>
#include <math.h>

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <filesystem>

#include <gtest/gtest.h>

#define private public
#include "db/db.h"

namespace shardora {

namespace db {

namespace test {

class TestDb : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}

    virtual void SetUp() {
        db_path_ = "/tmp/shardora_db_test_" + std::to_string(test_counter_++);
        db_ = std::make_shared<Db>();
        ASSERT_TRUE(db_->Init(db_path_));
    }

    virtual void TearDown() {
        db_->Destroy();
        db_.reset();
        std::filesystem::remove_all(db_path_);
    }

protected:
    std::string db_path_;
    std::shared_ptr<Db> db_;
    static int test_counter_;
};

int TestDb::test_counter_ = 0;

// --- Basic Put/Get/Delete Tests ---

TEST_F(TestDb, PutAndGet) {
    auto st = db_->Put("key1", "value1");
    ASSERT_TRUE(st.ok());

    std::string value;
    st = db_->Get("key1", &value);
    ASSERT_TRUE(st.ok());
    ASSERT_EQ(value, "value1");
}

TEST_F(TestDb, PutWithCharPointer) {
    const char* data = "binary\x00data\x01here";
    size_t len = 16;
    auto st = db_->Put("binkey", data, len);
    ASSERT_TRUE(st.ok());

    std::string value;
    st = db_->Get("binkey", &value);
    ASSERT_TRUE(st.ok());
    ASSERT_EQ(value.size(), len);
    ASSERT_EQ(memcmp(value.c_str(), data, len), 0);
}

TEST_F(TestDb, GetNonExistentKey) {
    std::string value;
    auto st = db_->Get("nonexistent", &value);
    ASSERT_FALSE(st.ok());
}

TEST_F(TestDb, DeleteKey) {
    db_->Put("key_to_delete", "value");
    ASSERT_TRUE(db_->Exist("key_to_delete"));

    auto st = db_->Delete("key_to_delete");
    ASSERT_TRUE(st.ok());
    ASSERT_FALSE(db_->Exist("key_to_delete"));

    std::string value;
    st = db_->Get("key_to_delete", &value);
    ASSERT_FALSE(st.ok());
}

TEST_F(TestDb, DeleteNonExistentKey) {
    // Deleting a non-existent key should not fail
    auto st = db_->Delete("never_existed");
    ASSERT_TRUE(st.ok());
}

TEST_F(TestDb, ExistKey) {
    ASSERT_FALSE(db_->Exist("mykey"));
    db_->Put("mykey", "myvalue");
    ASSERT_TRUE(db_->Exist("mykey"));
    db_->Delete("mykey");
    ASSERT_FALSE(db_->Exist("mykey"));
}

// --- Overwrite Tests ---

TEST_F(TestDb, OverwriteValue) {
    db_->Put("key", "original");
    std::string value;
    db_->Get("key", &value);
    ASSERT_EQ(value, "original");

    db_->Put("key", "updated");
    db_->Get("key", &value);
    ASSERT_EQ(value, "updated");
}

TEST_F(TestDb, OverwriteWithEmpty) {
    db_->Put("key", "notempty");
    db_->Put("key", "");

    std::string value;
    auto st = db_->Get("key", &value);
    ASSERT_TRUE(st.ok());
    ASSERT_EQ(value, "");
}

// --- Empty Key/Value Tests ---

TEST_F(TestDb, EmptyValue) {
    auto st = db_->Put("empty_val_key", "");
    ASSERT_TRUE(st.ok());

    std::string value;
    st = db_->Get("empty_val_key", &value);
    ASSERT_TRUE(st.ok());
    ASSERT_EQ(value, "");
    ASSERT_TRUE(db_->Exist("empty_val_key"));
}

TEST_F(TestDb, EmptyKey) {
    auto st = db_->Put("", "value_for_empty_key");
    ASSERT_TRUE(st.ok());

    std::string value;
    st = db_->Get("", &value);
    ASSERT_TRUE(st.ok());
    ASSERT_EQ(value, "value_for_empty_key");
}

// --- Multiple Keys Tests ---

TEST_F(TestDb, MultipleKeys) {
    const int count = 100;
    for (int i = 0; i < count; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::string val = "value_" + std::to_string(i);
        auto st = db_->Put(key, val);
        ASSERT_TRUE(st.ok());
    }

    for (int i = 0; i < count; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::string expected = "value_" + std::to_string(i);
        std::string value;
        auto st = db_->Get(key, &value);
        ASSERT_TRUE(st.ok());
        ASSERT_EQ(value, expected);
    }
}

// --- Large Value Tests ---

TEST_F(TestDb, LargeValue) {
    std::string large_value(1024 * 1024, 'X');  // 1MB
    auto st = db_->Put("large_key", large_value);
    ASSERT_TRUE(st.ok());

    std::string value;
    st = db_->Get("large_key", &value);
    ASSERT_TRUE(st.ok());
    ASSERT_EQ(value.size(), large_value.size());
    ASSERT_EQ(value, large_value);
}

// --- ClearPrefix Tests ---

TEST_F(TestDb, ClearPrefix) {
    db_->Put("prefix_aaa", "1");
    db_->Put("prefix_bbb", "2");
    db_->Put("prefix_ccc", "3");
    db_->Put("other_key", "4");

    db_->ClearPrefix("prefix_");

    ASSERT_FALSE(db_->Exist("prefix_aaa"));
    ASSERT_FALSE(db_->Exist("prefix_bbb"));
    ASSERT_FALSE(db_->Exist("prefix_ccc"));
    ASSERT_TRUE(db_->Exist("other_key"));
}

TEST_F(TestDb, ClearPrefixNoMatch) {
    db_->Put("key1", "val1");
    db_->Put("key2", "val2");

    db_->ClearPrefix("nonexistent_prefix_");

    // Nothing should be deleted
    ASSERT_TRUE(db_->Exist("key1"));
    ASSERT_TRUE(db_->Exist("key2"));
}

TEST_F(TestDb, ClearPrefixEmpty) {
    // ClearPrefix with empty prefix on empty db should not crash
    db_->ClearPrefix("anything");
}

// --- GetAllPrefix Tests ---

TEST_F(TestDb, GetAllPrefix) {
    db_->Put("user:001", "alice");
    db_->Put("user:002", "bob");
    db_->Put("user:003", "charlie");
    db_->Put("item:001", "sword");
    db_->Put("item:002", "shield");

    std::map<std::string, std::string> result;
    db_->GetAllPrefix("user:", result);

    ASSERT_EQ(result.size(), 3u);
    ASSERT_EQ(result["user:001"], "alice");
    ASSERT_EQ(result["user:002"], "bob");
    ASSERT_EQ(result["user:003"], "charlie");
}

TEST_F(TestDb, GetAllPrefixNoMatch) {
    db_->Put("key1", "val1");

    std::map<std::string, std::string> result;
    db_->GetAllPrefix("nonexistent:", result);

    ASSERT_TRUE(result.empty());
}

TEST_F(TestDb, GetAllPrefixAll) {
    db_->Put("abc_1", "v1");
    db_->Put("abc_2", "v2");
    db_->Put("abc_3", "v3");

    std::map<std::string, std::string> result;
    db_->GetAllPrefix("abc_", result);

    ASSERT_EQ(result.size(), 3u);
}

// --- Init/Destroy Lifecycle Tests ---

TEST_F(TestDb, DoubleInitFails) {
    // db_ is already initialized in SetUp
    ASSERT_FALSE(db_->Init(db_path_));
}

TEST_F(TestDb, DestroyAndReinit) {
    db_->Put("before_destroy", "value");
    db_->Destroy();

    // Reinit should work
    ASSERT_TRUE(db_->Init(db_path_));

    // Data should persist (RocksDB is persistent)
    std::string value;
    auto st = db_->Get("before_destroy", &value);
    ASSERT_TRUE(st.ok());
    ASSERT_EQ(value, "value");
}

// --- Binary Data Tests ---

TEST_F(TestDb, BinaryKeyAndValue) {
    std::string binary_key(32, '\0');
    binary_key[0] = '\x01';
    binary_key[15] = '\xFF';
    binary_key[31] = '\xAB';

    std::string binary_value(64, '\0');
    binary_value[0] = '\xDE';
    binary_value[63] = '\xAD';

    auto st = db_->Put(binary_key, binary_value);
    ASSERT_TRUE(st.ok());

    std::string value;
    st = db_->Get(binary_key, &value);
    ASSERT_TRUE(st.ok());
    ASSERT_EQ(value, binary_value);
}

}  // namespace test

}  // namespace db

}  // namespace shardora
