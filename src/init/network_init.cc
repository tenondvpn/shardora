#include "init/network_init.h"
#include <functional>

#include "block/block_manager.h"
#include "common/global_info.h"
#include "common/ip.h"
#include "common/parse_args.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "common/random.h"
#include "common/time_utils.h"
#include "db/db.h"
#include "elect/elect_manager.h"
#include "http/http_server.h"
#include "init/genesis_block_init.h"
#include "init/init_utils.h"
#include "network/network_utils.h"
#include "network/dht_manager.h"
#include "network/universal_manager.h"
#include "network/bootstrap.h"
#include "network/route.h"
#include "protos/get_proto_hash.h"
#include "protos/prefix_db.h"
#include "security/ecdsa/ecdsa.h"
#include "timeblock/time_block_manager.h"
#include "timeblock/time_block_utils.h"
#include "transport/multi_thread.h"
#include "transport/tcp_transport.h"
#include "transport/transport_utils.h"
#include "zjcvm/execution.h"
#include "yaml-cpp/yaml.h"

namespace zjchain {

namespace init {

static const std::string kDefaultConfigPath("./conf/zjchain.conf");
static const uint32_t kDefaultBufferSize = 1024u * 1024u;
static const std::string kInitJoinWaitingPoolDbKey = "__kInitJoinWaitingPoolDbKey";

NetworkInit::NetworkInit() {}

NetworkInit::~NetworkInit() {
    
}

int NetworkInit::Init(int argc, char** argv) {
    auto b_time = common::TimeUtils::TimestampMs();
    if (inited_) {
        INIT_ERROR("network inited!");
        return kInitError;
    }

    if (InitConfigWithArgs(argc, argv) != kInitSuccess) {
        INIT_ERROR("init config with args failed!");
        return kInitError;
    }

    if (common::GlobalInfo::Instance()->Init(conf_) != common::kCommonSuccess) {
        INIT_ERROR("init global info failed!");
        return kInitError;
    }

    if (InitSecurity() != kInitSuccess) {
        INIT_ERROR("InitSecurity failed!");
        return kInitError;
    }

    contract_mgr_ = std::make_shared<contract::ContractManager>();
    contract_mgr_->Init(security_);
    common::ParserArgs parser_arg;
    if (ParseParams(argc, argv, parser_arg) != kInitSuccess) {
        INIT_ERROR("parse params failed!");
        return kInitError;
    }

    int genesis_check = GenesisCmd(parser_arg);
    if (genesis_check != -1) {
        std::cout << "genesis cmd over, exit." << std::endl;
        return genesis_check;
    }

    std::string db_path = "./db";
    conf_.Get("zjchain", "db_path", db_path);
    db_ = std::make_shared<db::Db>();
    if (!db_->Init(db_path)) {
        INIT_ERROR("init db failed!");
        return kInitError;
    }

    common::Ip::Instance();
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    // 随机数
    vss_mgr_ = std::make_shared<vss::VssManager>(security_);
    kv_sync_ = std::make_shared<sync::KeyValueSync>();
    gas_prepayment_ = std::make_shared<consensus::ContractGasPrepayment>(
        common::GlobalInfo::Instance()->message_handler_thread_count() - 1,
        db_);
    zjcvm::Execution::Instance()->Init(db_);
    
    InitLocalNetworkId();
    ZJC_DEBUG("id: %s, init sharding id: %u",
        common::Encode::HexEncode(security_->GetAddress()).c_str(),
        common::GlobalInfo::Instance()->network_id());
    if (net_handler_.Init(db_) != transport::kTransportSuccess) {
        return kInitError;
    }

    int transport_res = transport::TcpTransport::Instance()->Init(
        common::GlobalInfo::Instance()->config_local_ip() + ":" +
        std::to_string(common::GlobalInfo::Instance()->config_local_port()),
        128,
        true,
        &net_handler_);
    if (transport_res != transport::kTransportSuccess) {
        INIT_ERROR("int tcp transport failed!");
        return kInitError;
    }

    network::DhtManager::Instance();
    network::Route::Instance();
    network::Route::Instance()->RegisterMessage(
        common::kInitMessage,
        std::bind(&NetworkInit::HandleMessage, this, std::placeholders::_1));
    network::UniversalManager::Instance()->Init(security_, db_);
    if (InitNetworkSingleton() != kInitSuccess) {
        INIT_ERROR("InitNetworkSingleton failed!");
        return kInitError;
    }

    account_mgr_ = std::make_shared<block::AccountManager>();
    block_mgr_ = std::make_shared<block::BlockManager>(net_handler_);
    bls_mgr_ = std::make_shared<bls::BlsManager>(security_, db_);
    elect_mgr_ = std::make_shared<elect::ElectManager>(
        vss_mgr_, block_mgr_, security_, bls_mgr_, db_,
        nullptr);
    kv_sync_->Init(
        block_mgr_,
        db_);
    pools_mgr_ = std::make_shared<pools::TxPoolManager>(
        security_, db_, kv_sync_,
        std::bind(&NetworkInit::RotationLeaderCallback, this, std::placeholders::_1, std::placeholders::_2));
    account_mgr_->Init(
        common::GlobalInfo::Instance()->message_handler_thread_count(),
        db_,
        pools_mgr_);
    auto new_db_cb = std::bind(
        &NetworkInit::DbNewBlockCallback,
        this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3);
    shard_statistic_ = std::make_shared<pools::ShardStatistic>(
        elect_mgr_, db_, security_, pools_mgr_);
    block_mgr_->Init(
        account_mgr_,
        db_,
        pools_mgr_,
        shard_statistic_,
        security_,
        contract_mgr_,
        security_->GetAddress(),
        new_db_cb,
        std::bind(&NetworkInit::BlockBlsAggSignatureValid, this, std::placeholders::_1, std::placeholders::_2));
    tm_block_mgr_ = std::make_shared<timeblock::TimeBlockManager>();
    bft_mgr_ = std::make_shared<consensus::BftManager>();
    auto bft_init_res = bft_mgr_->Init(
        std::bind(&NetworkInit::BlockBlsAggSignatureValid, this, std::placeholders::_1, std::placeholders::_2),
        contract_mgr_,
        gas_prepayment_,
        vss_mgr_,
        account_mgr_,
        block_mgr_,
        elect_mgr_,
        pools_mgr_,
        security_,
        tm_block_mgr_,
        bls_mgr_,
        kv_sync_,
        db_,
        nullptr,
        common::GlobalInfo::Instance()->message_handler_thread_count() - 1,
        std::bind(&NetworkInit::AddBlockItemToCache, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    if (bft_init_res != consensus::kConsensusSuccess) {
        INIT_ERROR("init bft failed!");
        return kInitError;
    }

    tm_block_mgr_->Init(vss_mgr_,account_mgr_);
    if (elect_mgr_->Init() != elect::kElectSuccess) {
        INIT_ERROR("init elect manager failed!");
        return kInitError;
    }

    if (common::GlobalInfo::Instance()->network_id() != common::kInvalidUint32 &&
            common::GlobalInfo::Instance()->network_id() >= network::kConsensusShardEndNetworkId) {
        if (elect_mgr_->Join(
                0,
                common::GlobalInfo::Instance()->network_id()) != elect::kElectSuccess) {
            INIT_ERROR("join waiting pool network[%u] failed!",
                common::GlobalInfo::Instance()->network_id());
            return kInitError;
        }
    }

    block_mgr_->LoadLatestBlocks(common::GlobalInfo::Instance()->message_handler_thread_count());
    shard_statistic_->Init();
    transport::TcpTransport::Instance()->Start(false);
    if (InitHttpServer() != kInitSuccess) {
        INIT_ERROR("InitHttpServer failed!");
        return kInitError;
    }

    net_handler_.Start();
    GetAddressShardingId(main_thread_idx_);
    if (InitCommand() != kInitSuccess) {
        INIT_ERROR("InitCommand failed!");
        return kInitError;
    }

    inited_ = true;
    
    cmd_.Run();
    return kInitSuccess;
}

void NetworkInit::HandleMessage(const transport::MessagePtr& msg_ptr) {
    if (msg_ptr->header.init_proto().has_addr_req()) {
        HandleAddrReq(msg_ptr);
    }

    if (msg_ptr->header.init_proto().has_addr_res()) {
        HandleAddrRes(msg_ptr);
    }

    if (msg_ptr->header.init_proto().has_pools()) {
        HandleLeaderPools(msg_ptr);
    }
}

void NetworkInit::BroadcastInvalidPools(
        uint8_t thread_idx,
        std::shared_ptr<LeaderRotationInfo> rotation,
        int32_t mod_num) {
    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id < network::kRootCongressNetworkId || net_id >= network::kConsensusShardEndNetworkId) {
        return;
    }

    if (rotation->rotations[mod_num].version < 0) {
        rotation->rotations[mod_num].version = 0;
    }

    uint32_t leader_idx = common::kInvalidUint32;
    while (true) {
        auto idx = rotation->rotations[mod_num].version %
            rotation->rotations[mod_num].rotation_leaders.size();
        leader_idx = rotation->rotations[mod_num].rotation_leaders[idx];
        if (!rotation->rotation_used[leader_idx]) {
            break;
        }

        ++rotation->rotations[mod_num].version;
    }

    ZJC_DEBUG("now tm: %lu, old: %lu, kRotationLeaderCount: %u, leader_idx: %u, "
        "new leader idx: %u",
        common::TimeUtils::TimestampSeconds(),
        rotation->tm_block_tm, kRotationLeaderCount, leader_idx,
        leader_idx);
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    msg_ptr->header.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    dht::DhtKeyManager dht_key(net_id);
    msg_ptr->header.set_des_dht_key(dht_key.StrKey());
    msg_ptr->header.set_type(common::kInitMessage);
    auto* init_msg = msg_ptr->header.mutable_init_proto();
    auto* pools = init_msg->mutable_pools();
    pools->set_elect_height(rotation->elect_height);
    pools->set_member_index(rotation->local_member_index);
    pools->set_mod_num(mod_num);
    pools->set_leader_idx(leader_idx);
    pools->set_version(rotation->rotations[mod_num].version);
    msg_ptr->header.mutable_broadcast();
    transport::TcpTransport::Instance()->SetMessageHash(
        msg_ptr->header,
        msg_ptr->thread_idx);
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    std::string sign;
    if (security_->Sign(msg_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return;
    }

    msg_ptr->header.set_sign(sign);
    network::Route::Instance()->Send(msg_ptr);
    HandleLeaderPools(msg_ptr);
    ZJC_DEBUG("success broadcast invalid pools.");
}

void NetworkInit::HandleLeaderPools(const transport::MessagePtr& msg_ptr) {
    ZJC_DEBUG("roatation leader message coming..");
    auto rotation = rotation_leaders_;
    if (rotation == nullptr) {
        return;
    }

    auto& pools = msg_ptr->header.init_proto().pools();
    if (pools.elect_height() != rotation->elect_height ||
            pools.member_index() >= rotation->members->size()) {
        ZJC_WARN("elect height error pools.elect_height(): %lu, rotation->elect_height: %lu, "
            "pools.member_index(): %u, rotation->members->size(): %u",
            pools.elect_height(), rotation->elect_height,
            pools.member_index(), rotation->members->size());
        return;
    }

    if (rotation->rotation_used[pools.leader_idx()]) {
        return;
    }

    if (pools.mod_num() >= (int32_t)rotation->rotations.size()) {
        assert(false);
        return;
    }

    auto& mem_ptr = (*rotation->members)[pools.member_index()];
    auto msg_hash = transport::TcpTransport::Instance()->GetHeaderHashForSign(msg_ptr->header);
    if (security_->Verify(
            msg_hash,
            mem_ptr->pubkey,
            msg_ptr->header.sign()) != security::kSecuritySuccess) {
        //assert(false);
        return;
    }

    auto& leader_rotation = rotation->rotations[pools.mod_num()];
    auto iter = leader_rotation.version_with_count.find(pools.version());
    if (iter == leader_rotation.version_with_count.end()) {
        leader_rotation.version_with_count[pools.version()] = RotatitionVersionInfo();
        iter = leader_rotation.version_with_count.find(pools.version());
    }

    auto exist_iter = iter->second.handled_set.find(pools.member_index());
    if (exist_iter != iter->second.handled_set.end()) {
        return;
    }

    iter->second.handled_set.insert(pools.member_index());
    auto count_iter = iter->second.count_map.find(pools.leader_idx());
    if (count_iter == iter->second.count_map.end()) {
        iter->second.count_map[pools.leader_idx()] = 0;
        count_iter = iter->second.count_map.find(pools.leader_idx());
    }

    ++count_iter->second;
    uint32_t t = common::GetSignerCount(rotation->members->size());
    ZJC_DEBUG("roatation leader message coming mod num: %d, elect height: %lu, "
        "new leader idx: %u, count: %u, t: %u",
        pools.mod_num(),
        rotation->elect_height,
        pools.leader_idx(),
        count_iter->second,
        t);
    if (count_iter->second >= t) {
        rotation->rotation_used[pools.leader_idx()] = true;
        auto mem_ptr = (*rotation->members)[pools.leader_idx()];
        block_mgr_->ChangeLeader(pools.mod_num(), mem_ptr);
        bft_mgr_->RotationLeader(
            pools.mod_num(),
            rotation->elect_height,
            pools.leader_idx());
    }
}

void NetworkInit::RotationLeaderCallback(
        uint8_t thread_idx,
        const std::deque<std::shared_ptr<std::vector<std::pair<uint32_t, uint32_t>>>>& invalid_pools) {
    auto rotation = rotation_leaders_;
    if (rotation == nullptr) {
        return;
    }

    if (rotation->rotations.empty()) {
        return;
    }

    if (common::TimeUtils::TimestampSeconds() <= rotation->tm_block_tm) {
        return;
    }

    uint64_t invalid_leader_mods[rotation->rotations.size()] = { 0 };
    for (uint32_t pool = 0; pool < invalid_pools.size(); ++pool) {
        auto& invalid_pool = *(invalid_pools[pool]);
        std::pair<uint32_t, uint32_t> tx_counts[rotation->rotations.size()];
        for (uint32_t i = 0; i < invalid_pool.size(); ++i) {
            auto mod_idx = i % rotation->rotations.size();
            tx_counts[mod_idx].first += invalid_pool[i].first;
            tx_counts[mod_idx].second += invalid_pool[i].second;
        }

        for (uint32_t i = 0; i < rotation->rotations.size(); ++i) {
            if (tx_counts[i].first == 0 && tx_counts[i].second > 0) {
                ++invalid_leader_mods[i];
                ZJC_DEBUG("pool mod num: %u, handled: %u, all: %u, count: %lu, need: %lu",
                    i, tx_counts[i].first, tx_counts[i].second,
                    invalid_leader_mods[i],
                    (pools::kCaculateLeaderLofPeriod / pools::kCheckLeaderLofPeriod));
            }
        }
    }

    for (uint32_t i = 0; i < rotation->rotations.size(); ++i) {
        if (invalid_leader_mods[i] + 2 >= (pools::kCaculateLeaderLofPeriod / pools::kCheckLeaderLofPeriod)) {
            BroadcastInvalidPools(thread_idx, rotation, i);
        }
    }
}

void NetworkInit::HandleAddrReq(const transport::MessagePtr& msg_ptr) {
    auto account_info = prefix_db_->GetAddressInfo(
            msg_ptr->header.init_proto().addr_req().id());
    if (account_info == nullptr) {
        return;
    }

    transport::protobuf::Header msg;
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    msg.set_type(common::kInitMessage);
    dht::DhtKeyManager dht_key(network::kUniversalNetworkId);
    msg.set_des_dht_key(dht_key.StrKey());
    auto& init_msg = *msg.mutable_init_proto()->mutable_addr_res();
    if (!prefix_db_->GetBlockWithHeight(
            network::kRootCongressNetworkId,
            account_info->pool_index(),
            account_info->latest_height(),
            init_msg.mutable_block())) {
        return;
    }

    bool tx_valid = false;
    for (int32_t i = 0; i < init_msg.block().tx_list_size(); ++i) {
        if (init_msg.block().tx_list(i).to() == account_info->addr()) {
            tx_valid = true;
            break;
        }
    }

    if (!tx_valid) {
        return;
    }

    std::cout << "success handle init req message." << std::endl;
    transport::TcpTransport::Instance()->SetMessageHash(msg, msg_ptr->thread_idx);
    transport::TcpTransport::Instance()->Send(msg_ptr->thread_idx, msg_ptr->conn, msg);
}

void NetworkInit::HandleAddrRes(const transport::MessagePtr& msg_ptr) {
    if (common::GlobalInfo::Instance()->network_id() != common::kInvalidUint32) {
        return;
    }

    auto& block = msg_ptr->header.init_proto().addr_res().block();
    if (block.tx_list_size() != 1) {
        return;
    }

    uint32_t sharding_id = common::kInvalidUint32;
    for (int32_t i = 0; i < block.tx_list_size(); ++i) {
        if (block.tx_list(i).to() == security_->GetAddress()) {
            for (int32_t j = 0; j < block.tx_list(i).storages_size(); ++j) {
                if (block.tx_list(i).storages(j).key() == protos::kRootCreateAddressKey) {
                    uint32_t* tmp = (uint32_t*)block.tx_list(i).storages(j).val_hash().c_str();
                    sharding_id = tmp[0];
                    break;
                }
            }

            break;
        }
    }

    std::cout << "success handle init res message. response shard: " << sharding_id
        << ", join type: " << common::GlobalInfo::Instance()->join_root() <<  std::endl;
    if (sharding_id == common::kInvalidUint32) {
        return;
    }

    des_sharding_id_ = sharding_id;
    // random chance to join root shard
    if (common::GlobalInfo::Instance()->join_root() == common::kJoinRoot) {
        sharding_id = network::kRootCongressNetworkId;
    } else if (common::GlobalInfo::Instance()->join_root() == common::kRandom &&
            common::Random::RandomInt32() % 4 == 1) {
        sharding_id = network::kRootCongressNetworkId;
    }
        
    std::cout << "success handle init res message. response shard: " << sharding_id
        << ", join type: " << common::GlobalInfo::Instance()->join_root() << ", rand join: " << sharding_id << std::endl;
    prefix_db_->SaveJoinShard(sharding_id, des_sharding_id_);
    auto waiting_network_id = sharding_id + network::kConsensusWaitingShardOffset;
    if (elect_mgr_->Join(msg_ptr->thread_idx, waiting_network_id) != elect::kElectSuccess) {
        INIT_ERROR("join waiting pool network[%u] failed!", waiting_network_id);
        return;
    }

    std::cout << "success handle init res message. join waiting shard: " << waiting_network_id
        << ", des_sharding_id_: " << des_sharding_id_ <<std::endl;
    common::GlobalInfo::Instance()->set_network_id(waiting_network_id);
}

void NetworkInit::GetAddressShardingId(uint8_t thread_idx) {
    if (common::GlobalInfo::Instance()->network_id() != common::kInvalidUint32) {
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->thread_idx = thread_idx;
    auto& msg = msg_ptr->header;
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    msg.set_type(common::kInitMessage);
    dht::DhtKeyManager dht_key(network::kRootCongressNetworkId);
    msg.set_des_dht_key(dht_key.StrKey());
    auto& init_msg = *msg.mutable_init_proto();
    auto& init_req = *init_msg.mutable_addr_req();
    init_req.set_id(security_->GetAddress());
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    network::Route::Instance()->Send(msg_ptr);
    std::cout << "sent get addresss info: " << common::Encode::HexEncode(security_->GetAddress()) << std::endl;
    init_tick_.CutOff(10000000lu, std::bind(&NetworkInit::GetAddressShardingId, this, std::placeholders::_1));
}

void NetworkInit::InitLocalNetworkId() {
    uint32_t got_sharding_id = common::kInvalidUint32;
    if (!prefix_db_->GetJoinShard(&got_sharding_id, &des_sharding_id_)) {
        auto local_node_account_info = prefix_db_->GetAddressInfo(security_->GetAddress());
        if (local_node_account_info == nullptr) {
            return;
        }

        got_sharding_id = local_node_account_info->sharding_id();
        des_sharding_id_ = got_sharding_id;
        prefix_db_->SaveJoinShard(got_sharding_id, des_sharding_id_);
        std::cout << "success handle init res message. join waiting shard: " << got_sharding_id
            << ", des_sharding_id_: " << des_sharding_id_ << std::endl;
    }

    elect::ElectBlockManager elect_block_mgr;
    // 加载最新的选举块到 cache
    // 从最新的选举块中，获取 node 所在 shard_id(对于种子节点，创世的时候已经写入这部分信息了)
    elect_block_mgr.Init(db_);
    for (uint32_t sharding_id = network::kRootCongressNetworkId;
            sharding_id < network::kConsensusShardEndNetworkId; ++sharding_id) {
        auto block_ptr = elect_block_mgr.GetLatestElectBlock(sharding_id);
        if (block_ptr == nullptr) {
            break;
        }

        auto& in = block_ptr->in();
        for (int32_t member_idx = 0; member_idx < in.size(); ++member_idx) {
            auto id = security_->GetAddress(in[member_idx].pubkey());
            // 如果本 node pubkey 与 elect block 当中记录的相同，则分配到对应的 sharding
            if (id == security_->GetAddress()) {
                ZJC_DEBUG("should join network: %u", sharding_id);
                des_sharding_id_ = sharding_id;
                common::GlobalInfo::Instance()->set_network_id(sharding_id);
                break;
            }
        }
    }

    if (common::GlobalInfo::Instance()->network_id() != common::kInvalidUint32) {
        return;
    }

    auto waiting_network_id = got_sharding_id + network::kConsensusWaitingShardOffset;
    common::GlobalInfo::Instance()->set_network_id(waiting_network_id);
}

void NetworkInit::SendJoinElectTransaction(uint8_t thread_idx) {
    // if (common::GlobalInfo::Instance()->for_ck_server()) {
    //     return;
    // }

    if (common::GlobalInfo::Instance()->network_id() < network::kConsensusShardEndNetworkId) {
        return;
    }

    if (common::GlobalInfo::Instance()->network_id() >= network::kConsensusWaitingShardEndNetworkId) {
        return;
    }

    if (des_sharding_id_ == common::kInvalidUint32) {
        ZJC_DEBUG("failed get address info: %s",
            common::Encode::HexEncode(security_->GetAddress()).c_str());
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    dht::DhtKeyManager dht_key(des_sharding_id_);
    msg.set_src_sharding_id(des_sharding_id_);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    msg.set_hop_count(0);
    auto broadcast = msg.mutable_broadcast();
    auto new_tx = msg.mutable_tx_proto();
    std::string gid = common::Hash::keccak256(
        std::to_string(tm_block_mgr_->LatestTimestamp()) + security_->GetAddress());
    new_tx->set_gid(gid);
    new_tx->set_pubkey(security_->GetPublicKeyUnCompressed());
    new_tx->set_step(pools::protobuf::kJoinElect);
    new_tx->set_gas_limit(consensus::kJoinElectGas + 100000);
    new_tx->set_gas_price(10);
    new_tx->set_key(protos::kElectJoinShard);
    bls::protobuf::JoinElectInfo join_info;
    uint32_t pos = common::kInvalidUint32;
    prefix_db_->GetLocalElectPos(security_->GetAddress(), &pos);
    join_info.set_member_idx(pos);
    join_info.set_shard_id(
        common::GlobalInfo::Instance()->network_id() -
        network::kConsensusWaitingShardOffset);
    if (pos == common::kInvalidUint32) {
        auto* req = join_info.mutable_g2_req();
        auto res = prefix_db_->GetBlsVerifyG2(security_->GetAddress(), req);
        if (!res) {
            CreateContribution(req);
        }
    }

    new_tx->set_value(join_info.SerializeAsString());
    transport::TcpTransport::Instance()->SetMessageHash(msg, thread_idx);
    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign;
    if (security_->Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return;
    }

    msg.set_sign(sign);
    msg_ptr->thread_idx = thread_idx;
    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("success send join elect request transaction: %u, join: %u, gid: %s, "
        "hash64: %lu, tx hash: %s, pk: %s sign: %s",
        des_sharding_id_, join_info.shard_id(),
        common::Encode::HexEncode(gid).c_str(),
        msg.hash64(),
        common::Encode::HexEncode(tx_hash).c_str(),
        common::Encode::HexEncode(new_tx->pubkey()).c_str(),
        common::Encode::HexEncode(msg.sign()).c_str());
}

void NetworkInit::CreateContribution(bls::protobuf::VerifyVecBrdReq* bls_verify_req) {
    auto n = common::GlobalInfo::Instance()->each_shard_max_members();
    auto t = common::GetSignerCount(n);
    libBLS::Dkg dkg_instance(t, n);
    std::vector<libff::alt_bn128_Fr> polynomial = dkg_instance.GeneratePolynomial();
    bls::protobuf::LocalPolynomial local_poly;
    for (uint32_t i = 0; i < polynomial.size(); ++i) {
        local_poly.add_polynomial(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(polynomial[i])));
    }

    auto g2_vec = dkg_instance.VerificationVector(polynomial);
    for (uint32_t i = 0; i < t; ++i) {
        bls::protobuf::VerifyVecItem& verify_item = *bls_verify_req->add_verify_vec();
        verify_item.set_x_c0(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].X.c0)));
        verify_item.set_x_c1(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].X.c1)));
        verify_item.set_y_c0(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].Y.c0)));
        verify_item.set_y_c1(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].Y.c1)));
        verify_item.set_z_c0(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].Z.c0)));
        verify_item.set_z_c1(common::Encode::HexDecode(
            libBLS::ThresholdUtils::fieldElementToString(g2_vec[i].Z.c1)));

    }
    
    auto str = bls_verify_req->SerializeAsString();
    prefix_db_->AddBlsVerifyG2(security_->GetAddress(), *bls_verify_req);
    prefix_db_->SaveLocalPolynomial(security_, security_->GetAddress(), local_poly);
}

