#include "consensus/zbft/zbft.h"

#include "block/account_manager.h"
#include "contract/contract_utils.h"
#include "elect/elect_manager.h"
#include "timeblock/time_block_manager.h"

namespace zjchain {

namespace consensus {

Zbft::~Zbft() {
    Destroy();
}

Zbft::Zbft(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        std::shared_ptr<WaitingTxsItem>& txs_ptr,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr)
        : account_mgr_(account_mgr),
        security_ptr_(security_ptr),
        bls_mgr_(bls_mgr),
        txs_ptr_(txs_ptr),
        pools_mgr_(pools_mgr) {
    reset_timeout();
}

int Zbft::Init(
        uint64_t elect_height,
        common::BftMemberPtr& leader_mem_ptr,
        common::MembersPtr& members_ptr,
        libff::alt_bn128_G2& common_pk,
        libff::alt_bn128_Fr& local_sec_key) {
    if (members_ptr == nullptr) {
        ZJC_ERROR("elected memmbers is null;");
        return kBftError;
    }

    elect_height_ = elect_height;
    if (leader_mem_ptr != nullptr) {
        leader_mem_ptr_ = leader_mem_ptr;
        if (leader_mem_ptr_->pool_index_mod_num < 0) {
            ZJC_ERROR("leader: %s mem ptr pool_index_mod_num: %d, error",
                common::Encode::HexEncode(leader_mem_ptr_->id).c_str(),
                leader_mem_ptr_->pool_index_mod_num);
            return kBftError;
        }

        leader_index_ = leader_mem_ptr_->index;
    }

    members_ptr_ = members_ptr;
    common_pk_ = common_pk;
    local_sec_key_ = local_sec_key;
    if (leader_index_ >= members_ptr_->size() ||
            common_pk_ == libff::alt_bn128_G2::zero() ||
            local_sec_key_ == libff::alt_bn128_Fr::zero()) {
        ZJC_ERROR("leader_index_: %d, size: %d, common_pk_: %d, local_sec_key_: %d",
            leader_index_,
            members_ptr_->size(),
            (common_pk_ == libff::alt_bn128_G2::zero()),
            (local_sec_key_ == libff::alt_bn128_Fr::zero()));
        return kBftError;
    }

    // just leader call init
    if (leader_mem_ptr != nullptr && leader_mem_ptr->id == security_ptr_->GetAddress()) {
        this_node_is_leader_ = true;
    }

    return kBftSuccess;
}

void Zbft::Destroy() {
    std::cout << "destroy called!" << std::endl;
    if (txs_ptr_ == nullptr) {
        return;
    }

    if (consensus_status_ != kBftCommited) {
        std::cout << "recover called!" << std::endl;
        pools_mgr_->TxRecover(pool_index(), txs_ptr_->txs);
    } else {
        std::cout << "over called!" << std::endl;
        pools_mgr_->TxOver(pool_index(), txs_ptr_->txs);
    }
}

int Zbft::Prepare(bool leader, transport::MessagePtr& msg_ptr) {
    if (leader) {
        return LeaderCreatePrepare(msg_ptr);
    }

    if (pool_index() >= common::kInvalidPoolIndex) {
        ZJC_ERROR("pool index invalid[%d]!", pool_index());
        return kBftInvalidPackage;
    }

    int32_t invalid_tx_idx = -1;
    int res = BackupCheckPrepare(msg_ptr, &invalid_tx_idx);
    if (res != kBftSuccess) {
        ZJC_ERROR("backup prepare failed: %d", res);
        return res;
    }

    return kBftSuccess;
}

int Zbft::LeaderCreatePrepare(transport::MessagePtr& msg_ptr) {
    local_member_index_ = leader_index_;
    LeaderCallTransaction(msg_ptr);
    auto hotstuff_proto = msg_ptr->header.mutable_hotstuff_proto();
    hotstuff::protobuf::TxBft& tx_bft = *hotstuff_proto->mutable_tx_bft();
    auto ltxp = tx_bft.mutable_ltx_prepare();
    if (txs_ptr_->bloom_filter == nullptr) {
        auto& tx_vec = txs_ptr_->txs;
        for (uint32_t i = 0; i < tx_vec.size(); ++i) {
            ltxp->add_tx_hash_list(tx_vec[i]->tx_hash);
        }
    } else {
        auto bloom_datas = txs_ptr_->bloom_filter->data();
        for (auto iter = bloom_datas.begin(); iter != bloom_datas.end(); ++iter) {
            ltxp->add_bloom_filter(*iter);
        }
    }

    return kBftSuccess;
}

int Zbft::BackupCheckPrepare(
        transport::MessagePtr& backup_msg_ptr,
        int32_t* invalid_tx_idx) {
    auto& bft_msg = *backup_msg_ptr->header.mutable_hotstuff_proto();
    auto& tx_bft = *bft_msg.mutable_tx_bft();
    auto ltx_msg = tx_bft.mutable_ltx_prepare();
    if (DoTransaction(*ltx_msg) != kBftSuccess) {
        return kBftInvalidPackage;
    }

    ltx_msg->clear_block();
    return kBftSuccess;
}

int Zbft::InitZjcTvmContext() {
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host_.tx_context_.tx_gas_price,
        common::GlobalInfo::Instance()->gas_price());
    zjc_host_.tx_context_.tx_origin = evmc::address{};
    zjc_host_.tx_context_.block_coinbase = evmc::address{};
    zjc_host_.tx_context_.block_number = pools_mgr_->latest_height(txs_ptr_->pool_index) + 1;
    zjc_host_.tx_context_.block_timestamp = common::TimeUtils::TimestampSeconds();
    zjc_host_.tx_context_.block_gas_limit = 0;
    uint64_t chanin_id = (((uint64_t)common::GlobalInfo::Instance()->network_id()) << 32 |
        (uint64_t)pool_index());
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host_.tx_context_.chain_id,
        chanin_id);
    return kBftSuccess;
}

