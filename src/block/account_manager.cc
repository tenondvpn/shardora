#include "block/account_manager.h"

#include <algorithm>
#include <common/utils.h>
#include <consensus/consensus_utils.h>
#include <protos/pools.pb.h>

#include "contract/contract_manager.h"
#include "common/encode.h"
#include "db/db.h"
#include "elect/elect_manager.h"
#include "network/network_utils.h"
#include "protos/address.pb.h"
#include "protos/block.pb.h"
#include "protos/prefix_db.h" 
#include "protos/elect.pb.h"
#include "transport/transport_utils.h"

namespace shardora {

namespace block {

AccountManager::AccountManager() {
}

AccountManager::~AccountManager() {
    destroy_ = true;
    if (update_acc_thread_ != nullptr) {
        update_acc_thread_->join();
        update_acc_thread_ = nullptr;
    }
}

int AccountManager::Init(
        std::shared_ptr<db::Db>& db,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr) {
    db_ = db;
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    pools_mgr_ = pools_mgr;
    CreatePoolsAddressInfo();
    inited_ = true;
    update_acc_thread_ = std::make_shared<std::thread>(
        std::bind(&AccountManager::RunUpdateAccounts, this));
    std::unique_lock<std::mutex> lock(thread_wait_mutex_);
    thread_wait_conn_.wait_for(lock, std::chrono::milliseconds(1000));
    return kBlockSuccess;
}

// 网络中每个 pool 都有个 address
void AccountManager::CreatePoolsAddressInfo() {
    root_pool_address_info_ = std::make_shared<address::protobuf::AddressInfo>();
    root_pool_address_info_->set_pubkey("");
    root_pool_address_info_->set_balance(0);
    root_pool_address_info_->set_sharding_id(-1);
    root_pool_address_info_->set_pool_index(common::kRootChainPoolIndex);
    root_pool_address_info_->set_addr(common::kRootPoolsAddress);
    root_pool_address_info_->set_type(address::protobuf::kToTxAddress);
    root_pool_address_info_->set_latest_height(0);
    uint32_t i = 0;
    std::unordered_set<uint32_t> pool_idx_set;
    // 这只是为了随机分配个 addr 给 pool，但这个 addr 必须和 pool 之间有 GetAddressPoolIndex 的关系，所以遍历着去找
    // pool_address_info_ 中存有 257 个 pool address
    for (uint32_t i = 0; i < common::kInvalidUint32; ++i) {
        std::string addr = common::kRootPoolsAddress;
        uint32_t* tmp_data = (uint32_t*)addr.data();
        tmp_data[0] = i;
        auto pool_idx = common::GetAddressPoolIndex(addr);

        if (pool_idx_set.size() > common::kImmutablePoolSize) {
            break;
        }

        auto iter = pool_idx_set.find(pool_idx);
        if (iter != pool_idx_set.end()) {
            continue;
        }

        pool_address_info_[pool_idx] = std::make_shared<address::protobuf::AddressInfo>();
        pool_address_info_[pool_idx]->set_pubkey("");
        pool_address_info_[pool_idx]->set_balance(0);
        pool_address_info_[pool_idx]->set_sharding_id(-1);
        pool_address_info_[pool_idx]->set_pool_index(pool_idx);
        pool_address_info_[pool_idx]->set_addr(addr);
        pool_address_info_[pool_idx]->set_type(address::protobuf::kToTxAddress);
        pool_address_info_[pool_idx]->set_latest_height(0);
        
        pool_idx_set.insert(pool_idx);
    }
}

bool AccountManager::AccountExists(const std::string& addr) {
    return GetAccountInfo(addr) != nullptr;
}

protos::AddressInfoPtr AccountManager::GetAcountInfoFromDb(const std::string& addr) {
    return prefix_db_->GetAddressInfo(addr);
}

protos::AddressInfoPtr AccountManager::GetAccountInfo(const std::string& addr) {
    protos::AddressInfoPtr addr_info = account_lru_map_.get(addr);
    if (addr_info != nullptr) {
        return addr_info;
    }

    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    addr_info = GetAcountInfoFromDb(addr);
    if (!addr_info) {
        BLOCK_DEBUG(
            "get account failed[%s] in thread_idx:%d", 
            common::Encode::HexEncode(addr).c_str(), thread_idx);
    } else {
        thread_update_accounts_queue_[thread_idx].push(addr_info);
        update_acc_con_.notify_one();
    }

    return addr_info;
}

protos::AddressInfoPtr AccountManager::GetContractInfoByAddress(
        const std::string& addr) {
    auto account_info = GetAccountInfo(addr);
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
        const std::string& addr,
        uint32_t* network_id) {
    auto account_ptr = GetAccountInfo(addr);
    if (account_ptr == nullptr) {
        return kBlockAddressNotExists;
    }

    *network_id = account_ptr->sharding_id();
    return kBlockSuccess;
}

const std::string& AccountManager::GetTxValidAddress(const block::protobuf::BlockTx& tx_info) {
    if (pools::IsTxUseFromAddress(tx_info.step())) {
        return tx_info.from();
    } else {
        return tx_info.to();
    }
}

void AccountManager::HandleNormalFromTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& block = view_block.block_info();
    auto& account_id = GetTxValidAddress(tx);
    auto account_info = GetAccountInfo(account_id);
    if (account_info == nullptr) {
        ZJC_INFO("get address info failed create new address to this id: %s,"
            "shard: %u, local shard: %u",
            common::Encode::HexEncode(account_id).c_str(), view_block.qc().network_id(),
            common::GlobalInfo::Instance()->network_id());
        account_info = std::make_shared<address::protobuf::AddressInfo>();
        account_info->set_pool_index(view_block.qc().pool_index());
        account_info->set_addr(account_id);
        account_info->set_type(address::protobuf::kNormal);
        account_info->set_sharding_id(view_block.qc().network_id());
    }