int NetworkInit::InitSecurity() {
    std::string prikey;
    if (!conf_.Get("zjchain", "prikey", prikey)) {
        INIT_ERROR("get private key from config failed!");
        return kInitError;
    }

    security_ = std::make_shared<security::Ecdsa>();
    if (security_->SetPrivateKey(
            common::Encode::HexDecode(prikey)) != security::kSecuritySuccess) {
        INIT_ERROR("init security failed!");
        return kInitError;
    }

    return kInitSuccess;
}

int NetworkInit::InitHttpServer() {
    std::string http_ip = "0.0.0.0";
    uint16_t http_port = 0;
    conf_.Get("zjchain", "http_ip", http_ip);
    if (conf_.Get("zjchain", "http_port", http_port) && http_port != 0) {
        if (http_server_.Init(http_ip.c_str(), http_port, 1) != 0) {
            INIT_ERROR("init http server failed! %s:%d", http_ip.c_str(), http_port);
            return kInitError;
        }

        http_handler_.Init(&net_handler_, security_, prefix_db_, contract_mgr_, http_server_);
        http_server_.Start();
    }

    return kInitSuccess;
}

void NetworkInit::Destroy() {
    cmd_.Destroy();
    net_handler_.Destroy();
//     if (db_ != nullptr) {
//         db_->Destroy();
//     }
}

