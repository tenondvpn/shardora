#include "consensus/hotstuff/block_acceptor.h"

#include "bls/agg_bls.h"
#include "common/defer.h"
#include "common/utils.h"
#include "consensus/consensus_utils.h"
#include "consensus/hotstuff/block_executor.h"
#include "consensus/hotstuff/types.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "consensus/zbft/contract_call.h"
#include "consensus/zbft/contract_user_call.h"
#include "consensus/zbft/contract_user_create_call.h"
#include "consensus/zbft/create_library.h"
#include "consensus/zbft/elect_tx_item.h"
#include "consensus/zbft/from_tx_item.h"
#include "consensus/zbft/pool_statistic_tag.h"
#include "consensus/zbft/to_tx_local_item.h"
#include "consensus/zbft/to_tx_item.h"
#include "consensus/zbft/time_block_tx.h"
#include "consensus/zbft/statistic_tx_item.h"
#include "consensus/zbft/root_to_tx_item.h"
#include "consensus/zbft/root_cross_tx_item.h"
#include "consensus/zbft/join_elect_tx_item.h"
#include "protos/pools.pb.h"
#include "protos/zbft.pb.h"
#include "security/gmssl/gmssl.h"
#include "security/oqs/oqs.h"
#include "zjcvm/zjcvm_utils.h"