    if (account_info->latest_height() > block.height()) {
        return;
    }

    account_info->set_latest_height(block.height());
    account_info->set_balance(tx.balance());
    prefix_db_->AddAddressInfo(account_id, *account_info, db_batch);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    thread_update_accounts_queue_[thread_idx].push(account_info);
    update_acc_con_.notify_one();
    ZJC_DEBUG("transfer from address new balance %s: %lu, height: %lu, pool: %u",
        common::Encode::HexEncode(account_id).c_str(), tx.balance(),
        block.height(), view_block.qc().pool_index());
}

void AccountManager::HandleCreateGenesisAcount(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& block = view_block.block_info();
    auto& account_id = tx.to();
    auto account_info = GetAccountInfo(account_id);
    if (account_info == nullptr) {
        ZJC_INFO("get address info failed create new address to this id: %s,"
            "shard: %u, local shard: %u",
            common::Encode::HexEncode(account_id).c_str(), view_block.qc().network_id(),
            common::GlobalInfo::Instance()->network_id());
        account_info = std::make_shared<address::protobuf::AddressInfo>();
        account_info->set_pool_index(view_block.qc().pool_index());
        account_info->set_addr(account_id);
        account_info->set_type(address::protobuf::kNormal);
        account_info->set_sharding_id(view_block.qc().network_id());
    }

    if (account_info->latest_height() > block.height()) {
        return;
    }

    account_info->set_latest_height(block.height());
    account_info->set_balance(tx.balance());
    prefix_db_->AddAddressInfo(account_id, *account_info, db_batch);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    thread_update_accounts_queue_[thread_idx].push(account_info);
    update_acc_con_.notify_one();
    ZJC_DEBUG("transfer from address new balance %s: %lu, height: %lu, pool: %u",
        common::Encode::HexEncode(account_id).c_str(), tx.balance(),
        block.height(), view_block.qc().pool_index());
}

void AccountManager::HandleContractPrepayment(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& block = view_block.block_info();
    auto& account_id = GetTxValidAddress(tx);
    ZJC_DEBUG("HandleContractPrepayment address coming from: %s, to: %s, amount:%lu, balance: %lu",
        common::Encode::HexEncode(tx.from()).c_str(),
        common::Encode::HexEncode(tx.to()).c_str(),
        tx.amount(),
        tx.balance());
    auto account_info = GetAccountInfo(account_id);
    if (account_info == nullptr) {
        assert(false);
        return;
    }

    if (account_info->latest_height() > block.height()) {
        return;
    }

    account_info->set_latest_height(block.height());
    account_info->set_balance(tx.balance());
    prefix_db_->AddAddressInfo(account_id, *account_info, db_batch);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    thread_update_accounts_queue_[thread_idx].push(account_info);
    update_acc_con_.notify_one();
    ZJC_INFO("contract prepayment address new balance %s: %lu, height: %lu, pool: %u",
        common::Encode::HexEncode(account_id).c_str(), tx.balance(),
        block.height(), view_block.qc().pool_index());
}

void AccountManager::HandleLocalToTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& block = view_block.block_info();
    if (tx.status() != consensus::kConsensusSuccess) {
        return;
    }