int NetworkInit::InitCommand() {
    bool first_node = false;
    if (!conf_.Get("zjchain", "first_node", first_node)) {
        INIT_ERROR("get conf zjchain first_node failed!");
        return kInitError;
    }

    bool show_cmd = false;
    if (!conf_.Get("zjchain", "show_cmd", show_cmd)) {
        INIT_ERROR("get conf zjchain show_cmd failed!");
        return kInitError;
    }

    if (!cmd_.Init(first_node, show_cmd)) {
        INIT_ERROR("init command failed!");
        return kInitError;
    }
    return kInitSuccess;
}

int NetworkInit::InitNetworkSingleton() {
    if (network::Bootstrap::Instance()->Init(conf_, security_) != network::kNetworkSuccess) {
        INIT_ERROR("init bootstrap failed!");
        return kInitError;
    }

    main_thread_idx_ = common::GlobalInfo::Instance()->message_handler_thread_count() + 1;
    if (network::UniversalManager::Instance()->CreateUniversalNetwork(
            main_thread_idx_,
            conf_) != network::kNetworkSuccess) {
        INIT_ERROR("create universal network failed!");
        return kInitError;
    }

    if (network::UniversalManager::Instance()->CreateNodeNetwork(
            main_thread_idx_,
            conf_) != network::kNetworkSuccess) {
        INIT_ERROR("create node network failed!");
        return kInitError;
    }

    return kInitSuccess;
}

