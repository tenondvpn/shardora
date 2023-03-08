#include "block/account_manager.h"

#include <algorithm>

#include "contract/contract_manager.h"
#include "common/encode.h"
#include "db/db.h"
#include "elect/member_manager.h"
#include "elect/elect_manager.h"
#include "network/network_utils.h"
#include "protos/address.pb.h"
#include "protos/block.pb.h"
#include "protos/prefix_db.h" 
#include "protos/elect.pb.h"
#include "transport/transport_utils.h"

namespace zjchain {

namespace block {

AccountManager::AccountManager() {
}

AccountManager::~AccountManager() {
    if (address_map_ != nullptr) {
        delete[] address_map_;
    }
}

int AccountManager::Init(
        uint8_t thread_count,
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr) {
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    pools_mgr_ = pools_mgr;
    address_map_ = new common::UniqueMap<std::string, protos::AddressInfoPtr>[thread_count];
    for (uint32_t i = 0; i < thread_count; ++i) {
        address_map_[i].Init(1024 * 1, 16);
    }

    srand(time(NULL));
    prev_refresh_heights_tm_ = common::TimeUtils::TimestampSeconds() + rand() % 30;
//     check_missing_height_tick_.CutOff(
//         kCheckMissingHeightPeriod,
//         std::bind(&AccountManager::CheckMissingHeight, this));
//     flush_db_tick_.CutOff(
//         kFushTreeToDbPeriod,
//         std::bind(&AccountManager::FlushPoolHeightTreeToDb, this));
    refresh_pool_max_height_tick_.CutOff(
        kRefreshPoolMaxHeightPeriod,
        std::bind(&AccountManager::RefreshPoolMaxHeight, this));
    inited_ = true;
    return kBlockSuccess;
}

bool AccountManager::AccountExists(uint8_t thread_idx, const std::string& addr) {
    return GetAcountInfo(thread_idx, addr) != nullptr;
}

protos::AddressInfoPtr AccountManager::GetAcountInfoFromDb(const std::string& addr) {
    return prefix_db_->GetAddressInfo(addr);
}

protos::AddressInfoPtr AccountManager::GetAcountInfo(
        uint8_t thread_idx,
        const std::string& addr) {
    // first get from cache
    std::shared_ptr<address::protobuf::AddressInfo> address_info = nullptr;
    if (address_map_[thread_idx].get(addr, &address_info)) {
        return address_info;
    }

    // get from db and add to memory cache
    address_info = prefix_db_->GetAddressInfo(addr);
    if (address_info != nullptr) {
        address_map_[thread_idx].add(addr, address_info);
    }

    return address_info;
}

protos::AddressInfoPtr AccountManager::GetContractInfoByAddress(
        uint8_t thread_idx,
        const std::string& addr) {
    auto account_info = GetAcountInfo(thread_idx, addr);
    if (account_info == nullptr) {
        BLOCK_ERROR("get account failed[%s]", common::Encode::HexEncode(addr).c_str());
        return nullptr;
    }

    if (account_info->type() != address::protobuf::kContract) {
        return nullptr;
    }

    return account_info;
}

int AccountManager::GetAddressConsensusNetworkId(
        uint8_t thread_idx,
        const std::string& addr,
        uint32_t* network_id) {
    auto account_ptr = GetAcountInfo(thread_idx, addr);
    if (account_ptr == nullptr) {
        return kBlockAddressNotExists;
    }

    *network_id = account_ptr->sharding_id();
    return kBlockSuccess;
}
// 
// int AccountManager::HandleElectBlock(
//         uint64_t height,
//         const block::protobuf::BlockTx& tx_info,
//         db::DbWriteBach& db_batch) {
//     elect::protobuf::ElectBlock elect_block;
//     for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
//         if (tx_info.storages(i).key() == elect::kElectNodeAttrElectBlock) {
//             elect_block.ParseFromString(tx_info.storages(i).val_hash());
//         }
//     }
// 
//     if (!elect_block.IsInitialized()) {
//         return kBlockSuccess;
//     }
// 
//     elect::ElectManager::Instance()->OnNewElectBlock(height, elect_block);
//     // vss::VssManager::Instance()->OnElectBlock(elect_block.shard_network_id(), height);
//     if (elect_block.prev_members().bls_pubkey_size() > 0) {
//         std::string key = GetElectBlsMembersKey(
//             elect_block.prev_members().prev_elect_height(),
//             elect_block.shard_network_id());
//         db_batch.Put(key, elect_block.prev_members().SerializeAsString());
//     }
// 
//     return kBlockSuccess;
// }
// 
// int AccountManager::HandleRootSingleBlockTx(
//         uint64_t height,
//         const block::protobuf::BlockTx& tx_info,
//         db::DbWriteBach& db_batch) {
//     switch (tx_info.type()) {
//     case common::kConsensusRootElectShard:
//         return HandleElectBlock(height, tx_info, db_batch);
//     case common::kConsensusRootTimeBlock:
//         return HandleTimeBlock(height, tx_info);
//     default:
//         break;
//     }
// 
//     return kBlockSuccess;
// }
// 
// int AccountManager::HandleFinalStatisticBlock(
//         uint64_t height,
//         const block::protobuf::BlockTx& tx_info) {
//     if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
//         // add elect root transaction
//         auto tx_ptr = std::make_shared<pools::TxItem>();
//         tx_ptr->msg_ptr = std::make_shared<transport::TransportMessage>();
//         auto& elect_tx = *tx_ptr->msg_ptr->header.mutable_tx_proto();
//         if (elect::ElectManager::Instance()->CreateElectTransaction(
//                 tx_info.network_id(),
//                 height,
//                 tx_info,
//                 elect_tx) != elect::kElectSuccess) {
//             ZJC_ERROR("create elect transaction error!");
//         }
// 
//         if (pools_mgr_->Add(tx_ptr) != pools::kPoolsSuccess) {
//             ZJC_ERROR("dispatch pool failed!");
//         }
//     }
// 
//     return kBlockSuccess;
// }

protos::AddressInfoPtr AccountManager::GetAcountInfo(
        const std::shared_ptr<block::protobuf::Block>& block_item,
        const block::protobuf::BlockTx& tx_info) {
    std::string account_id = GetTxValidAddress(tx_info);
    auto account_info = std::make_shared<address::protobuf::AddressInfo>();
    account_info->set_pool_index(block_item->pool_index());
    account_info->set_addr(account_id);
    account_info->set_type(address::protobuf::kNormal);
    account_info->set_sharding_id(block_item->network_id());
    account_info->set_latest_height(block_item->height());
    account_info->set_balance(tx_info.balance());
    return account_info;
}

std::string AccountManager::GetTxValidAddress(const block::protobuf::BlockTx& tx_info) {
    if (tx_info.step() == pools::protobuf::kNormalTo ||
        tx_info.step() == pools::protobuf::kConsensusRootElectShard ||
        tx_info.step() == pools::protobuf::kConsensusRootTimeBlock ||
        tx_info.step() == pools::protobuf::kConsensusFinalStatistic ||
        tx_info.step() == pools::protobuf::kConsensusCreateGenesisAcount) {
        return tx_info.to();
    } else {
        return tx_info.from();
    }
}

void AccountManager::HandleNormalFromTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBach& db_batch) {
    std::string account_id = GetTxValidAddress(tx);
    auto account_info = GetAcountInfo(thread_idx, account_id);
    if (account_info == nullptr) {
        assert(false);
        return;
    }

    account_info->set_latest_height(block.height());
    account_info->set_balance(tx.balance());
    prefix_db_->AddAddressInfo(account_id, *account_info, db_batch);
//     ZJC_DEBUG("transfer from address new balance %s: %lu",
//         common::Encode::HexEncode(account_id).c_str(), tx.balance());
}

void AccountManager::HandleLocalToTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBach& db_batch) {
    std::string to_txs_str;
    if (!prefix_db_->GetTemporaryKv(tx.storages(1).val_hash(), &to_txs_str)) {
        return;
    }

    block::protobuf::ConsensusToTxs to_txs;
    if (!to_txs.ParseFromString(to_txs_str)) {
        return;
    }

    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        auto account_info = GetAcountInfo(thread_idx, to_txs.tos(i).to());
        if (account_info == nullptr) {
            account_info = std::make_shared<address::protobuf::AddressInfo>();
            account_info->set_pool_index(block.pool_index());
            account_info->set_addr(to_txs.tos(i).to());
            account_info->set_type(address::protobuf::kNormal);
            account_info->set_sharding_id(block.network_id());
            address_map_[thread_idx].add(to_txs.tos(i).to(), account_info);
        }

        account_info->set_latest_height(block.height());
        account_info->set_balance(to_txs.tos(i).balance());
        prefix_db_->AddAddressInfo(to_txs.tos(i).to(), *account_info, db_batch);
//         ZJC_DEBUG("transfer to address new balance %s: %lu",
//             common::Encode::HexEncode(to_txs.tos(i).to()).c_str(), to_txs.tos(i).balance());
    }
}