namespace shardora {

namespace hotstuff {

BlockAcceptor::BlockAcceptor() {}

BlockAcceptor::~BlockAcceptor() {}

void BlockAcceptor::Init(
        const uint32_t& pool_idx,
        const std::shared_ptr<security::Security>& security,
        const std::shared_ptr<block::AccountManager>& account_mgr,
        const std::shared_ptr<ElectInfo>& elect_info,
        const std::shared_ptr<vss::VssManager>& vss_mgr,
        const std::shared_ptr<contract::ContractManager>& contract_mgr,
        const std::shared_ptr<db::Db>& db,
        const std::shared_ptr<consensus::ContractGasPrepayment>& gas_prepayment,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<block::BlockManager>& block_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr,
        std::shared_ptr<elect::ElectManager> elect_mgr,
        consensus::BlockCacheCallback new_block_cache_callback,
        std::shared_ptr<ViewBlockChain> view_block_chain) {
    pool_idx_ = pool_idx;
    elect_mgr_ = elect_mgr;
    security_ptr_ = security;
    account_mgr_ = account_mgr;
    elect_info_ = elect_info;
    vss_mgr_ = vss_mgr;
    contract_mgr_ = contract_mgr;
    db_ = db;
    gas_prepayment_ = gas_prepayment;
    pools_mgr_ = pools_mgr;
    block_mgr_ = block_mgr;
    tm_block_mgr_ = tm_block_mgr;
    new_block_cache_callback_ = new_block_cache_callback;
    view_block_chain_ = view_block_chain;
    tx_pools_ = std::make_shared<consensus::WaitingTxsPools>(pools_mgr_, block_mgr_, tm_block_mgr_);
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);    
}

// Accept 验证 Leader 新提案信息，并执行 txs，修改 block
Status BlockAcceptor::Accept(
        std::shared_ptr<ProposeMsgWrapper>& pro_msg_wrap, 
        bool no_tx_allowed,
        bool directly_user_leader_txs,
        BalanceAndNonceMap& balance_and_nonce_map,
        zjcvm::ZjchainHost& zjc_host) {
    auto& msg_ptr = pro_msg_wrap->msg_ptr;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& propose_msg = pro_msg_wrap->msg_ptr->header.hotstuff().pro_msg().tx_propose();
    auto& view_block = *pro_msg_wrap->view_block_ptr;
    if (propose_msg.txs().empty()) {
        if (no_tx_allowed) {
            ZJC_DEBUG("success do transaction tx size: %u, add: %u, %u_%u_%lu, "
                "height: %lu, view hash: %s", 
                0, 
                view_block.block_info().tx_list_size(), 
                view_block.qc().network_id(), 
                view_block.qc().pool_index(), 
                view_block.qc().view(), 
                view_block.block_info().height(),
                common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str());
            assert(view_block.qc().view_block_hash().empty());
            view_block.mutable_qc()->set_view_block_hash(GetBlockHash(view_block));
            ZJC_DEBUG("success set view block hash: %s, parent: %s, %u_%u_%lu, "
                "chain has hash: %d, db has hash: %d",
                common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(),
                common::Encode::HexEncode(view_block.parent_hash()).c_str(),
                view_block.qc().network_id(),
                view_block.qc().pool_index(),
                view_block.qc().view(),
                view_block_chain_->Has(view_block.qc().view_block_hash()),
                prefix_db_->BlockExists(view_block.qc().view_block_hash()));
            // view_block_chain_->ResetViewBlock(view_block.qc().view_block_hash());
            if (view_block_chain_->Has(view_block.qc().view_block_hash())) {
                // assert(false);
                return Status::kSuccess;
            }

            if (prefix_db_->BlockExists(view_block.qc().view_block_hash())) {
                assert(false);
                return Status::kAcceptorBlockInvalid;
            }
        }

        ZJC_DEBUG("propose_msg.txs().empty() error!");
        return no_tx_allowed ? Status::kSuccess : Status::kAcceptorTxsEmpty;
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    // 1. verify block
    if (!IsBlockValid(view_block)) {
        ZJC_WARN("IsBlockValid error!");
        return Status::kAcceptorBlockInvalid;
    }

    // 2. Get txs from local pool
    auto txs_ptr = std::make_shared<consensus::WaitingTxsItem>();
    Status s = Status::kSuccess;
    ADD_DEBUG_PROCESS_TIMESTAMP();
    s = GetAndAddTxsLocally(
        msg_ptr,
        view_block.parent_hash(), 
        propose_msg, 
        directly_user_leader_txs, 
        txs_ptr, 
        balance_and_nonce_map,
        zjc_host);
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (s != Status::kSuccess) {
        ZJC_WARN("GetAndAddTxsLocally error!");
        return s;
    }
    
    // 3. Do txs and create block_tx
    ADD_DEBUG_PROCESS_TIMESTAMP();
    zjc_host.parent_hash_ = view_block.parent_hash();
    zjc_host.view_block_chain_ = view_block_chain_;
    zjc_host.view_ = view_block.qc().view();
    s = DoTransactions(txs_ptr, &view_block, balance_and_nonce_map, zjc_host);
    if (s != Status::kSuccess) {
        ZJC_WARN("DoTransactions error!");
        return s;
    }

    ZJC_DEBUG("success do transaction tx size: %u, add: %u, %u_%u_%lu, height: %lu", 
        txs_ptr->txs.size(), 
        view_block.block_info().tx_list_size(), 
        view_block.qc().network_id(), 
        view_block.qc().pool_index(), 
        view_block.qc().view(), 
        view_block.block_info().height());
    view_block.mutable_qc()->set_view_block_hash(GetBlockHash(view_block));
    ZJC_DEBUG("success set view block hash: %s, parent: %s, %u_%u_%lu",
        common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(),
        common::Encode::HexEncode(view_block.parent_hash()).c_str(),
        view_block.qc().network_id(),
        view_block.qc().pool_index(),
        view_block.qc().view());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (prefix_db_->BlockExists(view_block.qc().view_block_hash())) {
        return Status::kAcceptorBlockInvalid;
    }

    return Status::kSuccess;
}

// AcceptSync 验证同步来的 block 信息，并更新交易池
Status BlockAcceptor::AcceptSync(const view_block::protobuf::ViewBlockItem& view_block) {
    if (view_block.qc().pool_index() != pool_idx()) {
        return Status::kError;
    }
    
    return Status::kSuccess;
}

void BlockAcceptor::Commit(
        transport::MessagePtr msg_ptr, 
        std::shared_ptr<block::BlockToDbItem>& queue_item_ptr) {
    // commit block
    commit(msg_ptr, queue_item_ptr);
}

void BlockAcceptor::CommitSynced(std::shared_ptr<block::BlockToDbItem>& queue_item_ptr) {
    transport::MessagePtr msg_ptr;
    commit(msg_ptr, queue_item_ptr);
    auto block_ptr = &queue_item_ptr->view_block_ptr->block_info();
    ZJC_DEBUG("sync block message net: %u, pool: %u, height: %lu, block hash: %s",
        queue_item_ptr->view_block_ptr->qc().network_id(),
        queue_item_ptr->view_block_ptr->qc().pool_index(),
        block_ptr->height(),
        common::Encode::HexEncode(GetBlockHash(*queue_item_ptr->view_block_ptr)).c_str());
}

Status BlockAcceptor::addTxsToPool(
        transport::MessagePtr msg_ptr,
        const std::string& parent_hash,
        const google::protobuf::RepeatedPtrField<pools::protobuf::TxMessage>& txs,
        bool directly_user_leader_txs,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
        BalanceAndNonceMap& now_balance_map,
        zjcvm::ZjchainHost& zjc_host) {
    if (txs.size() == 0) {
        return Status::kAcceptorTxsEmpty;
    }
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    BalanceAndNonceMap prevs_balance_map;
    // view_block_chain_->MergeAllPrevStorageMap(parent_hash, zjc_host);
    view_block_chain_->MergeAllPrevBalanceMap(parent_hash, prevs_balance_map);
    // ZJC_DEBUG("merge prev all balance size: %u, tx size: %u",
    //     prevs_balance_map.size(), txs.size());
    ADD_DEBUG_PROCESS_TIMESTAMP();
    auto& txs_map = txs_ptr->txs;
    for (uint32_t i = 0; i < uint32_t(txs.size()); i++) {
        auto* tx = &txs[i];
        // ADD_TX_DEBUG_INFO(const_cast<pools::protobuf::TxMessage*>(tx));
        protos::AddressInfoPtr address_info = nullptr;
        std::string from_id;
        if (pools::IsUserTransaction(tx->step())) {
            if (tx->pubkey().size() == 64u) {
                security::GmSsl gmssl;
                from_id = gmssl.GetAddress(tx->pubkey());
            } else if (tx->pubkey().size() > 128u) {
                security::Oqs oqs;
                from_id = oqs.GetAddress(tx->pubkey());
            } else {
                from_id = security_ptr_->GetAddress(tx->pubkey());
            }
        }
        
        if (tx->step() == pools::protobuf::kContractExcute) {
            address_info = view_block_chain_->ChainGetAccountInfo(tx->to());
        } else {
            if (pools::IsUserTransaction(tx->step())) {
                address_info = view_block_chain_->ChainGetAccountInfo(from_id);
            } else {
                address_info = view_block_chain_->ChainGetPoolAccountInfo(pool_idx());
            }
        }

        if (!address_info) {
            ZJC_WARN("get address failed nonce: %lu", tx->nonce());
            return Status::kError;
        }

        auto now_map_iter = now_balance_map.find(address_info->addr());
        if (now_map_iter == now_balance_map.end()) {
            if (view_block_chain_ && view_block_chain_->CheckTxNonceValid(
                    address_info->addr(), tx->nonce(), parent_hash) != 0) {
                ZJC_WARN("check tx nonce addr: %s, failed: %lu, phash: %s", 
                    common::Encode::HexEncode(address_info->addr()).c_str(),
                    tx->nonce(), 
                    common::Encode::HexEncode(parent_hash).c_str());
                return Status::kError;
            }
        }
        
        auto iter = prevs_balance_map.find(address_info->addr());
        if (iter != prevs_balance_map.end()) {
            now_balance_map[iter->first] = iter->second;
        } else {
            now_balance_map[address_info->addr()] = std::make_pair<int64_t, uint64_t>(
                address_info->balance(), address_info->nonce());
        }

        std::string contract_prepayment_id;
        pools::TxItemPtr tx_ptr = nullptr;
        switch (tx->step()) {
        case pools::protobuf::kNormalFrom:
            tx_ptr = std::make_shared<consensus::FromTxItem>(
                    msg_ptr, i, account_mgr_, security_ptr_, address_info);
            // ADD_TX_DEBUG_INFO((const_cast<pools::protobuf::TxMessage*>(tx)));
            break;
        case pools::protobuf::kRootCreateAddress:
            tx_ptr = std::make_shared<consensus::RootToTxItem>(
                    elect_info_->max_consensus_sharding_id(),
                    msg_ptr, i,
                    vss_mgr_,
                    account_mgr_,
                    security_ptr_,
                    address_info);
            break;
        case pools::protobuf::kContractCreate:
            tx_ptr = std::make_shared<consensus::ContractUserCreateCall>(
                    contract_mgr_, 
                    db_, 
                    msg_ptr, i, 
                    account_mgr_, 
                    security_ptr_, 
                    address_info);
            contract_prepayment_id = tx->to() + from_id;
            break;
        case pools::protobuf::kContractExcute:
            tx_ptr = std::make_shared<consensus::ContractCall>(
                    contract_mgr_, 
                    gas_prepayment_, 
                    db_, 
                    msg_ptr, i,
                    account_mgr_, 
                    security_ptr_, 
                    address_info);
            contract_prepayment_id = tx->to() + from_id;
            break;
        case pools::protobuf::kContractGasPrepayment:
            tx_ptr = std::make_shared<consensus::ContractUserCall>(
                    db_, 
                    msg_ptr, i,
                    account_mgr_, 
                    security_ptr_, 
                    address_info);
            contract_prepayment_id = tx->to() + from_id;
            break;
        case pools::protobuf::kConsensusLocalTos:
            tx_ptr = std::make_shared<consensus::ToTxLocalItem>(
                    msg_ptr, i, 
                    db_, 
                    gas_prepayment_, 
                    account_mgr_, 
                    security_ptr_, 
                    address_info);
            break;
        case pools::protobuf::kNormalTo: {
            // TODO 这些 Single Tx 还是从本地交易池直接拿
            pools::protobuf::AllToTxMessage all_to_txs;
            if (!all_to_txs.ParseFromString(tx->value()) || all_to_txs.to_tx_arr_size() == 0) {
                assert(false);
                break;
            }

            if (directly_user_leader_txs) {
                tx_ptr = std::make_shared<consensus::ToTxItem>(msg_ptr, i, account_mgr_, security_ptr_, address_info);
            } else {
                auto tx_item = tx_pools_->GetToTxs(
                    pool_idx(), 
                    all_to_txs.to_tx_arr(0).to_heights().SerializeAsString());
                if (tx_item != nullptr && !tx_item->txs.empty() && view_block_chain_) {
                    tx_ptr = *(tx_item->txs.begin());
                    if (view_block_chain_->CheckTxNonceValid(
                            tx_ptr->tx_info->to(), 
                            tx_ptr->tx_info->nonce(), 
                            parent_hash) != 0) {
                        tx_ptr = nullptr;
                    }
                }
            }
            
            break;
        }
        case pools::protobuf::kStatistic:
        {
            // TODO 这些 Single Tx 还是从本地交易池直接拿
            ZJC_WARN("add tx now get statistic tx: %u", pool_idx());
            if (directly_user_leader_txs) {
                tx_ptr = std::make_shared<consensus::StatisticTxItem>(
                    msg_ptr, i, account_mgr_, security_ptr_, address_info);
            } else {
                auto tx_item = tx_pools_->GetStatisticTx(pool_idx(), 0);
                if (tx_item != nullptr && !tx_item->txs.empty()) {
                    tx_ptr = *(tx_item->txs.begin());
                } else {
                    ZJC_WARN("failed get statistic nonce: %lu, pool: %u, tx_proto: %s",
                        tx->nonce(), pool_idx_, ProtobufToJson(*tx).c_str());
                    // assert(false);
                }
            }
            break;
        }
        case pools::protobuf::kCross:
        {
            assert(false);
            break;
        }
        case pools::protobuf::kConsensusRootElectShard:
        {
            ZJC_WARN("now root elect shard coming: tx size: %u", txs.size());
            if (directly_user_leader_txs) {
                std::shared_ptr<bls::BlsManager> bls_mgr;
                tx_ptr = std::make_shared<consensus::ElectTxItem>(
                    msg_ptr, i,
                    account_mgr_,
                    security_ptr_,
                    prefix_db_,
                    elect_mgr_,
                    vss_mgr_,
                    bls_mgr,
                    false,
                    false,
                    elect_info_->max_consensus_sharding_id() - 1,
                    address_info);
            } else {
                auto txhash = pools::GetTxMessageHash(*tx);
                auto tx_item = tx_pools_->GetElectTx(pool_idx(), txhash);           
                if (tx_item != nullptr && !tx_item->txs.empty()) {
                    tx_ptr = *(tx_item->txs.begin());
                }
            }
                
            break;
        }
        case pools::protobuf::kConsensusRootTimeBlock:
        {
            // TODO 这些 Single Tx 还是从本地交易池直接拿
            if (directly_user_leader_txs) {
                tx_ptr = std::make_shared<consensus::TimeBlockTx>(
                    msg_ptr, i, account_mgr_, security_ptr_, address_info);
            } else {
                auto tx_item = tx_pools_->GetTimeblockTx(pool_idx(), false);
                if (tx_item != nullptr && !tx_item->txs.empty()) {
                    tx_ptr = *(tx_item->txs.begin());
                }
            }
            break;
        }
        case pools::protobuf::kRootCross:
        {
            tx_ptr = std::make_shared<consensus::RootCrossTxItem>(
                msg_ptr, i, 
                account_mgr_, 
                security_ptr_, 
                address_info);
            break;
        }
        case pools::protobuf::kJoinElect:
        {
            auto keypair = bls::AggBls::Instance()->GetKeyPair();
            tx_ptr = std::make_shared<consensus::JoinElectTxItem>(
                msg_ptr, i, 
                account_mgr_, 
                security_ptr_, 
                prefix_db_, 
                elect_mgr_, 
                address_info,
                (*tx).pubkey(),
                keypair->pk(),
                keypair->proof());
            ZJC_WARN("add tx now get join elect tx: %u", pool_idx());
            break;
        }
        case pools::protobuf::kPoolStatisticTag:
        {
            tx_ptr = std::make_shared<consensus::PoolStatisticTag>(
                msg_ptr, i, 
                account_mgr_, 
                security_ptr_, 
                address_info);
            ZJC_WARN("add tx now get kPoolStatisticTag tx: %u", pool_idx());
            break;
        }
        case pools::protobuf::kCreateLibrary: {
            tx_ptr = std::make_shared<consensus::CreateLibrary>(
                msg_ptr, i, 
                account_mgr_, 
                security_ptr_, 
                address_info);
            ZJC_WARN("add tx now get CreateLibrary tx: %u", pool_idx());
            break;
        }
        default:
            // TODO 没完！还需要支持其他交易的写入
            // break;
            ZJC_FATAL("invalid tx step: %d", tx->step());
            return Status::kError;
        }

        if (!contract_prepayment_id.empty()) {
            auto iter = prevs_balance_map.find(contract_prepayment_id);
            if (iter != prevs_balance_map.end()) {
                now_balance_map[iter->first] = iter->second;
            } else {
                address_info = view_block_chain_->ChainGetAccountInfo(contract_prepayment_id);
                if (address_info) {
                    now_balance_map[contract_prepayment_id] = std::make_pair<int64_t, uint64_t>(
                        address_info->balance(), address_info->nonce());
                }
            }
        }
        
        if (tx_ptr != nullptr) {
            auto tx_hash = pools::GetTxMessageHash(*tx);
            txs_map.push_back(tx_ptr);
            if (pools::IsUserTransaction(tx_ptr->tx_info->step())) {
                if (!msg_ptr->is_leader) {
                    if (tx->pubkey().size() == 64u) {
                        security::GmSsl gmssl;
                        if (gmssl.Verify(
                                tx_hash,
                                tx_ptr->tx_info->pubkey(),
                                tx_ptr->tx_info->sign()) != security::kSecuritySuccess) {
                            assert(false);
                            return Status::kError;
                        }
                    } else if (tx->pubkey().size() > 128u) {
                        security::Oqs oqs;
                        if (oqs.Verify(
                                tx_hash,
                                tx_ptr->tx_info->pubkey(),
                                tx_ptr->tx_info->sign()) != security::kSecuritySuccess) {
                            assert(false);
                            return Status::kError;
                        }
                    } else {
                        if (security_ptr_->Verify(
                                tx_hash,
                                tx_ptr->tx_info->pubkey(),
                                tx_ptr->tx_info->sign()) != security::kSecuritySuccess) {
                            assert(false);
                            return Status::kError;
                        }
                    }
                }
            }
        }
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    return Status::kSuccess;
}

Status BlockAcceptor::GetAndAddTxsLocally(
        transport::MessagePtr msg_ptr,
        const std::string& parent_hash,
        const hotstuff::protobuf::TxPropose& tx_propose,
        bool directly_user_leader_txs,
        std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
        BalanceAndNonceMap& balance_map,
        zjcvm::ZjchainHost& zjc_host) {
    auto add_txs_status = addTxsToPool(
        msg_ptr,
        parent_hash, 
        tx_propose.txs(), 
        directly_user_leader_txs, 
        txs_ptr,
        balance_map,
        zjc_host);
    if (add_txs_status != Status::kSuccess) {
        ZJC_ERROR("invalid consensus, add_txs_status failed: %d.", add_txs_status);
        return add_txs_status;
    }

    if (!txs_ptr) {
        ZJC_ERROR("invalid consensus, tx empty.");
        return Status::kAcceptorTxsEmpty;
    }

    if (txs_ptr->txs.size() != (size_t)tx_propose.txs_size()) {
// #ifndef NDEBUG
//         for (uint32_t i = 0; i < uint32_t(tx_propose.txs_size()); i++) {
//             auto tx = &tx_propose.txs(i);
//             ZJC_WARN("leader tx step: %u, gid: %s", tx->step(), common::Encode::HexEncode(tx->gid()).c_str());
//         }
// #endif
        ZJC_ERROR("invalid consensus, txs not equal to leader %u, %u",
            txs_ptr->txs.size(), tx_propose.txs_size());
        // assert(false);
        return Status::kAcceptorTxsEmpty;
    }
    
    txs_ptr->pool_index = pool_idx_;
    return Status::kSuccess;
}

bool BlockAcceptor::IsBlockValid(const view_block::protobuf::ViewBlockItem& view_block) {
    // 校验 block prehash，latest height 等
    auto* zjc_block = &view_block.block_info();
    uint64_t pool_height = pools_mgr_->latest_height(pool_idx());
    if (zjc_block->height() <= pool_height || pool_height == common::kInvalidUint64) {
        ZJC_WARN("Accept height error: %lu, %lu", zjc_block->height(), pool_height);
        return false;
    }

    auto cur_time = common::TimeUtils::TimestampMs();
    // 新块的时间戳必须大于上一个块的时间戳
    uint64_t preblock_time = pools_mgr_->latest_timestamp(pool_idx());
    if (zjc_block->timestamp() <= preblock_time && zjc_block->timestamp() + 10000lu >= cur_time) {
        ZJC_WARN("Accept timestamp error: %lu, %lu, cur: %lu", zjc_block->timestamp(), preblock_time, cur_time);
        return false;
    }
    
    return true;
}

Status BlockAcceptor::DoTransactions(
        const std::shared_ptr<consensus::WaitingTxsItem>& txs_ptr,
        view_block::protobuf::ViewBlockItem* view_block,
        BalanceAndNonceMap& balance_map,
        zjcvm::ZjchainHost& zjc_host) {
    Status s = BlockExecutorFactory().Create(security_ptr_)->DoTransactionAndCreateTxBlock(
            txs_ptr, view_block, balance_map, zjc_host);
    if (s != Status::kSuccess) {
        return s;
    }

// #ifndef NDEBUG
//     if (!txs_ptr->txs.empty() && !network::IsSameToLocalShard(network::kRootCongressNetworkId)) {
//         bool valid = true;
//         for (uint32_t i = 0; i < view_block->block_info().tx_list_size(); ++i) {
//             auto& tx = view_block->block_info().tx_list(i);
//             ZJC_WARN("block tx from: %s, to: %s, amount: %lu, balance: %lu, %u_%u_%u, height: %lu",
//                 (tx.from().empty() ? 
//                 "" : 
//                 common::Encode::HexEncode(tx.from()).c_str()), 
//                 common::Encode::HexEncode(tx.to()).c_str(), 
//                 tx.amount(),
//                 tx.balance(),
//                 view_block->qc().network_id(),
//                 view_block->qc().pool_index(),
//                 view_block->qc().view(),
//                 view_block->block_info().height());

//             if (tx.amount() != 0) {
//                 valid = false;
//                 const std::string* addr = nullptr;
//                 if (pools::IsTxUseFromAddress(tx.step())) {
//                     addr = &tx.from();
//                 } else {
//                     addr = &tx.to();
//                 }

//                 auto addr_iter = balance_map.find(*addr);
//                 if (addr_iter == balance_map.end()) {
//                     assert(false);
//                 }
                    
//             ZJC_WARN("transaction balance map addr: %s, balance: %lu, view_block_hash: %s, prehash: %s",
//                     common::Encode::HexEncode(*addr).c_str(), addr_iter->second, 
//                     common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(), 
//                     common::Encode::HexEncode(view_block->parent_hash()).c_str());
//             }
//         }

//         if (balance_map.empty()) {
//             assert(valid);
//         }
//     }
// #endif

    return s;
}

void BlockAcceptor::commit(
        transport::MessagePtr msg_ptr, 
        std::shared_ptr<block::BlockToDbItem>& queue_item_ptr) {
    auto block = &queue_item_ptr->view_block_ptr->block_info();
    new_block_cache_callback_(
        queue_item_ptr->view_block_ptr,
        *queue_item_ptr->final_db_batch);
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (network::IsSameToLocalShard(queue_item_ptr->view_block_ptr->qc().network_id())) {
        pools_mgr_->TxOver(pool_idx_, *queue_item_ptr->view_block_ptr);
        // tps measurement
        ADD_DEBUG_PROCESS_TIMESTAMP();
        CalculateTps(block->tx_list_size());
        auto now_ms = common::TimeUtils::TimestampMs();
        uint64_t b_tm = 0;
        common::StringUtil::ToUint64(queue_item_ptr->view_block_ptr->debug(), &b_tm);
        ZJC_DEBUG("[NEW BLOCK] hash: %s, prehash: %s, view: %u_%u_%lu, "
            "key: %u_%u_%u_%u, timestamp:%lu, txs: %lu, propose_debug: %s, use time ms: %lu",
            common::Encode::HexEncode(queue_item_ptr->view_block_ptr->qc().view_block_hash()).c_str(),
            common::Encode::HexEncode(queue_item_ptr->view_block_ptr->parent_hash()).c_str(),
            queue_item_ptr->view_block_ptr->qc().network_id(),
            queue_item_ptr->view_block_ptr->qc().pool_index(),
            queue_item_ptr->view_block_ptr->qc().view(),
            queue_item_ptr->view_block_ptr->qc().network_id(),
            queue_item_ptr->view_block_ptr->qc().pool_index(),
            block->height(),
            queue_item_ptr->view_block_ptr->qc().elect_height(),
            block->timestamp(),
            block->tx_list_size(),
            "",
            (now_ms - b_tm));
        ADD_DEBUG_PROCESS_TIMESTAMP();
    }
    
    ADD_DEBUG_PROCESS_TIMESTAMP();
    block_mgr_->ConsensusAddBlock(queue_item_ptr);
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

} // namespace hotstuff

} // namespace shardora