int NetworkInit::InitConfigWithArgs(int argc, char** argv) {
    common::ParserArgs parser_arg;
    if (ParseParams(argc, argv, parser_arg) != kInitSuccess) {
        INIT_ERROR("parse params failed!");
        return kInitError;
    }

    if (parser_arg.Has("h")) {
        cmd_.Help();
        exit(0);
    }

    if (parser_arg.Has("v")) {
        std::string version_info = common::GlobalInfo::Instance()->GetVersionInfo();
        exit(0);
    }

    parser_arg.Get("c", config_path_);
    if (config_path_.empty()) {
        config_path_ = kDefaultConfigPath;
    }

    if (!conf_.Init(config_path_.c_str())) {
        INIT_ERROR("init config file failed: %s", config_path_.c_str());
        return kInitError;
    }

    if (ResetConfig(parser_arg) != kInitSuccess) {
        INIT_ERROR("reset config with arg parser failed!");
        return kInitError;
    }

    return kInitSuccess;
}

int NetworkInit::ResetConfig(common::ParserArgs& parser_arg) {
        std::string db_path;
    if (parser_arg.Get("d", db_path) == common::kParseSuccess) {
        if (!conf_.Set("db", "path", db_path)) {
            INIT_ERROR("set config failed [db][path][%s]", db_path.c_str());
            return kInitError;
        }
    }
    std::string country;
    parser_arg.Get("o", country);
    if (!country.empty()) {
        if (!conf_.Set("zjchain", "country", country)) {
            INIT_ERROR("set config failed [node][country][%s]", country.c_str());
            return kInitError;
        }
    }

    std::string local_ip;
    parser_arg.Get("a", local_ip);
    if (!local_ip.empty()) {
        if (!conf_.Set("zjchain", "local_ip", local_ip)) {
            INIT_ERROR("set config failed [node][local_ip][%s]", local_ip.c_str());
            return kInitError;
        }
    }

    uint16_t local_port = 0;
    if (parser_arg.Get("l", local_port) == common::kParseSuccess) {
        if (!conf_.Set("zjchain", "local_port", local_port)) {
            INIT_ERROR("set config failed [node][local_port][%d]", local_port);
            return kInitError;
        }
    }

    std::string prikey;
    parser_arg.Get("k", prikey);
    if (!prikey.empty()) {
        if (!conf_.Set("zjchain", "prikey", prikey)) {
            INIT_ERROR("set config failed [node][id][%s]", prikey.c_str());
            return kInitError;
        }
    }

    int first = 0;
    if (parser_arg.Get("f", first) == common::kParseSuccess) {
        bool first_node = false;
        if (first == 1) {
            first_node = true;
        }

        if (!conf_.Set("zjchain", "first_node", first_node)) {
            INIT_ERROR("set config failed [node][first_node][%d]", first_node);
            return kInitError;
        }
    }

    std::string network_ids;
    if (parser_arg.Get("n", network_ids) == common::kParseSuccess) {
        if (!conf_.Set("zjchain", "net_ids", network_ids)) {
            INIT_ERROR("set config failed [node][net_id][%s]", network_ids.c_str());
            return kInitError;
        }
    }

    std::string peer;
    parser_arg.Get("p", peer);
    if (!peer.empty()) {
        if (!conf_.Set("zjchain", "bootstrap", peer)) {
            INIT_ERROR("set config failed [node][bootstrap][%s]", peer.c_str());
            return kInitError;
        }
    }

    std::string id;
    parser_arg.Get("i", id);
    if (!id.empty()) {
        if (!conf_.Set("zjchain", "id", id)) {
            INIT_ERROR("set config failed [node][id][%s]", peer.c_str());
            return kInitError;
        }
    }

    int show_cmd = 1;
    if (parser_arg.Get("g", show_cmd) == common::kParseSuccess) {
        if (!conf_.Set("zjchain", "show_cmd", show_cmd == 1)) {
            INIT_ERROR("set config failed [node][show_cmd][%d]", show_cmd);
            return kInitError;
        }
    }

    int vpn_vip_level = 0;
    if (parser_arg.Get("V", vpn_vip_level) == common::kParseSuccess) {
        if (!conf_.Set("zjchain", "vpn_vip_level", vpn_vip_level)) {
            INIT_ERROR("set config failed [node][vpn_vip_level][%d]", vpn_vip_level);
            return kInitError;
        }
    }

    std::string log_path;
    if (parser_arg.Get("L", log_path) != common::kParseSuccess) {
        log_path = "log/zjchain.log";
    }

    if (!conf_.Set("log", "path", log_path)) {
        INIT_ERROR("set config failed [log][log_path][%s]", log_path.c_str());
        return kInitError;
    }
    return kInitSuccess;
}