bool Zbft::BackupCheckLeaderValid(const transport::MessagePtr& msg_ptr) {
    auto& bft_msg = msg_ptr->header.hotstuff_proto();
    auto local_elect_height = elect_height_;
    auto members = members_ptr_;
    if (members == nullptr ||
            common_pk_ == libff::alt_bn128_G2::zero() ||
            local_sec_key_ == libff::alt_bn128_Fr::zero()) {
        if (members != nullptr) {
            ZJC_ERROR("get members failed!. bft_msg.member_index(): %d, members->size(): %d, "
                "common_pk_ == libff::alt_bn128_G2::zero(), local_sec_key_ == libff::alt_bn128_Fr::zero()",
                bft_msg.member_index(), members->size(),
                (common_pk_ == libff::alt_bn128_G2::zero()),
                (local_sec_key_ == libff::alt_bn128_Fr::zero()));
        } else {
            ZJC_ERROR("get members failed!: %lu, net id: %d", local_elect_height, common::GlobalInfo::Instance()->network_id());
        }
        return false;
    }

    for (uint32_t i = 0; i < members->size(); ++i) {
        if ((*members)[i]->id == security_ptr_->GetAddress()) {
            local_member_index_ = i;
            break;
        }
    }

    if (local_member_index_ == elect::kInvalidMemberIndex) {
        ZJC_ERROR("get local member failed!.");
        return false;
    }

    leader_mem_ptr_ = (*members)[bft_msg.member_index()];
    if (!leader_mem_ptr_) {
        ZJC_ERROR("get leader failed!.");
        return false;
    }

    elect_height_ = local_elect_height;
    members_ptr_ = members;
    ZJC_INFO("backup check leader success elect height: %lu, local_member_index_: %lu, gid: %s",
        elect_height_, local_member_index_, common::Encode::HexEncode(gid_).c_str());
    return true;
}