    const std::string* to_txs_str = nullptr;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kConsensusLocalNormalTos) {
            to_txs_str = &tx.storages(i).value();
            break;
        }
    }

    if (to_txs_str == nullptr) {
        ZJC_WARN("get local tos info failed!");
        return;
    }

    block::protobuf::ConsensusToTxs to_txs;
    if (!to_txs.ParseFromString(*to_txs_str)) {
        assert(false);
        return;
    }

    for (int32_t i = 0; i < to_txs.tos_size(); ++i) {
        if (to_txs.tos(i).to().size() != security::kUnicastAddressLength * 2 &&
                to_txs.tos(i).to().size() != security::kUnicastAddressLength) {
            //assert(false);
            ZJC_DEBUG("invalid address coming to: %s, balance: %lu",
                common::Encode::HexEncode(to_txs.tos(i).to()).c_str(),
                to_txs.tos(i).balance());
            continue;
        }

        auto account_info = GetAccountInfo(to_txs.tos(i).to());
        if (account_info == nullptr) {
            ZJC_INFO("0 get address info failed create new address to this id: %s,"
                "shard: %u, local shard: %u",
                common::Encode::HexEncode(to_txs.tos(i).to()).c_str(), view_block.qc().network_id(),
                common::GlobalInfo::Instance()->network_id());
            account_info = std::make_shared<address::protobuf::AddressInfo>();
            account_info->set_pool_index(view_block.qc().pool_index());
            account_info->set_addr(to_txs.tos(i).to());
            if (to_txs.tos(i).to().size() != security::kUnicastAddressLength * 2) {
                account_info->set_type(address::protobuf::kContractPrepayment);
            } else {
                account_info->set_type(address::protobuf::kNormal);
            }

            account_info->set_sharding_id(view_block.qc().network_id());
            account_info->set_latest_height(block.height());
            account_info->set_balance(to_txs.tos(i).balance());
            prefix_db_->AddAddressInfo(to_txs.tos(i).to(), *account_info, db_batch);
        } else {
            if (account_info->latest_height() > block.height()) {
                return;
            }

            account_info->set_latest_height(block.height());
            account_info->set_balance(to_txs.tos(i).balance());
            prefix_db_->AddAddressInfo(to_txs.tos(i).to(), *account_info, db_batch);
        }

        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        thread_update_accounts_queue_[thread_idx].push(account_info);
        update_acc_con_.notify_one();
        ZJC_INFO("transfer to address new balance %s: %lu",
            common::Encode::HexEncode(to_txs.tos(i).to()).c_str(), to_txs.tos(i).balance());
    }
}

void AccountManager::HandleContractCreateByRootTo(
		const view_block::protobuf::ViewBlockItem& view_block,
		const block::protobuf::BlockTx& tx,
		db::DbWriteBatch& db_batch) {
    auto& block = view_block.block_info();
	ZJC_DEBUG("create contract by root to: %s, status: %d, sharding: %u, pool index: %u, contract_code: %s",
		common::Encode::HexEncode(tx.to()).c_str(),
		tx.status(),
		view_block.qc().network_id(),
		view_block.qc().pool_index(),
		tx.contract_code().c_str());
	
	if (tx.status() != consensus::kConsensusSuccess) {
		return;
	}

	auto account_info = GetAccountInfo(tx.to());
	if (account_info != nullptr) {
		return;
	}
        
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
	for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kCreateContractBytesCode) {
            account_info = std::make_shared<address::protobuf::AddressInfo>();
            auto& bytes_code = tx.storages(i).value();
            account_info->set_type(address::protobuf::kContract);
            account_info->set_pool_index(view_block.qc().pool_index());
            account_info->set_addr(tx.to());
            account_info->set_sharding_id(view_block.qc().network_id());
            account_info->set_latest_height(block.height());
            account_info->set_balance(tx.amount());
            account_info->set_bytes_code(bytes_code);
            prefix_db_->AddAddressInfo(tx.to(), *account_info, db_batch);
            thread_update_accounts_queue_[thread_idx].push(account_info);
            update_acc_con_.notify_one();
            ZJC_INFO("create add local contract direct: %s, amount: %lu, sharding: %u, pool index: %u",
                common::Encode::HexEncode(tx.to()).c_str(),
                tx.amount(),
                view_block.qc().network_id(),
                view_block.qc().pool_index());
            break;
		}
	}
}