int NetworkInit::ParseParams(int argc, char** argv, common::ParserArgs& parser_arg) {
    parser_arg.AddArgType('h', "help", common::kNoValue);
    parser_arg.AddArgType('g', "show_cmd", common::kMaybeValue);
    parser_arg.AddArgType('p', "peer", common::kMaybeValue);
    parser_arg.AddArgType('f', "first_node", common::kMaybeValue);
    parser_arg.AddArgType('l', "local_port", common::kMaybeValue);
    parser_arg.AddArgType('a', "local_ip", common::kMaybeValue);
    parser_arg.AddArgType('o', "country_code", common::kMaybeValue);
    parser_arg.AddArgType('k', "private_key", common::kMaybeValue);
    parser_arg.AddArgType('n', "network", common::kMaybeValue);
    parser_arg.AddArgType('c', "config_path", common::kMaybeValue);
    parser_arg.AddArgType('d', "db_path", common::kMaybeValue);
    parser_arg.AddArgType('v', "version", common::kNoValue);
    parser_arg.AddArgType('L', "log_path", common::kMaybeValue);
    parser_arg.AddArgType('i', "id", common::kMaybeValue);
    parser_arg.AddArgType('V', "vpn_vip_level", common::kNoValue);
    parser_arg.AddArgType('U', "gen_root", common::kNoValue);
    parser_arg.AddArgType('S', "gen_shard", common::kMaybeValue);
    // parser_arg.AddArgType('1', "root_nodes", common::kMaybeValue);    

    for (uint32_t arg_i = network::kConsensusShardBeginNetworkId-1; arg_i < network::kConsensusShardEndNetworkId; arg_i++) {
        std::string arg_shard = std::to_string(arg_i);
        std::string name = "shard_nodes" + arg_shard;
        parser_arg.AddArgType(arg_shard[0], name.c_str(), common::kMaybeValue);
    }

    std::string tmp_params = "";
    for (int i = 1; i < argc; i++) {
        if (strlen(argv[i]) == 0) {
            tmp_params += static_cast<char>(31);
        }
        else {
            tmp_params += argv[i];
        }
        tmp_params += " ";
    }

    std::string err_pos;
    if (parser_arg.Parse(tmp_params, err_pos) != common::kParseSuccess) {
        INIT_ERROR("parse params failed!");
        return kInitError;
    }
    return kInitSuccess;
}