int Zbft::LeaderPrecommitOk(
        const hotstuff::protobuf::LeaderTxPrepare& tx_prepare,
        uint32_t index,
        const libff::alt_bn128_G1& backup_sign,
        const std::string& id) {
    ZJC_DEBUG("node index: %d, final prepare hash: %s",
        index,
        common::Encode::HexEncode(tx_prepare.prepare().prepare_final_hash()).c_str());
    auto tbft_prepare_block = std::make_shared<hotstuff::protobuf::HotstuffLeaderPrepare>(
        tx_prepare.prepare());
    if (leader_handled_precommit_) {
        ZJC_DEBUG("leader_handled_precommit_: %d", leader_handled_precommit_);
        return kBftHandled;
    }

    // TODO: check back hash eqal to it's signed hash
    auto valid_count = SetPrepareBlock(
        id,
        index,
        tx_prepare.prepare().prepare_final_hash(),
        tbft_prepare_block,
        backup_sign);
    if ((uint32_t)valid_count >= min_aggree_member_count_) {
        if (LeaderCreatePreCommitAggChallenge(
                tx_prepare.prepare().prepare_final_hash()) != kBftSuccess) {
            ZJC_ERROR("create bls precommit agg sign failed!");
            return kBftOppose;
        }

        leader_handled_precommit_ = true;
        return kBftAgree;
    }

    return kBftWaitingBackup;
}

int Zbft::LeaderCommitOk(
        uint32_t index,
        const libff::alt_bn128_G1& backup_sign,
        const std::string& id) {
    if (leader_handled_commit_) {
        ZJC_DEBUG("leader_handled_commit_");
        return kBftHandled;
    }
//     if (!prepare_bitmap_.Valid(index)) {
//         ZJC_DEBUG("index invalid: %d", index);
//         return kBftWaitingBackup;
//     }

//     auto mem_ptr = elect::ElectManager::Instance()->GetMember(network_id_, index);
    commit_aggree_set_.insert(id);
    precommit_bitmap_.Set(index);
    backup_commit_signs_[index] = backup_sign;
    ZJC_DEBUG("commit_aggree_set_.size() >= min_aggree_member_count_: %d, %d",
        commit_aggree_set_.size(), min_aggree_member_count_);
    if (commit_aggree_set_.size() >= min_aggree_member_count_) {
        leader_handled_commit_ = true;
        if (LeaderCreateCommitAggSign() != kBftSuccess) {
            ZJC_ERROR("leader create commit agg sign failed!");
            return kBftOppose;
        }

        return kBftAgree;
    }

    return kBftWaitingBackup;
}

int Zbft::CheckTimeout() {
    auto now_timestamp = std::chrono::steady_clock::now();
    if (timeout_ <= now_timestamp) {
        ZJC_DEBUG("%lu, %lu, Timeout %s,",
            timeout_.time_since_epoch().count(),
            now_timestamp.time_since_epoch().count(),
            common::Encode::HexEncode(gid()).c_str());
        return kTimeout;
    }

    if (!this_node_is_leader_) {
        return kBftSuccess;
    }

    if (!leader_handled_precommit_) {
//         if (precommit_aggree_set_.size() >= min_prepare_member_count_ ||
//                 (precommit_aggree_set_.size() >= min_aggree_member_count_ &&
//                 now_timestamp >= prepare_timeout_)) {
//             LeaderCreatePreCommitAggChallenge("");
//             leader_handled_precommit_ = true;
//             ZJC_ERROR("kTimeoutCallPrecommit %s,", common::Encode::HexEncode(gid()).c_str());
//             return kTimeoutCallPrecommit;
//        
        return kTimeoutWaitingBackup;
    }

    if (!leader_handled_commit_) {
        if (now_timestamp >= precommit_timeout_) {
            if (precommit_bitmap_.valid_count() < min_aggree_member_count_) {
                ZJC_ERROR("precommit_bitmap_.valid_count() failed!");
                return kTimeoutWaitingBackup;
            }

            prepare_bitmap_ = precommit_bitmap_;
            LeaderCreatePreCommitAggChallenge("");
            RechallengePrecommitClear();
            ZJC_ERROR("kTimeoutCallReChallenge %s,", common::Encode::HexEncode(gid()).c_str());
            return kTimeoutCallReChallenge;
        }

        return kTimeoutWaitingBackup;
    }

    return kTimeoutNormal;
}