void AccountManager::HandleCreateContract(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    // handle from
    ZJC_DEBUG("HandleCreateContract address coming from: %s, to: %s, amount:%lu, balance: %lu",
        common::Encode::HexEncode(tx.from()).c_str(),
        common::Encode::HexEncode(tx.to()).c_str(),
        tx.amount(),
        tx.balance());
    auto& block = view_block.block_info();
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    {
        auto account_info = GetAccountInfo(tx.from());
        if (account_info == nullptr) {
            ZJC_INFO("0 get address info failed create new address to this id: %s,"
                "shard: %u, local shard: %u",
                common::Encode::HexEncode(tx.from()).c_str(), view_block.qc().network_id(),
                common::GlobalInfo::Instance()->network_id());
            account_info = std::make_shared<address::protobuf::AddressInfo>();
            account_info->set_pool_index(view_block.qc().pool_index());
            account_info->set_addr(tx.from());
            account_info->set_type(address::protobuf::kNormal);
            account_info->set_sharding_id(view_block.qc().network_id());
            account_info->set_latest_height(block.height());
            account_info->set_balance(tx.balance());
            prefix_db_->AddAddressInfo(tx.from(), *account_info, db_batch);
            thread_update_accounts_queue_[thread_idx].push(account_info);
        } else {
            if (account_info->latest_height() < block.height()) {
                account_info->set_latest_height(block.height());
                account_info->set_balance(tx.balance());
                prefix_db_->AddAddressInfo(tx.from(), *account_info, db_batch);
                thread_update_accounts_queue_[thread_idx].push(account_info);
            }
        }

        update_acc_con_.notify_one();
    }

    if (tx.status() == consensus::kConsensusSuccess) {
        auto account_info = GetAccountInfo(tx.to());
        if (account_info != nullptr) {
            // assert(false);
            return;
        }

        for (int32_t i = 0; i < tx.storages_size(); ++i) {
            if (tx.storages(i).key() == protos::kCreateContractBytesCode) {
                account_info = std::make_shared<address::protobuf::AddressInfo>();
                auto& bytes_code = tx.storages(i).value();
                account_info->set_type(address::protobuf::kWaitingRootConfirm);
                account_info->set_pool_index(view_block.qc().pool_index());
                account_info->set_addr(tx.to());
                account_info->set_sharding_id(view_block.qc().network_id());
                account_info->set_latest_height(block.height());
                account_info->set_balance(tx.amount());
                account_info->set_bytes_code(bytes_code);
                prefix_db_->AddAddressInfo(tx.to(), *account_info, db_batch);
                thread_update_accounts_queue_[thread_idx].push(account_info);
                ZJC_INFO("1 get address info failed create new address to this id: %s,"
                    "shard: %u, local shard: %u",
                    common::Encode::HexEncode(tx.to()).c_str(), view_block.qc().network_id(),
                    common::GlobalInfo::Instance()->network_id());

                ZJC_DEBUG("create add contract direct: %s, amount: %lu, sharding: %u, pool index: %u",
                    common::Encode::HexEncode(tx.to()).c_str(),
                    tx.amount(),
                    view_block.qc().network_id(),
                    view_block.qc().pool_index());
                break;
            }
        }
                
        update_acc_con_.notify_one();
    }
}