int NetworkInit::GenesisCmd(common::ParserArgs& parser_arg) {
    if (!parser_arg.Has("U") && !parser_arg.Has("S")) {
        return -1;
    }
    
    std::set<uint32_t> valid_net_ids_set;
    std::string valid_arg_i_value;
    YAML::Node genesis_config = YAML::LoadFile("./genesis.yml");

    if (parser_arg.Has("U")) {
        valid_net_ids_set.clear();
        valid_net_ids_set.insert(network::kRootCongressNetworkId);
        for (uint32_t net_i = 0; net_i < genesis_config["shards"].size(); net_i++) {
            valid_net_ids_set.insert(genesis_config["shards"][net_i]["net_id"].as<uint32_t>());
        }

        if (valid_net_ids_set.size() == 0) {
            return kInitError;
        }
        
        auto db = std::make_shared<db::Db>();
        if (!db->Init("./root_db")) {
            INIT_ERROR("init db failed!");
            return kInitError;
        }

        account_mgr_ = std::make_shared<block::AccountManager>();
        block_mgr_ = std::make_shared<block::BlockManager>(net_handler_);
        init::GenesisBlockInit genesis_block(account_mgr_, block_mgr_, db);
        genesis_block.SetGenesisConfig(genesis_config);
        
        std::vector<GenisisNodeInfoPtr> root_genesis_nodes;
        std::vector<GenisisNodeInfoPtrVector> cons_genesis_nodes_of_shards(network::kConsensusShardEndNetworkId-network::kConsensusShardBeginNetworkId);

        GetNetworkNodesFromConf(genesis_config, root_genesis_nodes, cons_genesis_nodes_of_shards);

        if (genesis_block.CreateGenesisBlocks(
                GenisisNetworkType::RootNetwork,
                root_genesis_nodes,
                cons_genesis_nodes_of_shards,
                valid_net_ids_set) != 0) {
            return kInitError;
        }

        return kInitSuccess;
    }

    if (parser_arg.Has("S")) {
        std::string net_id_str;
        if (parser_arg.Get("S", net_id_str) != common::kParseSuccess) {
            return kInitError;
        }

        uint32_t net_id = static_cast<uint32_t>(std::stoul(net_id_str));
        // shard3 创世时需要 root 节点参与
        if (net_id == network::kConsensusShardBeginNetworkId) {
            valid_net_ids_set.insert(network::kRootCongressNetworkId);
        }
        valid_net_ids_set.insert(net_id);

        if (valid_net_ids_set.size() == 0) {
            return kInitError;
        }
        
        ZJC_DEBUG("save shard db: shard_db");
        auto db = std::make_shared<db::Db>();
        if (!db->Init("./shard_db_" + net_id_str)) {
            INIT_ERROR("init db failed!");
            return kInitError;
        }

        account_mgr_ = std::make_shared<block::AccountManager>();
        block_mgr_ = std::make_shared<block::BlockManager>(net_handler_);
        init::GenesisBlockInit genesis_block(account_mgr_, block_mgr_, db);
        genesis_block.SetGenesisConfig(genesis_config);

        std::vector<GenisisNodeInfoPtr> root_genesis_nodes;
        std::vector<GenisisNodeInfoPtrVector> cons_genesis_nodes_of_shards(network::kConsensusShardEndNetworkId-network::kConsensusShardBeginNetworkId);

        GetNetworkNodesFromConf(genesis_config, root_genesis_nodes, cons_genesis_nodes_of_shards);

        if (genesis_block.CreateGenesisBlocks(
                GenisisNetworkType::ShardNetwork,
                root_genesis_nodes,
                cons_genesis_nodes_of_shards,
                valid_net_ids_set) != 0) {
            return kInitError;
        }

        return kInitSuccess;
    }

    return -1;
}

void NetworkInit::GetNetworkNodesFromConf(const YAML::Node& genesis_config,
                                          std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
                                          std::vector<GenisisNodeInfoPtrVector>& cons_genesis_nodes_of_shards) {
            if (genesis_config["root"]) {
            auto root_config = genesis_config["root"];
            if (root_config["sks"]) {
                for (uint32_t i = 0; i < root_config["sks"].size(); i++) {
                    std::string sk = root_config["sks"][i].as<std::string>();
                    auto node_ptr = std::make_shared<GenisisNodeInfo>();
                    node_ptr->prikey = common::Encode::HexDecode(sk);
                    std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
                    secptr->SetPrivateKey(node_ptr->prikey);
                    node_ptr->pubkey = secptr->GetPublicKey();
                    node_ptr->id = secptr->GetAddress(node_ptr->pubkey);
                    root_genesis_nodes.push_back(node_ptr);                    
                }
            }
        }
        
        uint32_t shard_num = network::kConsensusShardEndNetworkId-network::kConsensusShardBeginNetworkId;        
        if (genesis_config["shards"]) {
            assert(genesis_config["shards"].size() == shard_num);
            
            for (uint32_t net_i = 0; net_i < genesis_config["shards"].size(); net_i++) {
                auto shard_config = genesis_config["shards"][net_i];
                std::vector<GenisisNodeInfoPtr> cons_genesis_nodes;
                uint32_t net_id = shard_config["net_id"].as<uint32_t>();
                
                for (uint32_t i = 0; i < shard_config["sks"].size(); i++) {        
                    std::string sk = shard_config["sks"][i].as<std::string>();
                    auto node_ptr = std::make_shared<GenisisNodeInfo>();
                    node_ptr->prikey = common::Encode::HexDecode(sk);
                    std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
                    secptr->SetPrivateKey(node_ptr->prikey);
                    node_ptr->pubkey = secptr->GetPublicKey();
                    node_ptr->id = secptr->GetAddress(node_ptr->pubkey);
                    cons_genesis_nodes.push_back(node_ptr);        
                }
                
                cons_genesis_nodes_of_shards[net_id-network::kConsensusShardBeginNetworkId] = cons_genesis_nodes;
            }
        }
}