void Zbft::RechallengePrecommitClear() {
    leader_handled_commit_ = false;
    init_precommit_timeout();
    precommit_bitmap_.clear();
    commit_aggree_set_.clear();
//     precommit_aggree_set_.clear();
    precommit_oppose_set_.clear();
    commit_oppose_set_.clear();
}

int Zbft::LeaderCreatePreCommitAggChallenge(const std::string& prpare_hash) {
    auto iter = prepare_block_map_.find(prpare_hash);
    if (iter == prepare_block_map_.end()) {
        return kBftError;
    }

    std::vector<libff::alt_bn128_G1> all_signs;
    uint32_t bit_size = iter->second->prepare_bitmap_.data().size() * 64;
    uint32_t t = min_aggree_member_count_;
    uint32_t n = members_ptr_->size();
    std::vector<size_t> idx_vec;
    for (uint32_t i = 0; i < n; ++i) {
        if (!iter->second->prepare_bitmap_.Valid(i)) {
            continue;
        }

        assert(iter->second->backup_precommit_signs_[i] != libff::alt_bn128_G1::zero());
        all_signs.push_back(iter->second->backup_precommit_signs_[i]);
        idx_vec.push_back(i + 1);
        if (idx_vec.size() >= t) {
            break;
        }
    }

    try {
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
        bls_precommit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(
            bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
        leader_tbft_prepare_hash_ = prpare_hash;
        std::string msg_hash_src = prpare_hash;
        for (uint32_t i = 0; i < iter->second->prepare_bitmap_.data().size(); ++i) {
            msg_hash_src += std::to_string(iter->second->prepare_bitmap_.data()[i]);
        }

        precommit_hash_ = common::Hash::keccak256(msg_hash_src);
        ZJC_DEBUG("leader create precommit_hash_: %s, prpare_hash: %s",
            common::Encode::HexEncode(precommit_hash_).c_str(),
            common::Encode::HexEncode(prpare_hash).c_str());
        if (bls_mgr_->Verify(
                t,
                n,
                common_pk_,
                *bls_precommit_agg_sign_,
                prpare_hash) != bls::kBlsSuccess) {
            common_pk_.to_affine_coordinates();
            auto cpk = std::make_shared<BLSPublicKey>(common_pk_);
            auto cpk_strs = cpk->toString();
            ZJC_ERROR("failed leader verify leader precommit agg sign! t: %u, n: %u,"
                "common public key: %s, %s, %s, %s, elect height: %lu, "
                "network id: %u, prepare hash: %s",
                t, n, cpk_strs->at(0).c_str(), cpk_strs->at(1).c_str(),
                cpk_strs->at(2).c_str(), cpk_strs->at(3).c_str(),
                elect_height_, network_id_, common::Encode::HexEncode(prpare_hash).c_str());
            assert(false);
            return kBftError;
        } else {
            common_pk_.to_affine_coordinates();
            auto cpk = std::make_shared<BLSPublicKey>(common_pk_);
            auto cpk_strs = cpk->toString();
            ZJC_DEBUG("success leader verify leader precommit agg sign! t: %u, n: %u,"
                "common public key: %s, %s, %s, %s, elect height: %lu,"
                "network id: %u, prepare hash: %s",
                t, n, cpk_strs->at(0).c_str(), cpk_strs->at(1).c_str(),
                cpk_strs->at(2).c_str(), cpk_strs->at(3).c_str(),
                elect_height_, network_id_, common::Encode::HexEncode(prpare_hash).c_str());
        }

        bls_precommit_agg_sign_->to_affine_coordinates();
        prepare_bitmap_ = iter->second->prepare_bitmap_;
        uint64_t max_height = 0;
        uint64_t max_count = 0;
        for (auto hiter = iter->second->height_count_map.begin();
                hiter != iter->second->height_count_map.end(); ++hiter) {
            if (hiter->second > max_count) {
                max_height = hiter->first;
                max_count = hiter->second;
            }
        }

        prepare_latest_height_ = max_height;
//         for (int32_t i = 0; i < iter->second->prpare_block->prepare_txs_size(); ++i) {
//             auto tx_info = pools_mgr_->GetTx(
//                 txs_ptr_->pool_index,
//                 iter->second->prpare_block->prepare_txs(i).gid());
//             if (!tx_info) {
//                 ZJC_ERROR("get tx failed pool: %d, gid: %s",
//                     txs_ptr_->pool_index,
//                     common::Encode::HexEncode(
//                         iter->second->prpare_block->prepare_txs(i).gid()).c_str());
//                 assert(false);
//                 continue;
//             } else {
//                 ZJC_DEBUG("get tx success pool: %d, gid: %s",
//                     txs_ptr_->pool_index,
//                     common::Encode::HexEncode(
//                         iter->second->prpare_block->prepare_txs(i).gid()).c_str());
//             }
//         }
    } catch (std::exception& e) {
        ZJC_ERROR("catch bls exception: %s", e.what());
        return kBftError;
    }

    return kBftSuccess;
}

int Zbft::LeaderCreateCommitAggSign() {
    std::vector<libff::alt_bn128_G1> all_signs;
    uint32_t bit_size = precommit_bitmap_.data().size() * 64;
    uint32_t t = min_aggree_member_count_;
    uint32_t n = members_ptr_->size();
    std::vector<size_t> idx_vec;
    for (uint32_t i = 0; i < bit_size; ++i) {
        if (!precommit_bitmap_.Valid(i)) {
            continue;
        }

        all_signs.push_back(backup_commit_signs_[i]);
        idx_vec.push_back(i + 1);
        if (idx_vec.size() >= min_aggree_member_count_) {
            break;
        }
    }

    try {
        libBLS::Bls bls_instance = libBLS::Bls(t, n);
        auto lagrange_coeffs = libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t);
        bls_commit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(bls_instance.SignatureRecover(
            all_signs,
            lagrange_coeffs));
        std::string msg_hash_src = precommit_hash();
        for (uint32_t i = 0; i < precommit_bitmap_.data().size(); ++i) {
            msg_hash_src += std::to_string(precommit_bitmap_.data()[i]);
        }

        commit_hash_ = common::Hash::Hash256(msg_hash_src);
        if (bls_mgr_->Verify(
                t,
                n,
                common_pk_,
                *bls_commit_agg_sign_,
                precommit_hash_) != bls::kBlsSuccess) {
            ZJC_ERROR("leader verify leader commit agg sign failed!");
            return kBftError;
        }

        bls_commit_agg_sign_->to_affine_coordinates();
    } catch (...) {
        return kBftError;
    }

    return kBftSuccess;
}