void AccountManager::HandleCreateContractByRootFrom(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    // handle from
    // 只处理 from 账户，合约账户需要 root 分配 shard，在该 shard 中执行 ConsensusLocalTos 交易来创建   
    auto& block = view_block.block_info();
    auto& account_id = GetTxValidAddress(tx);
    auto account_info = GetAccountInfo(account_id);
    if (account_info == nullptr) {
        ZJC_INFO("0 get address info failed create new address to this id: %s,"
            "shard: %u, local shard: %u",
            common::Encode::HexEncode(tx.from()).c_str(), view_block.qc().network_id(),
            common::GlobalInfo::Instance()->network_id());
        account_info = std::make_shared<address::protobuf::AddressInfo>();
        account_info->set_pool_index(view_block.qc().pool_index());
        account_info->set_addr(account_id);
        account_info->set_type(address::protobuf::kNormal);
        account_info->set_sharding_id(view_block.qc().network_id());
        // account_info->set_latest_height(block.height());
        // account_info->set_balance(tx.balance());
        // return;
    }

    if (account_info->latest_height() > block.height()) {
        return;
    }
    
    account_info->set_latest_height(block.height());
    account_info->set_balance(tx.balance());
    prefix_db_->AddAddressInfo(account_id, *account_info, db_batch);
    UpdateContractPrepayment(view_block, tx, db_batch);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    thread_update_accounts_queue_[thread_idx].push(account_info);
    update_acc_con_.notify_one();
    ZJC_INFO("contract create by root from new balance %s: %lu, height: %lu, pool: %u",
        common::Encode::HexEncode(account_id).c_str(), tx.balance(),
        block.height(), view_block.qc().pool_index());
}

void AccountManager::UpdateContractPrepayment(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& block = view_block.block_info();
    auto account_id = tx.to() + tx.from();
    auto account_info = GetAccountInfo(account_id);
    if (account_info == nullptr) {
        ZJC_INFO("0 get address info failed create new address to this id: %s,"
            "shard: %u, local shard: %u",
            common::Encode::HexEncode(tx.from()).c_str(), view_block.qc().network_id(),
            common::GlobalInfo::Instance()->network_id());
        account_info = std::make_shared<address::protobuf::AddressInfo>();
        account_info->set_pool_index(view_block.qc().pool_index());
        account_info->set_addr(account_id);
        account_info->set_type(address::protobuf::kNormal);
        account_info->set_sharding_id(view_block.qc().network_id());
        // account_info->set_latest_height(block.height());
        // account_info->set_balance(tx.balance());
        // return;
    }

    if (account_info->latest_height() > block.height()) {
        return;
    }
    
    account_info->set_latest_height(block.height());
    if (tx.has_contract_prepayment() && tx.contract_prepayment() > 0) {
        account_info->set_balance(tx.contract_prepayment());
    } else {
        account_info->set_balance(tx.balance());
    }

    prefix_db_->AddAddressInfo(account_id, *account_info, db_batch);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    thread_update_accounts_queue_[thread_idx].push(account_info);
    ZJC_INFO("contract create by root from new balance %s: %lu, height: %lu, pool: %u",
        common::Encode::HexEncode(account_id).c_str(), tx.balance(),
        block.height(), view_block.qc().pool_index());
}


void AccountManager::HandleContractExecuteTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& block = view_block.block_info();
    // if (tx.status() != consensus::kConsensusSuccess) {
    //     return;
    // }

    ZJC_DEBUG("HandleContractExecuteTx address coming from: %s, to: %s, amount:%lu, balance: %lu",
        common::Encode::HexEncode(tx.from()).c_str(),
        common::Encode::HexEncode(tx.to()).c_str(),
        tx.amount(),
        tx.balance());
    auto& account_id = GetTxValidAddress(tx);
    auto account_info = GetAccountInfo(account_id);
    if (account_info == nullptr) {
        assert(false);
        return;
    }

    if (account_info->latest_height() > block.height()) {
        return;
    }

    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kContractDestruct) {
            account_info->set_destructed(true);
        }
    }

    account_info->set_latest_height(block.height());
    // amount is contract 's new balance
    account_info->set_balance(tx.amount());
    prefix_db_->AddAddressInfo(account_id, *account_info, db_batch);
    UpdateContractPrepayment(view_block, tx, db_batch);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    thread_update_accounts_queue_[thread_idx].push(account_info);
    update_acc_con_.notify_one();
    ZJC_INFO("contract call address new balance %s, from: %s, to: %s, balance: %lu, amount: %lu",
        common::Encode::HexEncode(account_id).c_str(), 
        common::Encode::HexEncode(tx.from()).c_str(), 
        common::Encode::HexEncode(tx.to()).c_str(), 
        tx.balance(),
        tx.amount());
}