void AccountManager::AddBlockItemToCache(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        db::DbWriteBach& db_batch) {
    const auto& tx_list = block_item->tx_list();
    if (tx_list.empty()) {
        return;
    }
    
    // one block must be one consensus pool
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        switch (tx_list[i].step()) {
        case pools::protobuf::kNormalFrom:
            HandleNormalFromTx(thread_idx, *block_item, tx_list[i], db_batch);
            break;
        case pools::protobuf::kConsensusLocalTos:
            HandleLocalToTx(thread_idx, *block_item, tx_list[i], db_batch);
            break;
        default:
//             ZJC_DEBUG("not handled step: %d", tx_list[i].step());
            break;
        }
    }
}

int AccountManager::AddNewAccount(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx_info) {
    auto& account_id = tx_info.to();
    if (AccountExists(thread_idx, account_id)) {
        return kBlockSuccess;
    }

    auto account_info = std::make_shared<address::protobuf::AddressInfo>();
    switch (tx_info.step()) {
    case pools::protobuf::kNormalTo:
        if (tx_info.amount() == 0) {
            ZJC_ERROR("invalid tx amount: %lu, step: %d", tx_info.amount(), tx_info.step());
            return kBlockSuccess;
        }

        account_info->set_type(address::protobuf::kNormal);
        break;
    case common::kConsensusCreateContract: {
        account_info->set_type(address::protobuf::kContract);
        for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
            if (tx_info.storages(i).key() == protos::kContractCreatedBytesCode) {
                account_info->set_bytes_code(tx_info.storages(i).val_hash());
            }
        }

        break;
    }
    case pools::protobuf::kConsensusCreateGenesisAcount:
        account_info->set_type(address::protobuf::kNormal);
        break;
    case pools::protobuf::kConsensusFinalStatistic:
        account_info->set_type(address::protobuf::kStatistic);
        break;
    case pools::protobuf::kConsensusRootTimeBlock:
        account_info->set_type(address::protobuf::kRootTimer);
        break;
    case pools::protobuf::kConsensusRootElectShard:
        account_info->set_type(address::protobuf::kRootElect);
        break;
    default:
        return kBlockError;
    }

    account_info->set_sharding_id(block.network_id());
    account_info->set_addr(tx_info.to());
    account_info->set_pool_index(block.pool_index());
    account_info->set_latest_height(block.height());
    account_info->set_balance(tx_info.balance());
    address_map_[thread_idx].add(account_id, account_info);
