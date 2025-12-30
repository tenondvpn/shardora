#include"consensus_utils.h"
namespace shardora {

namespace consensus {

// std::string GetViewBlockHash(const view_block::protobuf::ViewBlockItem& view_block) {
//     std::string msg;
//     auto& block = view_block.block_info();
//     msg.reserve(block.tx_list_size() * 32 + 256);
//     for (int32_t i = 0; i < block.tx_list_size(); i++) {
//         msg.append(GetTxMessageHash(block.tx_list(i)));
//     }

//     msg.append(view_block.parent_hash());
//     uint64_t vss_random = block.consistency_random();
//     msg.append((char*)&vss_random, sizeof(vss_random));
//     uint64_t height = block.height();
//     msg.append((char*)&height, sizeof(height));
//     uint64_t timeblock_height = block.timeblock_height();
//     msg.append((char*)&timeblock_height, sizeof(timeblock_height));
//     uint64_t elect_height = view_block.qc().elect_height();
//     msg.append((char*)&elect_height, sizeof(elect_height));
//     uint64_t timestamp = block.timestamp();
//     msg.append((char*)&timestamp, sizeof(timestamp));  
//     uint32_t leader_idx = view_block.qc().leader_idx();
//     msg.append((char*)&leader_idx, sizeof(leader_idx));
//     if (block.change_leader_invalid_hashs_size() > 0) {
//         for (int32_t i = 0; i < block.change_leader_invalid_hashs_size(); ++i) {
//             msg.append(block.change_leader_invalid_hashs(i));
//         }
//     }

//     auto tmp_hash = common::Hash::keccak256(msg);
//     SHARDORA_DEBUG("block.prehash(): %s, height: %lu, vss_random: %lu, "
//         "timeblock_height: %lu, elect_height: %lu, leader_idx: %u, get block hash: %s, tmp_hash: %s, msg: %s, ",
//         common::Encode::HexEncode(view_block.parent_hash()).c_str(),
//         height,
//         vss_random,
//         timeblock_height,
//         elect_height,
//         leader_idx,
//         common::Encode::HexEncode(common::Hash::keccak256(msg)).c_str(),
//         common::Encode::HexEncode(tmp_hash).c_str(),
//         common::Encode::HexEncode(msg).c_str());
//     return tmp_hash;
// }

// std::string GetTxMessageHash(const block::protobuf::BlockTx& tx_info) {
//     std::string message;
//     message.reserve(tx_info.ByteSizeLong());
//     message.append(tx_info.gid());
//     message.append(tx_info.from());
//     message.append(tx_info.to());
//     uint64_t amount = tx_info.amount();
//     message.append(std::string((char*)&amount, sizeof(amount)));
//     uint64_t gas_limit = tx_info.gas_limit();
//     message.append(std::string((char*)&gas_limit, sizeof(gas_limit)));
//     uint64_t gas_price = tx_info.gas_price();
//     message.append(std::string((char*)&gas_price, sizeof(gas_price)));
//     uint32_t step = tx_info.step();
//     message.append(std::string((char*)&step, sizeof(step)));
//     for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
//         message.append(tx_info.storages(i).key());
//         message.append(tx_info.storages(i).value());
//         SHARDORA_DEBUG("add tx key: %s, %s, value: %s",
//             tx_info.storages(i).key().c_str(),
//             common::Encode::HexEncode(tx_info.storages(i).key()).c_str(),
//             common::Encode::HexEncode(tx_info.storages(i).value()).c_str());
//     }

//     for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
//         SHARDORA_DEBUG("amount: %lu, gas_limit: %lu, gas_price: %lu, step: %u, key: %s, %s, val: %s, block tx hash: %s, message: %s",
//             amount, gas_limit, gas_price, step,
//             common::Encode::HexEncode(tx_info.storages(i).key()).c_str(),
//             tx_info.storages(i).key().c_str(),
//             common::Encode::HexEncode(tx_info.storages(i).value()).c_str(),
//             common::Encode::HexEncode(common::Hash::keccak256(message)).c_str(),
//             common::Encode::HexEncode(message).c_str());
//     }

//     return common::Hash::keccak256(message);
// }

}
}