void AccountManager::HandleRootCreateAddressTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& block = view_block.block_info();
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId &&
            common::GlobalInfo::Instance()->network_id() !=
            (network::kRootCongressNetworkId + network::kRootCongressNetworkId)) {
        return;
    }

    auto account_info = GetAccountInfo(tx.to());
    if (account_info != nullptr) {
//         assert(false);
        return;
    }

    uint32_t sharding_id = common::kInvalidUint32;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kRootCreateAddressKey) {
            uint32_t* tmp = (uint32_t*)tx.storages(i).value().c_str();
            sharding_id = tmp[0];
            break;
        }
    }

    if (sharding_id == common::kInvalidUint32) {
        assert(false);
        return;
    }

    uint32_t pool_index = common::GetAddressPoolIndex(tx.to());
    account_info = std::make_shared<address::protobuf::AddressInfo>();
    account_info->set_pool_index(pool_index);
    account_info->set_addr(tx.to());
    
    if (account_info->type() == address::protobuf::kWaitingRootConfirm) {
        account_info->set_type(address::protobuf::kContract);
    } else {
        account_info->set_type(address::protobuf::kNormal);
    }
    
    account_info->set_sharding_id(sharding_id);
    account_info->set_latest_height(block.height());
    account_info->set_balance(0);  // root address balance invalid
    prefix_db_->AddAddressInfo(tx.to(), *account_info, db_batch);
    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    thread_update_accounts_queue_[thread_idx].push(account_info);
    update_acc_con_.notify_one();
    ZJC_INFO("2 get address info failed create new address to this id: %s,"
        "shard: %u, local shard: %u",
        common::Encode::HexEncode(tx.to()).c_str(), sharding_id,
        common::GlobalInfo::Instance()->network_id());

    ZJC_DEBUG("create root address direct: %s, sharding: %u, pool index: %u",
        common::Encode::HexEncode(tx.to()).c_str(),
        sharding_id,
        pool_index);
}


void AccountManager::HandleJoinElectTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    auto& block = view_block.block_info();
    bls::protobuf::JoinElectInfo join_info;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kJoinElectVerifyG2) {
            if (!join_info.ParseFromString(tx.storages(i).value())) {
                assert(false);
                break;
            }

            break;
        }
    }

    if (!join_info.has_member_idx()) {
        ZJC_WARN("get local tos info failed!");
        return;
    }

    auto account_info = GetAccountInfo(tx.from());
    if (account_info == nullptr) {
        ZJC_INFO("get address info failed create new address to this shard: %s",
            common::Encode::HexEncode(tx.from()).c_str());
        account_info = std::make_shared<address::protobuf::AddressInfo>();
        account_info->set_pool_index(view_block.qc().pool_index());
        account_info->set_addr(tx.from());
        account_info->set_type(address::protobuf::kNormal);
        account_info->set_sharding_id(view_block.qc().network_id());
        account_info->set_latest_height(block.height());
        account_info->set_balance(tx.balance());
        account_info->set_elect_pos(join_info.member_idx());
        prefix_db_->AddAddressInfo(tx.from(), *account_info);
        ZJC_INFO("3 get address info failed create new address to this id: %s,"
            "shard: %u, local shard: %u, elect pos: %u",
            common::Encode::HexEncode(tx.from()).c_str(), view_block.qc().network_id(),
            common::GlobalInfo::Instance()->network_id(),
            join_info.member_idx());

    } else {
        if (account_info->latest_height() > block.height()) {
            return;
        }

        account_info->set_latest_height(block.height());
        account_info->set_balance(tx.balance());
        account_info->set_elect_pos(join_info.member_idx());
        prefix_db_->AddAddressInfo(tx.from(), *account_info, db_batch);
        ZJC_INFO("3 1 get address info failed create new address to this id: %s,"
            "shard: %u, local shard: %u, elect pos: %u",
            common::Encode::HexEncode(tx.from()).c_str(), view_block.qc().network_id(),
            common::GlobalInfo::Instance()->network_id(),
            join_info.member_idx());
    }

    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    thread_update_accounts_queue_[thread_idx].push(account_info);
    update_acc_con_.notify_one();
    ZJC_INFO("join elect to address new elect pos %s: %lu, balance: %lu",
        common::Encode::HexEncode(tx.from()).c_str(),
        join_info.member_idx(), account_info->balance());
}