void NetworkInit::AddBlockItemToCache(
        uint8_t thread_idx,
        std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch) {
    if (!block->is_commited_block()) {
        assert(false);
        return;
    }

    if (prefix_db_->BlockExists(block->hash())) {
        ZJC_DEBUG("failed cache new block coming sharding id: %u, pool: %d, height: %lu, tx size: %u, hash: %s",
            block->network_id(),
            block->pool_index(),
            block->height(),
            block->tx_list_size(),
            common::Encode::HexEncode(block->hash()).c_str());
//         assert(false);
        return;
    }

    if (prefix_db_->BlockExists(block->network_id(), block->pool_index(), block->height())) {
        ZJC_DEBUG("failed cache new block coming sharding id: %u, pool: %d, height: %lu, tx size: %u, hash: %s",
            block->network_id(),
            block->pool_index(),
            block->height(),
            block->tx_list_size(),
            common::Encode::HexEncode(block->hash()).c_str());
//         assert(false);
        return;
    }

    const auto& tx_list = block->tx_list();
    if (tx_list.empty()) {
        assert(false);
        return;
    }

    ZJC_DEBUG("cache new block coming sharding id: %u, pool: %d, height: %lu, tx size: %u, hash: %s",
        block->network_id(),
        block->pool_index(),
        block->height(),
        block->tx_list_size(),
        common::Encode::HexEncode(block->hash()).c_str());
    if (block->network_id() == common::GlobalInfo::Instance()->network_id() ||
            block->network_id() + network::kConsensusWaitingShardOffset ==
            common::GlobalInfo::Instance()->network_id()) {
        pools_mgr_->UpdateLatestInfo(
            thread_idx,
            block->network_id(),
            block->pool_index(),
            block->height(),
            block->hash(),
            block->prehash(),
            db_batch);
    }
    
    // one block must be one consensus pool
    for (int32_t i = 0; i < tx_list.size(); ++i) {
//         if (tx_list[i].status() != consensus::kConsensusSuccess) {
//             continue;
//         }

        switch (tx_list[i].step()) {
        case pools::protobuf::kNormalFrom:
        case pools::protobuf::kRootCreateAddress:
        case pools::protobuf::kJoinElect:
        case pools::protobuf::kContractGasPrepayment:
            account_mgr_->NewBlockWithTx(thread_idx, block, tx_list[i], db_batch);
            break;
        case pools::protobuf::kConsensusLocalTos:
        case pools::protobuf::kContractCreate:
        case pools::protobuf::kContractExcute:
            account_mgr_->NewBlockWithTx(thread_idx, block, tx_list[i], db_batch);
            gas_prepayment_->NewBlockWithTx(thread_idx, block, tx_list[i], db_batch);
            zjcvm::Execution::Instance()->NewBlockWithTx(thread_idx, block, tx_list[i], db_batch);
            break;
        default:
            break;
        }
    }
}

// pool tx thread, thread safe
bool NetworkInit::DbNewBlockCallback(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch) {
    for (int32_t i = 0; i < block->tx_list_size(); ++i) {
        switch (block->tx_list(i).step()) {
        case pools::protobuf::kConsensusRootTimeBlock:
            HandleTimeBlock(thread_idx, block, block->tx_list(i), db_batch);
            break;
        case pools::protobuf::kConsensusRootElectShard:
            HandleElectionBlock(thread_idx, block, block->tx_list(i), db_batch);
            break;
        default:
            break;
        }
    }

    shard_statistic_->OnNewBlock(*block);
    return true;
}

void NetworkInit::HandleTimeBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kAttrTimerBlock) {
            if (tx.storages(i).val_hash().size() != 16) {
                return;
            }

            uint64_t* data_arr = (uint64_t*)tx.storages(i).val_hash().c_str();
            vss_mgr_->OnTimeBlock(data_arr[0], block->height(), data_arr[1]);
            tm_block_mgr_->OnTimeBlock(data_arr[0], block->height(), data_arr[1]);
            bls_mgr_->OnTimeBlock(data_arr[0], block->height(), data_arr[1]);
            shard_statistic_->OnTimeBlock(data_arr[0], block->height(), data_arr[1]);
            block_mgr_->OnTimeBlock(thread_idx, data_arr[0], block->height(), data_arr[1]);
            ZJC_DEBUG("new time block called height: %lu, tm: %lu", block->height(), data_arr[1]);
        }

        if (tx.storages(i).key() == protos::kAttrGenesisTimerBlock) {
            if (tx.storages(i).key() == protos::kAttrGenesisTimerBlock) {
                prefix_db_->SaveGenesisTimeblock(block->height(), block->timestamp(), db_batch);
            }
        }
    }
}