void Zbft::LeaderCallTransaction(transport::MessagePtr& msg_ptr) {
    auto& res_tx_bft = *msg_ptr->header.mutable_hotstuff_proto()->mutable_tx_bft();
    auto ltx_msg = res_tx_bft.mutable_ltx_prepare();
    if (DoTransaction(*ltx_msg) != kBftSuccess) {
        ZJC_ERROR("leader do transaction failed!");
        return;
    }

    libff::alt_bn128_G1 bn_sign;
    if (bls_mgr_->Sign(
            min_aggree_member_count(),
            member_count(),
            local_sec_key(),
            prepare_block()->prepare_final_hash(),
            &bn_sign) != bls::kBlsSuccess) {
        ZJC_ERROR("leader do transaction sign data failed!");
        return;
    }

    if (LeaderPrecommitOk(
            *ltx_msg,
            leader_index_,
            bn_sign,
            leader_mem_ptr_->id) != bls::kBlsSuccess) {
        ZJC_ERROR("leader call LeaderPrecommitOk failed!");
        return;
    }
}

int Zbft::DoTransaction(hotstuff::protobuf::LeaderTxPrepare& ltx_prepare) {
    if (InitZjcTvmContext() != kBftSuccess) {
        return kBftError;
    }

    std::string pool_hash = pools_mgr_->latest_hash(txs_ptr_->pool_index);
    uint64_t pool_height = pools_mgr_->latest_height(txs_ptr_->pool_index);
    block::protobuf::Block& zjc_block = *(ltx_prepare.mutable_block());
    DoTransactionAndCreateTxBlock(zjc_block);
    if (zjc_block.tx_list_size() <= 0) {
        ZJC_ERROR("all choose tx invalid!");
        return kBftNoNewTxs;
    }

    zjc_block.set_pool_index(txs_ptr_->pool_index);
    zjc_block.set_prehash(pool_hash);
    zjc_block.set_version(common::kTransactionVersion);
    zjc_block.set_network_id(common::GlobalInfo::Instance()->network_id());
    zjc_block.set_consistency_random(0);
    zjc_block.set_height(pool_height + 1);
    zjc_block.set_timestamp(common::TimeUtils::TimestampMs());
    zjc_block.set_timeblock_height(tmblock::TimeBlockManager::Instance()->LatestTimestampHeight());
    zjc_block.set_electblock_height(elect_height_);
    zjc_block.set_leader_index(leader_index_);
    zjc_block.set_hash(GetBlockHash(zjc_block));
    auto block_ptr = std::make_shared<block::protobuf::Block>(zjc_block);
    SetBlock(block_ptr);
    tbft_prepare_block_ = CreatePrepareTxInfo(block_ptr, ltx_prepare);
    if (tbft_prepare_block_ == nullptr) {
        return kBftError;
    }

    return kBftSuccess;
}

