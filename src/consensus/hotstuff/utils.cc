#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/utils.h>

namespace shardora {

namespace hotstuff {

std::string GetTxMessageHash(const block::protobuf::BlockTx& tx_info, const std::string& phash) {
    std::string message;
    message.reserve(tx_info.ByteSizeLong());
    message.append(tx_info.gid());
    message.append(tx_info.from());
    message.append(tx_info.to());
    uint64_t balance = tx_info.balance();
    message.append(std::string((char*)&balance, sizeof(balance)));
    uint64_t amount = tx_info.amount();
    message.append(std::string((char*)&amount, sizeof(amount)));
    uint64_t gas_limit = tx_info.gas_limit();
    message.append(std::string((char*)&gas_limit, sizeof(gas_limit)));
    uint64_t gas_price = tx_info.gas_price();
    message.append(std::string((char*)&gas_price, sizeof(gas_price)));
    uint32_t step = tx_info.step();
    message.append(std::string((char*)&step, sizeof(step)));
    // TODO 加上 gas_used 和 status 之后 100 节点共识发生卡顿，频繁超时
    uint64_t gas_used = tx_info.gas_used();
    message.append(std::string((char*)&gas_used, sizeof(gas_used)));
    uint32_t status = tx_info.status();
    message.append(std::string((char*)&status, sizeof(status)));
    for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
        message.append(tx_info.storages(i).key());
        message.append(tx_info.storages(i).value());
        // ZJC_DEBUG("add tx key: %s, %s, val: %s",
        //     tx_info.storages(i).key().c_str(),
        //     common::Encode::HexEncode(tx_info.storages(i).key()).c_str(), 
        //     common::Encode::HexEncode(tx_info.storages(i).value()).c_str());
    }

    // ZJC_DEBUG("phash: %s, gid: %s, from: %s, to: %s, balance: %lu, amount: %lu, gas_limit: %lu, "
    //     "gas_price: %lu, step: %u, gas_used: %lu, status: %lu, block tx hash: %s, message: %s",
    //     common::Encode::HexEncode(phash).c_str(),
    //     common::Encode::HexEncode(tx_info.gid()).c_str(),
    //     common::Encode::HexEncode(tx_info.from()).c_str(),
    //     common::Encode::HexEncode(tx_info.to()).c_str(),
    //     balance, amount, gas_limit, gas_price, step,
    //     gas_used,
    //     status,
    //     common::Encode::HexEncode(common::Hash::keccak256(message)).c_str(),
    //     common::Encode::HexEncode(message).c_str());
    return common::Hash::keccak256(message);
}

std::string GetBlockHash(const view_block::protobuf::ViewBlockItem &view_block) {
    std::string msg;
    auto& block = view_block.block_info();
    msg.reserve(block.tx_list_size() * 32 + 256);
    for (int32_t i = 0; i < block.tx_list_size(); i++) {
        msg.append(GetTxMessageHash(block.tx_list(i), view_block.parent_hash()));
    }

    // ZJC_DEBUG("get block hash txs message: %s, vss_random: %lu, height: %lu, "
    //     "tm height: %lu, tm: %lu, invalid hash size: %u",
    //     common::Encode::HexEncode(msg).c_str(), 
    //     block.consistency_random(), 
    //     block.height(), 
    //     block.timeblock_height(), 
    //     block.timestamp(), 
    //     block.change_leader_invalid_hashs_size());
    uint32_t sharding_id = view_block.qc().network_id();
    msg.append((char*)&sharding_id, sizeof(sharding_id));
    uint32_t pool_index = view_block.qc().pool_index();
    msg.append((char*)&pool_index, sizeof(pool_index));
    msg.append(view_block.parent_hash());
    uint64_t vss_random = block.consistency_random();
    msg.append((char*)&vss_random, sizeof(vss_random));
    uint64_t height = block.height();
    msg.append((char*)&height, sizeof(height));
    uint64_t timeblock_height = block.timeblock_height();
    msg.append((char*)&timeblock_height, sizeof(timeblock_height));
    uint64_t timestamp = 0;  // block.timestamp(); // TODO: fix(重试导致时间戳不一致)
    msg.append((char*)&timestamp, sizeof(timestamp));  
    // msg.append(block.leader_ip());
    // msg.append((char*)&leader_port, sizeof(leader_port));
    if (block.change_leader_invalid_hashs_size() > 0) {
        for (int32_t i = 0; i < block.change_leader_invalid_hashs_size(); ++i) {
            msg.append(block.change_leader_invalid_hashs(i));
        }
    }

    // auto hash = common::Hash::keccak256(msg);
    // ZJC_DEBUG("get block hash: %s, sharding_id: %u, pool_index: %u, "
    //     "phash: %s, vss_random: %lu, height: %lu, "
    //     "timeblock_height: %lu, timestamp: %lu, msg: %s",
    //     common::Encode::HexEncode(hash).c_str(),
    //     sharding_id, pool_index, 
    //     common::Encode::HexEncode(view_block.parent_hash()).c_str(), 
    //     vss_random, height, timeblock_height, timestamp,
    //     common::Encode::HexEncode(msg).c_str());
    return common::Hash::keccak256(msg);
}

} // namespace consensus

} // namespace shardora


