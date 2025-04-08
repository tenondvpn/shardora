#include "timeblock/time_block_manager.h"

#include <cstdlib>

#include "block/account_manager.h"
#include "block/block_utils.h"
#include "common/global_info.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "dht/dht_key.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "protos//get_proto_hash.h"
#include "protos/prefix_db.h"
#include "protos/pools.pb.h"
#include "timeblock/time_block_utils.h"
#include "transport/tcp_transport.h"
#include "transport/transport_utils.h"
#include "vss/vss_manager.h"

namespace shardora {

namespace timeblock {

static const std::string kTimeBlockGidPrefix = common::Encode::HexDecode(
    "c575ff0d3eea61205e3433495431e312056d0d51a64c6badfd4ad8cc092b7daa");

void TimeBlockManager::Init(
        std::shared_ptr<vss::VssManager>& vss_mgr,
        std::shared_ptr<block::AccountManager>& account_mgr) {
    vss_mgr_ = vss_mgr;
    account_mgr_ = account_mgr;
}

TimeBlockManager::TimeBlockManager() {}

TimeBlockManager::~TimeBlockManager() {}

void TimeBlockManager::CreateTimeBlockTx() {
    if (create_tm_tx_cb_ == nullptr) {
        return;
    }

    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId &&
            common::GlobalInfo::Instance()->network_id() !=
            (network::kRootCongressNetworkId + network::kConsensusWaitingShardOffset)) {
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->address_info = account_mgr_->pools_address_info(common::kImmutablePoolSize);
    assert(msg_ptr->address_info != nullptr);
    assert(!msg_ptr->address_info->addr().empty());
    pools::protobuf::TxMessage& tx_info = *msg_ptr->header.mutable_tx_proto();
    tx_info.set_step(pools::protobuf::kConsensusRootTimeBlock);
    tx_info.set_pubkey("");
    tx_info.set_to(msg_ptr->address_info->addr());
    tx_info.set_nonce(msg_ptr->address_info->nonce() + 1);
    tx_info.set_gas_limit(0llu);
    tx_info.set_amount(0);
    tx_info.set_gas_price(common::kBuildinTransactionGasPrice);
    tx_info.set_key(protos::kAttrTimerBlock);
    tmblock_tx_ptr_ = create_tm_tx_cb_(msg_ptr);
    ZJC_INFO("success create timeblock tx key: %s",
        common::Encode::HexEncode(pools::GetTxKey(
            msg_ptr->address_info->addr(), 
            msg_ptr->address_info->nonce())).c_str());
}

bool TimeBlockManager::HasTimeblockTx(
        uint32_t pool_index, 
        pools::CheckAddrNonceValidFunction tx_valid_func) {
    if (pool_index != common::kImmutablePoolSize ||
            common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
        return false;
    }
    
    if (tmblock_tx_ptr_ != nullptr) {
        auto now_tm_us = common::TimeUtils::TimestampUs();
        // if (tmblock_tx_ptr_->prev_consensus_tm_us + 3000000lu > now_tm_us) {
        //     ZJC_DEBUG("tmblock_tx_ptr_->prev_consensus_tm_us + 3000000lu > now_tm_us, is leader: %d", leader);
        //     return nullptr;
        // }

        if (!CanCallTimeBlockTx()) {
            return false;
        }

        if (tx_valid_func(*tmblock_tx_ptr_->address_info, *tmblock_tx_ptr_->tx_info) != 0) {
            return false;
        }
        
        return true;
    }

    return false;
}

pools::TxItemPtr TimeBlockManager::tmblock_tx_ptr(bool leader, uint32_t pool_index) {
    if (tmblock_tx_ptr_ != nullptr) {
        auto now_tm_us = common::TimeUtils::TimestampUs();
        // if (tmblock_tx_ptr_->prev_consensus_tm_us + 3000000lu > now_tm_us) {
        //     ZJC_DEBUG("tmblock_tx_ptr_->prev_consensus_tm_us + 3000000lu > now_tm_us, is leader: %d", leader);
        //     return nullptr;
        // }

        if (!CanCallTimeBlockTx()) {
            ZJC_DEBUG("CanCallTimeBlockTx leader: %d", leader);
            return nullptr;
        }


        auto& tx_info = tmblock_tx_ptr_->tx_info;
        char data[16];
        uint64_t* u64_data = (uint64_t*)data;
        uint64_t now_tm_sec = now_tm_us / 1000000lu;
        uint64_t new_time_block_tm = latest_time_block_tm_ + common::kTimeBlockCreatePeriodSeconds;
        while (new_time_block_tm < now_tm_sec && now_tm_sec - new_time_block_tm >= 30lu) {
            new_time_block_tm += common::kTimeBlockCreatePeriodSeconds;
        }

        u64_data[0] = new_time_block_tm;
        u64_data[1] = vss_mgr_->GetConsensusFinalRandom();
        tx_info->set_value(std::string(data, sizeof(data)));
        // pool_index 一定是 256
        auto account_info = account_mgr_->pools_address_info(pool_index);
        tx_info->set_to(account_info->addr());
        tx_info->set_key(common::Hash::keccak256(tx_info->value()));
        tmblock_tx_ptr_->prev_consensus_tm_us = now_tm_us;
        ZJC_DEBUG("success create timeblock tx tm: %lu, vss: %lu, leader: %d, unique hash: %s",
            u64_data[0], u64_data[1], leader,
            common::Encode::HexEncode(tx_info->key()).c_str());
    }

    return tmblock_tx_ptr_;
}

void TimeBlockManager::OnTimeBlock(
        uint64_t latest_time_block_tm,
        uint64_t latest_time_block_height,
        uint64_t vss_random) {
    if (latest_time_block_height_ != common::kInvalidUint64 &&
            latest_time_block_height_ >= latest_time_block_height) {
        return;
    }

    ZJC_DEBUG("LeaderNewTimeBlockValid height[%lu:%lu], tm[%lu:%lu], vss[%lu]",
        latest_time_block_height,
        latest_time_block_height_,
        latest_time_block_tm,
        latest_time_block_tm_,
        vss_random);
    assert(vss_random != 0);
    latest_time_block_height_ = latest_time_block_height;
    latest_time_block_tm_ = latest_time_block_tm;
    latest_tm_block_local_sec_ = common::TimeUtils::TimestampSeconds();
    CreateTimeBlockTx();
}

}  // namespace timeblock

}  // namespace shardora
 