std::shared_ptr<hotstuff::protobuf::HotstuffLeaderPrepare> Zbft::CreatePrepareTxInfo(
        std::shared_ptr<block::protobuf::Block>& block_ptr,
        hotstuff::protobuf::LeaderTxPrepare& ltx_prepare) {
    std::string tbft_prepare_txs_str_for_hash;
    auto prepare = ltx_prepare.mutable_prepare();
    for (int32_t i = 0; i < block_ptr->tx_list_size(); ++i) {
        auto tx_hash = GetPrepareTxsHash(block_ptr->tx_list(i));
        if (tx_hash.empty()) {
            continue;
        }

        auto prepare_txs_item = prepare->add_prepare_txs();
        prepare_txs_item->set_gid("uni_gid");
        prepare_txs_item->set_balance(block_ptr->tx_list(i).balance());
        if (block_ptr->tx_list(i).step() == pools::protobuf::kNormalTo) {
            prepare_txs_item->set_address(block_ptr->tx_list(i).to());
        } else {
            prepare_txs_item->set_address("block_ptr->tx_list(i).from()");
        }
    }

    if (prepare->prepare_txs_size() <= 0) {
        return nullptr;
    }

    prepare->set_prepare_final_hash(GetBlockHash(*block_ptr));
    prepare->set_height(block_ptr->height());
    set_prepare_hash(prepare->prepare_final_hash());
    return std::make_shared<hotstuff::protobuf::HotstuffLeaderPrepare>(*prepare);
}

int Zbft::GetTempAccountBalance(
        const std::string& id,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        uint64_t* balance) {
    auto iter = acc_balance_map.find(id);
    if (iter == acc_balance_map.end()) {
        auto acc_info = account_mgr_->GetAcountInfo(
            txs_ptr_->thread_index,
            id);
        if (acc_info == nullptr) {
            ZJC_ERROR("account addres not exists[%s]", common::Encode::HexEncode(id).c_str());
            return kBftAccountNotExists;
        }

        acc_balance_map[id] = acc_info->balance();
        *balance = acc_info->balance();
    } else {
        *balance = iter->second;
    }

    return kBftSuccess;
}