//     ZJC_DEBUG("add account success: %s", common::Encode::HexEncode(account_id).c_str());
    return kBlockSuccess;
}

int AccountManager::UpdateAccountInfo(
        uint8_t thread_idx,
        const std::string& account_id,
        const block::protobuf::BlockTx& tx_info,
        const std::shared_ptr<block::protobuf::Block>& block_item,
        uint32_t* pool_index) {
    if (tx_info.status() != 0 || tx_info.step() != pools::protobuf::kNormalTo) {
        if (tx_info.step() != common::kConsensusCallContract &&
            tx_info.step() != common::kConsensusCreateContract) {
            return kBlockSuccess;
        }
    }

    std::shared_ptr<address::protobuf::AddressInfo> account_info = nullptr;
    address_map_[thread_idx].get(account_id, &account_info);
    if (account_info == nullptr) {
        AddNewAccount(thread_idx, *block_item, tx_info);
        return kBlockError;
    }

    account_info->set_latest_height(block_item->height());
    account_info->set_balance(tx_info.balance());
    return kBlockSuccess;
}

void AccountManager::RefreshPoolMaxHeight() {
    SendRefreshHeightsRequest();
    refresh_pool_max_height_tick_.CutOff(
        kRefreshPoolMaxHeightPeriod,
        std::bind(&AccountManager::RefreshPoolMaxHeight, this));
}

