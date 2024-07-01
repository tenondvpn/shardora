#pragma once
#include "common/utils.h"
#include "common/log.h"
#include "common/encode.h"

#define DB_DEBUG(fmt, ...) ZJC_DEBUG("[db]" fmt, ## __VA_ARGS__)
#define DB_INFO(fmt, ...) ZJC_INFO("[db]" fmt, ## __VA_ARGS__)
#define DB_WARN(fmt, ...) ZJC_WARN("[db]" fmt, ## __VA_ARGS__)
#define DB_ERROR(fmt, ...) ZJC_ERROR("[db]" fmt, ## __VA_ARGS__)

namespace shardora {

namespace db {

static const char kDbFieldLinkLetter = '\x01';


// class MyWriteBatchHandler : public TmpDbWriteBatch::Handler {
// public:
//     MyWriteBatchHandler() {}
//     // 重写 Handle方法，处理每个操作
//     virtual void Put(const DbSlice& key, const DbSlice& value) override {
//         ZJC_DEBUG("Put operation: Key=%s, Value=%s", 
//             common::Encode::HexEncode(key.ToString()).c_str(), common::Encode::HexEncode(value.ToString()).c_str());
//     }

//     // virtual void Merge(const DbSlice& key, const DbSlice& value) override {
//     //     ZJC_DEBUG("Merge operation: Key=%s, Value=%s", 
//     //         common::Encode::HexEncode(key.ToString()).c_str(), common::Encode::HexEncode(value.ToString()).c_str());
//     // }

//     virtual void Delete(const DbSlice& key) override {
//         ZJC_DEBUG("Delete operation: Key=%s", common::Encode::HexEncode(key.ToString()).c_str());
//     }
// };
// static const auto log_write_batch_handler = new MyWriteBatchHandler();

// static void print_write_batch(const DbWriteBatch& write_batch) {
//     DB_DEBUG("Print Write Batch");
//     write_batch.Iterate(log_write_batch_handler);
// }
}

}