void Zbft::TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) {
    block_tx->set_gid(tx_info.gid());
    block_tx->set_from_pubkey(tx_info.pubkey());
    block_tx->set_gas_limit(tx_info.gas_limit());
    block_tx->set_gas_price(tx_info.gas_price());
    block_tx->set_step(tx_info.step());
    block_tx->set_from_pubkey(tx_info.pubkey());
    block_tx->set_to(tx_info.to());
    block_tx->set_amount(tx_info.amount());
    if (!tx_info.key().empty()) {
        auto storage = block_tx->add_storages();
        storage->set_key(tx_info.key());
        storage->set_val_hash(common::Hash::keccak256(tx_info.value()));
    }
}

void Zbft::DoTransactionAndCreateTxBlock(block::protobuf::Block& zjc_block) {
    auto tx_list = zjc_block.mutable_tx_list();
    auto& tx_vec = txs_ptr_->txs;
    std::unordered_map<std::string, int64_t> acc_balance_map;
    for (uint32_t i = 0; i < tx_vec.size(); ++i) {
        auto& tx_info = tx_vec[i]->msg_ptr->header.tx_proto();
        auto& block_tx = *tx_list->Add();
        TxToBlockTx(tx_info, &block_tx);
        block_tx.set_status(kBftSuccess);
        if (AddTransaction(
                tx_vec[i],
                acc_balance_map,
                block_tx) != kBftSuccess) {
            continue;
        }
    }
}

int Zbft::AddTransaction(
        pools::TxItemPtr& tx_info,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    uint64_t gas_used = 0;
    // gas just consume by from
    uint64_t from_balance = 0;
    uint64_t to_balance = 0;
    auto& from = tx_info->msg_ptr->address_info->addr();
    if (block_tx.step() == pools::protobuf::kNormalFrom) {
        int balance_status = GetTempAccountBalance(from, acc_balance_map, &from_balance);
        if (balance_status != kBftSuccess) {
            block_tx.set_status(balance_status);
            // will never happen
            assert(false);
            return kBftError;
        }

        do 
        {
            gas_used = kTransferGas;
            for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
                // TODO(): check key exists and reserve gas
                gas_used += (block_tx.storages(i).key().size() + block_tx.storages(i).val_size()) *
                    kKeyValueStorageEachBytes;
            }

            if (from_balance < block_tx.gas_limit()  * block_tx.gas_price()) {
                block_tx.set_status(kBftUserSetGasLimitError);
                break;
            }

            if (block_tx.gas_limit() < gas_used) {
                block_tx.set_status(kBftUserSetGasLimitError);
                break;
            }
        } while (0);
    } else {
        int balance_status = GetTempAccountBalance(block_tx.to(), acc_balance_map, &to_balance);
        if (balance_status != kBftSuccess) {
            block_tx.set_status(balance_status);
            assert(false);
            return kBftError;
        }
    }

    if (block_tx.step() == pools::protobuf::kNormalFrom) {
        if (block_tx.status() == kBftSuccess) {
            uint64_t dec_amount = block_tx.amount() + gas_used * block_tx.gas_price();
            if (from_balance >= gas_used * block_tx.gas_price()) {
                if (from_balance >= dec_amount) {
                    from_balance -= dec_amount;
                } else {
                    from_balance -= gas_used * block_tx.gas_price();
                    block_tx.set_status(kBftAccountBalanceError);
                    ZJC_ERROR("leader balance error: %llu, %llu", from_balance, dec_amount);
                }
            } else {
                from_balance = 0;
                block_tx.set_status(kBftAccountBalanceError);
                ZJC_ERROR("leader balance error: %llu, %llu",
                    from_balance, gas_used * block_tx.gas_price());
            }
        } else {
            if (from_balance >= gas_used * block_tx.gas_price()) {
                    from_balance -= gas_used * block_tx.gas_price();
            } else {
                from_balance = 0;
            }
        }

        acc_balance_map[from] = from_balance;
        block_tx.set_balance(from_balance);
        block_tx.set_gas_used(gas_used);
    } else {
        if (block_tx.status() == kBftSuccess) {
            to_balance += block_tx.amount();
        }

        acc_balance_map[block_tx.to()] = to_balance;
        block_tx.set_balance(to_balance);
        block_tx.set_gas_used(0);
    }

    return kBftSuccess;
}

};  // namespace consensus

};  // namespace zjchain