void AccountManager::SendRefreshHeightsRequest() {
//     transport::protobuf::Header msg;
//     dht::BaseDhtPtr dht = nullptr;
//     uint32_t des_net_id = common::GlobalInfo::Instance()->network_id();
//     dht = network::UniversalManager::Instance()->GetUniversal(network::kUniversalNetworkId);
//     if (des_net_id >= network::kConsensusShardEndNetworkId) {
//         des_net_id -= network::kConsensusWaitingShardOffset;
//     }
// 
//     msg.set_src_sharding_id(dht->local_node()->sharding_id);
//     dht::DhtKeyManager dht_key(des_net_id);
//     msg.set_des_dht_key(dht_key.StrKey());
//     msg.set_type(common::kBlockMessage);
//     block::protobuf::BlockMessage block_msg;
//     auto ref_hegihts_req = block_msg.mutable_ref_heights_req();
//     for (uint32_t i = 0; i <= common::kImmutablePoolSize; ++i) {
//         uint64_t height = 0;
//         block_pools_[i]->GetHeight(&height);
//         ref_hegihts_req->add_heights(height);
//     }
// 
//     msg.set_data(block_msg.SerializeAsString());
//     dht->RandomSend(msg);
}

void AccountManager::SendRefreshHeightsResponse(const transport::protobuf::Header& header) {
//     transport::protobuf::Header msg;
//     msg.set_src_dht_key(header.des_dht_key());
//     msg.set_des_dht_key(header.src_dht_key());
//     msg.set_priority(transport::kTransportPriorityMiddle);
//     msg.set_id(common::GlobalInfo::Instance()->MessageId());
//     msg.set_universal(false);
//     msg.set_type(common::kBlockMessage);
//     msg.set_hop_count(0);
//     msg.set_client(false);
//     block::protobuf::BlockMessage block_msg;
//     auto ref_hegihts_req = block_msg.mutable_ref_heights_res();
//     for (uint32_t i = 0; i <= common::kImmutablePoolSize; ++i) {
//         uint64_t height = 0;
//         block_pools_[i]->GetHeight(&height);
//         ref_hegihts_req->add_heights(height);
//     }
// 
//     msg.set_data(block_msg.SerializeAsString());
//     transport::MultiThreadHandler::Instance()->tcp_transport()->Send(
//         header.from_ip(), header.from_port(), 0, msg);
}

int AccountManager::HandleRefreshHeightsReq(const transport::MessagePtr& msg_ptr) {
//     if (!inited_) {
//         return kBlockSuccess;
//     }
// 
//     for (int32_t i = 0; i < block_msg.ref_heights_req().heights_size(); ++i) {
//         block_pools_[i]->SetMaxHeight(block_msg.ref_heights_req().heights(i));
//     }
// 
//     SendRefreshHeightsResponse(header);
    return kBlockSuccess;
}

int AccountManager::HandleRefreshHeightsRes(const transport::MessagePtr& msg_ptr) {
    
    return kBlockSuccess;
}

}  // namespace block

}  //namespace zjchain