void AccountManager::RunUpdateAccounts() {
    {
        // new thread with thread index
        auto thread_index = common::GlobalInfo::Instance()->get_thread_index();
        // std::unique_lock<std::mutex> lock(thread_wait_mutex_);
        thread_wait_conn_.notify_one();
    }
    
    while (!destroy_) {
        UpdateAccountsThread();
        std::unique_lock<std::mutex> lock(update_acc_mutex_);
        update_acc_con_.wait_for(lock, std::chrono::milliseconds(100));
    }
}

void AccountManager::UpdateAccountsThread() {
    common::ThreadSafeQueue<protos::AddressInfoPtr> updates_accounts_;
    for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
        while (true) {
            protos::AddressInfoPtr account_info = nullptr;
            thread_update_accounts_queue_[i].pop(&account_info);
            if (account_info == nullptr) {
                break;
            }

            ZJC_DEBUG("success add address info thread index: %d, id: %s, balance: %lu",
                    i, 
                    common::Encode::HexEncode(account_info->addr()).c_str(), 
                    account_info->balance());
            account_lru_map_.insert(account_info);
        }
    }

    // while (true) {
    //     protos::AddressInfoPtr account_info = nullptr;
    //     updates_accounts_.pop(&account_info);
    //     if (account_info == nullptr) {
    //         break;
    //     }
        
    //     for (uint32_t i = 0; i < common::kMaxThreadCount; ++i) {
    //         // TODO: check it
    //         // if (thread_valid_accounts_queue_[i].size() >= 1024 && !thread_valid_[i]) {
    //         //     ZJC_DEBUG("failed add address info thread index: %d, id: %s, balance: %lu",
    //         //         i, common::Encode::HexEncode(account_info->addr()).c_str(), account_info->balance());
    //         //     continue;
    //         // }

    //         CHECK_MEMORY_SIZE_WITH_MESSAGE(
    //             thread_valid_accounts_queue_[i], 
    //             (std::string("push thread index: ") + std::to_string(i)).c_str())
    //         thread_valid_accounts_queue_[i].push(account_info);
    //         ZJC_DEBUG("success add address info thread index: %d, id: %s, balance: %lu",
    //                 i, 
    //                 common::Encode::HexEncode(account_info->addr()).c_str(), 
    //                 account_info->balance());
    //         // if (thread_valid_accounts_queue_[i].size() >= 2024) {
    //         //     thread_valid_[i] = false;
    //         // }
    //     }
    // }
}

void AccountManager::NewBlockWithTx(
        const view_block::protobuf::ViewBlockItem& view_block_item,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    ZJC_DEBUG("now handle new block %u_%u_%lu %lu, gid: %s",
        view_block_item.qc().network_id(), 
        view_block_item.qc().pool_index(), 
        view_block_item.qc().view(), 
        view_block_item.block_info().height(),
        common::Encode::HexEncode(tx.gid()).c_str());
    switch (tx.step()) {
    case pools::protobuf::kRootCreateAddress:
        HandleRootCreateAddressTx(view_block_item, tx, db_batch);
        break;
    case pools::protobuf::kNormalFrom:
        HandleNormalFromTx(view_block_item, tx, db_batch);
        break;
    case pools::protobuf::kConsensusLocalTos:
        HandleLocalToTx(view_block_item, tx, db_batch);
        break;
    case pools::protobuf::kCreateLibrary:
    case pools::protobuf::kContractCreate:
        HandleCreateContract(view_block_item, tx, db_batch);
        break;
    case pools::protobuf::kContractExcute:
        HandleContractExecuteTx(view_block_item, tx, db_batch);
        break;
    case pools::protobuf::kJoinElect:
        HandleJoinElectTx(view_block_item, tx, db_batch);
        break;
    case pools::protobuf::kContractGasPrepayment:
        HandleContractPrepayment(view_block_item, tx, db_batch);
        break;
    case pools::protobuf::kContractCreateByRootFrom:
        HandleCreateContractByRootFrom(view_block_item, tx, db_batch);
        break;
    case pools::protobuf::kContractCreateByRootTo:
        HandleContractCreateByRootTo(view_block_item, tx, db_batch);
        break;
    case pools::protobuf::kConsensusCreateGenesisAcount:
        HandleCreateGenesisAcount(view_block_item, tx, db_batch);
        break;
    default:
        // ZJC_FATAL("invalid step: %d", tx.step());
        break;
    }
}

}  // namespace block

}  //namespace shardora