void NetworkInit::HandleElectionBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block,
        const block::protobuf::BlockTx& block_tx,
        db::DbWriteBatch& db_batch) {
    ZJC_DEBUG("new elect block coming, net: %u, pool: %u, height: %lu",
        block->network_id(), block->pool_index(), block->height());
    auto elect_block = std::make_shared<elect::protobuf::ElectBlock>();
    auto prev_elect_block = std::make_shared<elect::protobuf::ElectBlock>();
    for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
        if (block_tx.storages(i).key() == protos::kElectNodeAttrElectBlock) {
            std::string val;
            if (!prefix_db_->GetTemporaryKv(block_tx.storages(i).val_hash(), &val)) {
                ZJC_FATAL("elect block get temp kv from db failed!");
                return;
            }

            if (!elect_block->ParseFromString(val)) {
                ZJC_FATAL("parse elect block failed!");
                return;
            }

            std::string hash = protos::GetElectBlockHash(*elect_block);
            if (hash != block_tx.storages(i).val_hash()) {
                ZJC_FATAL("parse elect block failed!");
                return;
            }
        }

        if (block_tx.storages(i).key() == protos::kShardElectionPrevInfo) {
            std::string val;
            if (!prefix_db_->GetTemporaryKv(block_tx.storages(i).val_hash(), &val)) {
                ZJC_FATAL("elect block get temp kv from db failed!");
                return;
            }

            if (!prev_elect_block->ParseFromString(val)) {
                ZJC_FATAL("parse elect block failed!");
                return;
            }

            std::string hash = protos::GetElectBlockHash(*prev_elect_block);
            if (hash != block_tx.storages(i).val_hash()) {
                ZJC_FATAL("parse elect block failed!");
                return;
            }

            ZJC_INFO("success get prev elect block.");
        }
    }

    if (!elect_block->has_shard_network_id() ||
            elect_block->shard_network_id() >= network::kConsensusShardEndNetworkId ||
            elect_block->shard_network_id() < network::kRootCongressNetworkId) {
        ZJC_FATAL("parse elect block failed!");
        return;
    }

    auto members = elect_mgr_->OnNewElectBlock(
        thread_idx,
        block->height(),
        elect_block,
        prev_elect_block,
        db_batch);
    if (members == nullptr) {
        ZJC_ERROR("elect manager handle elect block failed!");
        return;
    }

    auto sharding_id = elect_block->shard_network_id();
    auto elect_height = elect_mgr_->latest_height(sharding_id);
    libff::alt_bn128_G2 common_pk;
    libff::alt_bn128_Fr sec_key;
    auto tmp_members = elect_mgr_->GetNetworkMembersWithHeight(
        elect_height,
        sharding_id,
        &common_pk,
        &sec_key);
    if (tmp_members == nullptr) {
        return;
    }

    if (sharding_id == common::GlobalInfo::Instance()->network_id()) {
        if (latest_elect_height_ < elect_height) {
            latest_elect_height_ = elect_height;
            memset(invalid_pools_, 0, sizeof(invalid_pools_));
            auto rotation_leaders = std::make_shared<LeaderRotationInfo>();
            rotation_leaders->elect_height = elect_height;
            uint32_t leader_count = 0;
            std::map<uint32_t, uint32_t> leader_idx_map;
            std::vector<uint32_t> rotaton_members;
            for (uint32_t i = 0; i < members->size(); ++i) {
                if ((*members)[i]->pool_index_mod_num >= 0) {
                    ++leader_count;
                    leader_idx_map[(*members)[i]->pool_index_mod_num] = i;
                    rotation_leaders->rotation_used[i] = true;
                } else {
                    if ((*members)[i]->bls_publick_key == libff::alt_bn128_G2::zero()) {
                        rotation_leaders->rotation_used[i] = true;
                    } else {
                        rotaton_members.push_back(i);
                    }
                }

                if ((*members)[i]->id == security_->GetAddress()) {
                    rotation_leaders->local_member_index = i;
                }
            }

            rotation_leaders->rotations.resize(leader_count);
            rotation_leaders->members = members;
            rotation_leaders->tm_block_tm = tm_block_mgr_->LatestTimestamp();
            uint32_t random_seed = 19245u;
            for (auto iter = leader_idx_map.begin(); iter != leader_idx_map.end(); ++iter) {
                rotation_leaders->rotations[iter->first].now_leader_idx = iter->second;
                rotation_leaders->rotations[iter->first].rotation_leaders = rotaton_members;
                auto& vec = rotation_leaders->rotations[iter->first].rotation_leaders;
                std::shuffle(vec.begin(), vec.end(), std::default_random_engine(random_seed++));
                std::string debug_str;
                for (uint32_t i = 0; i < vec.size(); ++i) {
                    debug_str += std::to_string(vec[i]) + " ";
                }

                ZJC_DEBUG("random_seed: %u, set rotations: %s", random_seed - 1, debug_str.c_str());
            }

            rotation_leaders_ = rotation_leaders;
        }
    }

    bft_mgr_->OnNewElectBlock(block->timestamp(),sharding_id, elect_height, members, common_pk, sec_key);
    block_mgr_->OnNewElectBlock(sharding_id, elect_height, members);
    vss_mgr_->OnNewElectBlock(sharding_id, elect_height, members);
    bls_mgr_->OnNewElectBlock(sharding_id, block->height(), elect_block);
    pools_mgr_->OnNewElectBlock(sharding_id, elect_height, members);
    shard_statistic_->OnNewElectBlock(sharding_id, block->height(), elect_height);
    kv_sync_->OnNewElectBlock(sharding_id, block->height());
    network::UniversalManager::Instance()->OnNewElectBlock(sharding_id, elect_height, members, elect_block);
    ZJC_DEBUG("1 success called election block. height: %lu, elect height: %lu, used elect height: %lu, net: %u, local net id: %u",
        block->height(), elect_height, block->electblock_height(), elect_block->shard_network_id(), common::GlobalInfo::Instance()->network_id());
    if (sharding_id + network::kConsensusWaitingShardOffset ==
            common::GlobalInfo::Instance()->network_id()) {
        join_elect_tick_.CutOff(
            uint64_t(rand()) % (common::kTimeBlockCreatePeriodSeconds / 4 * 3 * 1000000lu),
            std::bind(&NetworkInit::SendJoinElectTransaction, this, std::placeholders::_1));
        ZJC_DEBUG("now send join elect request transaction.");
    }
}

bool NetworkInit::BlockBlsAggSignatureValid(
        uint8_t thread_idx,
        const block::protobuf::Block& block) try {
    if (block.bls_agg_sign_x().empty() || block.bls_agg_sign_y().empty()) {
        assert(false);
        return false;
    }

    auto block_hash = consensus::GetBlockHash(block);
    if (block_hash != block.hash()) {
        assert(false);
        return false;
    }

    libff::alt_bn128_G2 common_pk = libff::alt_bn128_G2::zero();
    auto members = elect_mgr_->GetNetworkMembersWithHeight(
        block.electblock_height(),
        block.network_id(),
        &common_pk,
        nullptr);
    if (members == nullptr || common_pk == libff::alt_bn128_G2::zero()) {
        ZJC_ERROR("failed get elect members or common pk: %u, %lu, %d",
            block.network_id(),
            block.electblock_height(),
            (common_pk == libff::alt_bn128_G2::zero()));
       kv_sync_->AddSyncElectBlock(
           thread_idx,
           network::kRootCongressNetworkId,
           block.network_id(),
           block.electblock_height(),
           sync::kSyncHigh);
        return false;
    }

    libff::alt_bn128_G1 sign;
    sign.X = libff::alt_bn128_Fq(common::Encode::HexEncode(block.bls_agg_sign_x()).c_str());
    sign.Y = libff::alt_bn128_Fq(common::Encode::HexEncode(block.bls_agg_sign_y()).c_str());
    sign.Z = libff::alt_bn128_Fq::one();
    auto g1_hash = libBLS::Bls::Hashing(block_hash);
    bool check_res = libBLS::Bls::Verification(g1_hash, sign, common_pk);
    if (!check_res) {
        ZJC_ERROR("verification agg sign failed hash: %s, signx: %s, common pk x: %s",
            common::Encode::HexEncode(block_hash).c_str(),
            common::Encode::HexEncode(block.bls_agg_sign_x()).c_str(),
            libBLS::ThresholdUtils::fieldElementToString(common_pk.X.c0).c_str());
        //assert(check_res);
    }

    return check_res;
} catch (std::exception& e) {
    ZJC_ERROR("get invalid bls sign: %s, net: %u, height: %lu, prehash: %s, hash: %s, sign: %s, %s",
        e.what(), block.network_id(), block.height(),
        common::Encode::HexEncode(block.prehash()).c_str(),
        common::Encode::HexEncode(block.hash()).c_str(),
        common::Encode::HexEncode(block.bls_agg_sign_x()).c_str(),
        common::Encode::HexEncode(block.bls_agg_sign_y()).c_str());
    return false;
}

}  // namespace init

}  // namespace zjchain
