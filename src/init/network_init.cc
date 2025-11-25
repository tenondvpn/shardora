#include "init/network_init.h"
#include <bls/agg_bls.h>
#include <common/encode.h>
#include <common/log.h>
#include <common/utils.h>
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/hotstuff_manager.h>
#include <consensus/hotstuff/leader_rotation.h>
#include <consensus/hotstuff/pacemaker.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/hotstuff/hotstuff_syncer.h>
#include <consensus/consensus_utils.h>
#include <functional>
#include <libff/algebra/curves/alt_bn128/alt_bn128_init.hpp>
#include <memory>
#include <protos/pools.pb.h>
#include <tools/utils.h>

#include "block/block_manager.h"
#include "common/global_info.h"
#include "common/ip.h"
#include "common/parse_args.h"
#include "common/split.h"
#include "common/string_utils.h"
#include "common/random.h"
#include "common/time_utils.h"
#include "db/db.h"
#include "db/db_utils.h"
#include "elect/elect_manager.h"
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
#include "common/defer.h"

namespace shardora {

namespace init {

static const std::string kDefaultConfigPath("./conf/zjchain.conf");
static const uint32_t kDefaultBufferSize = 1024u * 1024u;
static const std::string kInitJoinWaitingPoolDbKey = "__kInitJoinWaitingPoolDbKey";

NetworkInit::NetworkInit() {}

NetworkInit::~NetworkInit() {
    
}

int NetworkInit::Init(int argc, char** argv) {
    ZJC_DEBUG("init 0 0");
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

    std::string db_path = "./db";
    conf_.Get("zjchain", "db_path", db_path);
    db_ = std::make_shared<db::Db>();
    if (!db_->Init(db_path)) {
        INIT_ERROR("init db failed!");
        return kInitError;
    }

    common::Ip::Instance();
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    ZJC_DEBUG("init 0 1");
    contract_mgr_ = std::make_shared<contract::ContractManager>();
    contract_mgr_->Init(security_);
    common::ParserArgs parser_arg;
    if (ParseParams(argc, argv, parser_arg) != kInitSuccess) {
        INIT_ERROR("parse params failed!");
        return kInitError;
    }

    std::string net_name;
    int genesis_check = GenesisCmd(parser_arg, net_name);
    if (genesis_check != -1) {
        common::GlobalInfo::Instance()->set_global_stoped();
        std::cout << net_name << " genesis cmd over, exit." << std::endl;
        return genesis_check;
    }

    // Init agg bls
    if (bls::AggBls::Instance()->Init(prefix_db_, security_) != common::kCommonSuccess) {
        return kInitError;
    }    

    // uint32_t ws_server = 0;
    // conf_.Get("zjchain", "ws_server", ws_server);
    // if (ws_server > 0) {
    //     if (ws_server_.Init(prefix_db_, security_, &net_handler_) != kInitSuccess) {
    //         ZJC_ERROR("init ws server failed!");
    //         return kInitError;
    //     }
    // }

    // 随机数
    vss_mgr_ = std::make_shared<vss::VssManager>();
    kv_sync_ = std::make_shared<sync::KeyValueSync>();
    ZJC_INFO("init 0 4");
    InitLocalNetworkId();
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        uint32_t config_net_id = 0;
        if (conf_.Get("zjchain", "net_id", config_net_id) &&
                config_net_id >= network::kRootCongressNetworkId && 
                config_net_id <= network::kConsensusShardEndNetworkId) {
            common::GlobalInfo::Instance()->set_network_id(
                config_net_id + network::kConsensusWaitingShardOffset);
        } else {
            INIT_ERROR("init network id failed!");
            return kInitError;
        }
    }

    ZJC_INFO("id: %s, init sharding id: %u",
        common::Encode::HexEncode(security_->GetAddress()).c_str(),
        common::GlobalInfo::Instance()->network_id());
    ZJC_INFO("init 0 5");
    if (net_handler_.Init(db_, security_) != transport::kTransportSuccess) {
        return kInitError;
    }

    ZJC_DEBUG("init 0 6");
    net_handler_.Start();
    ZJC_DEBUG("init 0 7");
    int transport_res = transport::TcpTransport::Instance()->Init(
        common::GlobalInfo::Instance()->config_local_ip() + ":" +
        std::to_string(common::GlobalInfo::Instance()->config_local_port()),
        128,
        true,
        &net_handler_);
    ZJC_DEBUG("init 0 8");
    if (transport_res != transport::kTransportSuccess) {
        INIT_ERROR("int tcp transport failed!");
        return kInitError;
    }

    ZJC_DEBUG("init 0 9");
    network::DhtManager::Instance();
    network::Route::Instance()->Init(security_);
    network::Route::Instance()->RegisterMessage(
        common::kInitMessage,
        std::bind(&NetworkInit::HandleMessage, this, std::placeholders::_1));
    account_mgr_ = std::make_shared<block::AccountManager>();
    network::UniversalManager::Instance()->Init(security_, db_, account_mgr_);
    ZJC_DEBUG("init 0 10");
    if (InitNetworkSingleton() != kInitSuccess) {
        INIT_ERROR("InitNetworkSingleton failed!");
        return kInitError;
    }

    auto ck_client = std::make_shared<ck::ClickHouseClient>("127.0.0.1", "", "", db_, contract_mgr_);
    auto block_ck_client = ck_client;
    if (!common::GlobalInfo::Instance()->for_ck_server()) {
        block_ck_client = nullptr;
    }

    block_mgr_ = std::make_shared<block::BlockManager>(net_handler_, block_ck_client);
    bls_mgr_ = std::make_shared<bls::BlsManager>(security_, db_, ck_client);
    elect_mgr_ = std::make_shared<elect::ElectManager>(
        vss_mgr_, account_mgr_, block_mgr_, security_, bls_mgr_, db_,
        nullptr);
    pools_mgr_ = std::make_shared<pools::TxPoolManager>(
        security_, db_, kv_sync_, account_mgr_);
    account_mgr_->Init(db_, pools_mgr_);
    zjcvm::Execution::Instance()->Init(db_);
    auto new_db_cb = std::bind(
        &NetworkInit::DbNewBlockCallback,
        this,
        std::placeholders::_1,
        std::placeholders::_2);
    shard_statistic_ = std::make_shared<pools::ShardStatistic>(
        elect_mgr_, db_, security_, pools_mgr_, contract_mgr_);
    tm_block_mgr_ = std::make_shared<timeblock::TimeBlockManager>();
    hotstuff_mgr_ = std::make_shared<consensus::HotstuffManager>();
    block_mgr_->Init(
        account_mgr_,
        db_,
        pools_mgr_,
        shard_statistic_,
        security_,
        contract_mgr_,
        hotstuff_mgr_,
        security_->GetAddress(),
        new_db_cb);
    auto consensus_init_res = hotstuff_mgr_->Init(
        kv_sync_,
        contract_mgr_,
        vss_mgr_,
        account_mgr_,
        block_mgr_,
        elect_mgr_,
        pools_mgr_,
        security_,
        tm_block_mgr_,
        bls_mgr_,
        db_,
        std::bind(&NetworkInit::AddBlockItemToCache, this,
            std::placeholders::_1, std::placeholders::_2));
    if (consensus_init_res != consensus::kConsensusSuccess) {
        INIT_ERROR("init bft failed!");
        return kInitError;
    }

    ZJC_WARN("init hotstuff_mgr_ success.");
    kv_sync_->Init(
        block_mgr_,
        hotstuff_mgr_,
        db_,
        std::bind(&consensus::HotstuffManager::VerifySyncedViewBlock,
            hotstuff_mgr_, std::placeholders::_1));
    ZJC_WARN("init kv_sync_ success.");
    tm_block_mgr_->Init(vss_mgr_,account_mgr_);
    ZJC_WARN("init tm_block_mgr_ success.");
    if (elect_mgr_->Init() != elect::kElectSuccess) {
        INIT_ERROR("init elect manager failed!");
        return kInitError;
    }

    ZJC_WARN("init elect_mgr_ success.");
    if (common::GlobalInfo::Instance()->network_id() != common::kInvalidUint32 &&
            common::GlobalInfo::Instance()->network_id() >= network::kConsensusShardEndNetworkId) {
        if (elect_mgr_->Join(
                common::GlobalInfo::Instance()->network_id()) != elect::kElectSuccess) {
            INIT_ERROR("join waiting pool network[%u] failed!",
                common::GlobalInfo::Instance()->network_id());
            return kInitError;
        }
    }

    if (shard_statistic_->Init() != pools::kPoolsSuccess) {
        INIT_ERROR("init shard statistic failed!");
        return kInitError;
    }

    ZJC_WARN("init shard_statistic_ success.");
    block_mgr_->LoadLatestBlocks();
    // 启动共识和同步
    hotstuff_syncer_ = std::make_shared<hotstuff::HotstuffSyncer>(
        hotstuff_mgr_, db_, kv_sync_, account_mgr_);
    RegisterFirewallCheck();
    hotstuff_syncer_->Start();
    hotstuff_mgr_->Start();
    // 以上应该放入 hotstuff 实例初始化中，并接收创世块
    ZJC_WARN("init hotstuff_mgr_ start success.");
    AddCmds();
    transport::TcpTransport::Instance()->Start(false);
    ZJC_DEBUG("init 6");
    if (InitHttpServer() != kInitSuccess) {
        INIT_ERROR("InitHttpServer failed!");
        return kInitError;
    }
    ZJC_DEBUG("init 7");
    GetAddressShardingId();
    if (InitCommand() != kInitSuccess) {
        INIT_ERROR("InitCommand failed!");
        return kInitError;
    }

    inited_ = true;
    common::GlobalInfo::Instance()->set_main_inited_success();
    cmd_.AddCommand("gs", [this](const std::vector<std::string>& args) {
        if (args.size() < 3) {
            return;
        }
        uint32_t shard_id = std::stoi(args[0]);
        std::string addr = args[1];
        uint64_t elect_height = std::stoull(args[2]);
        std::string con_addr = "";
        if (shard_id < 1){
           con_addr = args[3];
        }
        std::cout << "shard_id: " << shard_id << std::endl;
        std::cout << "addr: " << addr << std::endl;
        std::cout << "elect_height: " << elect_height << std::endl;
        std::cout << "con_addr: " << con_addr << std::endl;
        auto stoke = 0;
        std::cout << "stoke: " << stoke << std::endl;

    });

    cmd_.Run();
    // std::this_thread::sleep_for(std::chrono::seconds(120));
    return kInitSuccess;
}

int NetworkInit::InitWsServer() {
    // int32_t ws_server = 0;
    // conf_.Get("zjchain", "ws_server", ws_server);
    // if (ws_server > 0) {
    //     if (ws_server_.Init(prefix_db_, security_, &net_handler_) != kInitSuccess) {
    //         ZJC_ERROR("init ws server failed!");
    //         return kInitError;
    //     }

    //     if (ws_server > 1) {
    //         int transport_res = transport::TcpTransport::Instance()->Init(
    //             common::GlobalInfo::Instance()->config_local_ip() + ":" +
    //             std::to_string(common::GlobalInfo::Instance()->config_local_port()),
    //             128,
    //             true,
    //             &net_handler_);
    //         if (transport_res != transport::kTransportSuccess) {
    //             INIT_ERROR("int tcp transport failed!");
    //             return kInitError;
    //         }

    //         if (InitHttpServer() != kInitSuccess) {
    //             INIT_ERROR("InitHttpServer failed!");
    //             return kInitError;
    //         }
    //         transport::TcpTransport::Instance()->Start(false);
    //         if (InitCommand() != kInitSuccess) {
    //             INIT_ERROR("InitCommand failed!");
    //             return kInitError;
    //         }

    //         inited_ = true;
    //         cmd_.Run();
    //         return kInitSuccess;
    //     }
    // }

    return kInitSuccess;
}

void NetworkInit::AddCmds() {
    cmd_.AddCommand("pc", [this](const std::vector<std::string>& args){
        if (args.size() < 1) {
            return;
        }
        uint32_t pool_idx = std::stoi(args[0]);

        auto chain = hotstuff_mgr_->chain(pool_idx);
        if (!chain) {
            return;
        }
        auto pacemaker = hotstuff_mgr_->pacemaker(pool_idx);
        if (!pacemaker) {
            return;
        }

        std::cout << "highQC: " << chain->HighViewBlock()->qc().view()
                  << ",highTC: " << pacemaker->HighTC()->view()
                  << ",chainSize: " << chain->Size()
                  << ",commitView: " << chain->LatestCommittedBlock()->qc().view()
                  << ",CurView: " << pacemaker->CurView() << std::endl;
    });
}

void NetworkInit::RegisterFirewallCheck() {
    net_handler_.AddFirewallCheckCallback(
        common::kBlsMessage,
        std::bind(&bls::BlsManager::FirewallCheckMessage, bls_mgr_.get(), std::placeholders::_1));
    net_handler_.AddFirewallCheckCallback(
        common::kHotstuffMessage,
        std::bind(&consensus::HotstuffManager::FirewallCheckMessage, hotstuff_mgr_.get(), std::placeholders::_1));
    net_handler_.AddFirewallCheckCallback(
        common::kHotstuffSyncMessage,
        std::bind(&hotstuff::HotstuffSyncer::FirewallCheckMessage, hotstuff_syncer_.get(), std::placeholders::_1));
    net_handler_.AddFirewallCheckCallback(
        common::kBlockMessage,
        std::bind(&block::BlockManager::FirewallCheckMessage, block_mgr_.get(), std::placeholders::_1));
    net_handler_.AddFirewallCheckCallback(
        common::kSyncMessage,
        std::bind(&sync::KeyValueSync::FirewallCheckMessage, kv_sync_.get(), std::placeholders::_1));
    net_handler_.AddFirewallCheckCallback(
        common::kPoolsMessage,
        std::bind(&pools::TxPoolManager::FirewallCheckMessage, pools_mgr_.get(), std::placeholders::_1));
    net_handler_.AddFirewallCheckCallback(
        common::kInitMessage,
        std::bind(&NetworkInit::FirewallCheckMessage, this, std::placeholders::_1));
}

int NetworkInit::FirewallCheckMessage(transport::MessagePtr& msg_ptr) {
    return transport::kFirewallCheckSuccess;
}

void NetworkInit::HandleMessage(const transport::MessagePtr& msg_ptr) {
    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (msg_ptr->header.init_proto().has_addr_req()) {
        HandleAddrReq(msg_ptr);
    }

    ADD_DEBUG_PROCESS_TIMESTAMP();
    if (msg_ptr->header.init_proto().has_addr_res()) {
        HandleAddrRes(msg_ptr);
    }
    ADD_DEBUG_PROCESS_TIMESTAMP();
}

void NetworkInit::HandleAddrReq(const transport::MessagePtr& msg_ptr) {
    protos::AddressInfoPtr account_info = account_mgr_->GetAccountInfo(
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
            init_msg.mutable_view_block())) {
        return;
    }

    bool tx_valid = false;
    for (int32_t i = 0; i < init_msg.view_block().block_info().tx_list_size(); ++i) {
        if (init_msg.view_block().block_info().tx_list(i).to() == account_info->addr()) {
            tx_valid = true;
            break;
        }
    }

    if (!tx_valid) {
        return;
    }

    ZJC_DEBUG("success handle init req message: %s",
        common::Encode::HexEncode(msg_ptr->header.init_proto().addr_req().id()).c_str());
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    transport::TcpTransport::Instance()->Send(msg_ptr->conn.get(), msg);
}

void NetworkInit::HandleAddrRes(const transport::MessagePtr& msg_ptr) {
    if (des_sharding_id_ != common::kInvalidUint32) {
        return;
    }

    auto& block = msg_ptr->header.init_proto().addr_res().view_block().block_info();
    if (block.tx_list_size() != 1) {
        return;
    }

    uint32_t sharding_id = common::kInvalidUint32;
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

    prefix_db_->SaveJoinShard(sharding_id, des_sharding_id_);
    ZJC_DEBUG("success save local sharding %u, %u", sharding_id, des_sharding_id_);
    auto waiting_network_id = sharding_id + network::kConsensusWaitingShardOffset;
    if (elect_mgr_->Join(waiting_network_id) != elect::kElectSuccess) {
        INIT_ERROR("join waiting pool network[%u] failed!", waiting_network_id);
        return;
    }

    common::GlobalInfo::Instance()->set_network_id(waiting_network_id);
}

void NetworkInit::GetAddressShardingId() {
    if (des_sharding_id_ != common::kInvalidUint32) {
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    auto& msg = msg_ptr->header;
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    msg.set_type(common::kInitMessage);
    dht::DhtKeyManager dht_key(network::kRootCongressNetworkId);
    msg.set_des_dht_key(dht_key.StrKey());
    auto& init_msg = *msg.mutable_init_proto();
    auto& init_req = *init_msg.mutable_addr_req();
    init_req.set_id(security_->GetAddress());
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("sent get addresss info success.");
    init_tick_.CutOff(10000000lu, std::bind(&NetworkInit::GetAddressShardingId, this));
}

void NetworkInit::InitLocalNetworkId() {
    uint32_t got_sharding_id = common::kInvalidUint32;
    if (!prefix_db_->GetJoinShard(&got_sharding_id, &des_sharding_id_)) {
        auto local_node_account_info = prefix_db_->GetAddressInfo(security_->GetAddress());
        if (local_node_account_info == nullptr) {
            ZJC_INFO("failed get local account info id: %s",
                common::Encode::HexEncode(security_->GetAddress()).c_str());
            return;
        }

        got_sharding_id = local_node_account_info->sharding_id();
        des_sharding_id_ = got_sharding_id;
        prefix_db_->SaveJoinShard(got_sharding_id, des_sharding_id_);
        ZJC_INFO("success save local sharding %u, %u", got_sharding_id, des_sharding_id_);
    }

    for (uint32_t sharding_id = network::kRootCongressNetworkId;
            sharding_id < network::kConsensusShardEndNetworkId; ++sharding_id) {
        elect::protobuf::ElectBlock elect_block;
        if (!prefix_db_->GetLatestElectBlock(sharding_id, &elect_block)) {
            ZJC_FATAL("failed!");
        }

        auto& in = elect_block.in();
        for (int32_t member_idx = 0; member_idx < in.size(); ++member_idx) {
            auto id = security_->GetAddress(in[member_idx].pubkey());
            ZJC_INFO("network: %d get member id: %s, local id: %s",
                sharding_id, common::Encode::HexEncode(id).c_str(),
                common::Encode::HexEncode(security_->GetAddress()).c_str());
            // 如果本 node pubkey 与 elect block 当中记录的相同，则分配到对应的 sharding
            if (id == security_->GetAddress()) {
                ZJC_INFO("should join network: %u", sharding_id);
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
    ZJC_INFO("should join waiting network: %u", waiting_network_id);
}

int NetworkInit::InitSecurity() {
    std::string prikey;
    if (!conf_.Get("zjchain", "prikey", prikey)) {
        INIT_ERROR("get private key from config failed!");
        return kInitError;
    }
    ZJC_DEBUG("prikey1: %s", prikey.c_str());
    ZJC_DEBUG("prikey2: %s", common::Encode::HexEncode(common::Encode::HexDecode(prikey)).c_str());

    security_ = std::make_shared<security::Ecdsa>();
    if (security_->SetPrivateKey(
            common::Encode::HexDecode(prikey)) != security::kSecuritySuccess) {
        INIT_ERROR("init security failed!");
        return kInitError;
    }

    return kInitSuccess;
}

static std::condition_variable wait_con_;
static std::mutex wait_mutex_;

int NetworkInit::InitHttpServer() {
    std::string http_ip = "0.0.0.0";
    uint16_t http_port = 0;
    conf_.Get("zjchain", "http_ip", http_ip);
    if (conf_.Get("zjchain", "http_port", http_port) && http_port != 0) {
        http_handler_.Init(
            account_mgr_, 
            &net_handler_, 
            security_, 
            prefix_db_, 
            contract_mgr_, 
            http_ip, 
            http_port);
        httplib::Client cli(http_ip, http_port);
        if (auto res = cli.Get("/query_init")) {
            std::unique_lock<std::mutex> lock(wait_mutex_);
            wait_con_.notify_one();
        }

        ZJC_DEBUG("http init wait response coming.");
        std::unique_lock<std::mutex> lock(wait_mutex_);
        wait_con_.wait_for(lock, std::chrono::milliseconds(1000));
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

    if (network::UniversalManager::Instance()->CreateUniversalNetwork(
            conf_) != network::kNetworkSuccess) {
        INIT_ERROR("create universal network failed!");
        return kInitError;
    }

    if (network::UniversalManager::Instance()->CreateNodeNetwork(
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
    parser_arg.AddArgType('N', "node_count", common::kMaybeValue);
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

void NetworkInit::CreateInitAddress(uint32_t net_id) {
    auto file_name = std::string("/root/shardora/init_accounts") + std::to_string(net_id);
    if (common::isFileExist(file_name)) {
        return;
    }

    auto fd = fopen(file_name.c_str(), "w");
    uint32_t address_count_now = 0;
    // 给每个账户在 net_id 网络中创建块，并分配到不同的 pool 当中
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        while (true) {
            auto private_key = common::Random::RandomString(32);
            security::Ecdsa ecdsa;
            ecdsa.SetPrivateKey(private_key);
            auto address = ecdsa.GetAddress();
            if (common::GetAddressPoolIndex(address) == i) {
                auto data = common::Encode::HexEncode(private_key) + "\t" + common::Encode::HexEncode(ecdsa.GetPublicKey()) + "\n";
                fwrite(data.c_str(), 1, data.size(), fd);
                break;
            }
        }
    }

    fclose(fd);
}

int NetworkInit::GenesisCmd(common::ParserArgs& parser_arg, std::string& net_name) {
    if (!parser_arg.Has("U") && !parser_arg.Has("S")) {
        return -1;
    }

    int consensus_shard_node_count = 4;
    if (parser_arg.Has("N")) {
        if (parser_arg.Get("N", consensus_shard_node_count) != common::kParseSuccess) {
            return -1;
        }
    }
    
    ZJC_DEBUG("now consensus_shard_node_count: %u", consensus_shard_node_count);
    std::set<uint32_t> valid_net_ids_set;
    std::string valid_arg_i_value;
    for (uint32_t net_id = network::kConsensusShardBeginNetworkId; 
            net_id < network::kConsensusShardEndNetworkId; ++net_id) {
        CreateInitAddress(net_id);
    }

    if (parser_arg.Has("U")) {
        net_name = "root2";
        valid_net_ids_set.clear();
        valid_net_ids_set.insert(network::kRootCongressNetworkId);
        valid_net_ids_set.insert(3);
        auto db = std::make_shared<db::Db>();
        if (!db->Init("./root_db")) {
            INIT_ERROR("init db failed!");
            return kInitError;
        }

        account_mgr_ = std::make_shared<block::AccountManager>();
        // account_mgr_->Init(db, pools_mgr_);
        block_mgr_ = std::make_shared<block::BlockManager>(net_handler_, nullptr);
        init::GenesisBlockInit genesis_block(account_mgr_, block_mgr_, db);
        std::vector<GenisisNodeInfoPtr> root_genesis_nodes;
        std::vector<GenisisNodeInfoPtrVector> cons_genesis_nodes_of_shards(
            network::kConsensusShardEndNetworkId-network::kConsensusShardBeginNetworkId);
        GetNetworkNodesFromConf(
            consensus_shard_node_count, 
            root_genesis_nodes, 
            cons_genesis_nodes_of_shards, 
            db);
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

        net_name = "shard" + net_id_str;
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
        // account_mgr_->Init(db, pools_mgr_);
        block_mgr_ = std::make_shared<block::BlockManager>(net_handler_, nullptr);
        init::GenesisBlockInit genesis_block(account_mgr_, block_mgr_, db);
        std::vector<GenisisNodeInfoPtr> root_genesis_nodes;
        std::vector<GenisisNodeInfoPtrVector> cons_genesis_nodes_of_shards(
            network::kConsensusShardEndNetworkId-network::kConsensusShardBeginNetworkId);
        GetNetworkNodesFromConf(
            consensus_shard_node_count, 
            root_genesis_nodes, 
            cons_genesis_nodes_of_shards, 
            db);
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

void NetworkInit::GetNetworkNodesFromConf(
        uint32_t cons_shard_node_count,
        std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        std::vector<GenisisNodeInfoPtrVector>& cons_genesis_nodes_of_shards,
        const std::shared_ptr<db::Db>& db) {
    auto prefix_db = std::make_shared<protos::PrefixDb>(db);
    auto get_sks_func = [](FILE *fd, std::vector<std::string>& sks, int32_t count, bool reuse) {
        if (reuse) {
            char data[1024*1024];
            fread(data, 1, sizeof(data), fd);
            auto lines = common::Split<1024>(data, '\n');
            for (uint32_t i = 0; i < lines.Count(); ++i) {
                auto items = common::Split<>(lines[i], '\t');
                if (items.Count() != 2) {
                    break;
                }

                sks.push_back(common::Encode::HexDecode(items[0]));
                ZJC_DEBUG("reuse private key: %s", items[0]);
            }
        } else {
            for (uint32_t i = 0; i < count; i++) {
                sks.push_back(common::Random::RandomString(32));
                std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
                secptr->SetPrivateKey(sks[i]);
                auto data = common::Encode::HexEncode(sks[i]) + "\t" + common::Encode::HexEncode(secptr->GetPublicKey()) + "\n";
                fwrite(data.c_str(), 1, data.size(), fd);
                ZJC_DEBUG("random private key: %s", common::Encode::HexEncode(sks[i]).c_str());
            }
        }
    };
        
    bool reuse_root = common::isFileExist("/root/shardora/root_nodes");
    auto rfd = fopen("/root/shardora/root_nodes", (reuse_root ? "r" : "w"));
    assert(rfd != nullptr);
    std::vector<std::string> root_sks;
    get_sks_func(rfd, root_sks, 3, reuse_root);
    for (uint32_t i = 0; i < root_sks.size(); i++) {
        std::string& sk = root_sks[i];
        std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
        secptr->SetPrivateKey(sk);
        auto node_ptr = std::make_shared<GenisisNodeInfo>();
        node_ptr->prikey = sk;
        node_ptr->pubkey = secptr->GetPublicKey();
        node_ptr->id = secptr->GetAddress(node_ptr->pubkey);
        InitAggBlsForGenesis(node_ptr->id, secptr, prefix_db);
        auto keypair = bls::AggBls::Instance()->GetKeyPair();
        node_ptr->agg_bls_pk = keypair->pk();
        node_ptr->agg_bls_pk_proof = keypair->proof();
        node_ptr->nonce = 0;
        root_genesis_nodes.push_back(node_ptr);
        ZJC_DEBUG("root private key: %s, id: %s", 
            common::Encode::HexEncode(sk).c_str(), 
            common::Encode::HexEncode(node_ptr->id).c_str());
    }
    fclose(rfd);
    uint32_t shard_num = network::kConsensusShardEndNetworkId-network::kConsensusShardBeginNetworkId;        
    uint32_t n = cons_shard_node_count;
    uint32_t t = common::GetSignerCount(n);
    for (uint32_t net_i = network::kConsensusShardBeginNetworkId; net_i < network::kConsensusShardEndNetworkId; net_i++) {
        auto filename = std::string("/root/shardora/shards") + std::to_string(net_i);
        bool reuse_shard = common::isFileExist(filename);
        auto sfd = fopen(filename.c_str(), (reuse_shard ? "r" : "w"));
        assert(sfd != nullptr);
        std::vector<std::string> shard_sks;
        get_sks_func(sfd, shard_sks, n, reuse_shard);
        std::vector<GenisisNodeInfoPtr> cons_genesis_nodes;
        for (uint32_t i = 0; i < n; i++) {
            ZJC_DEBUG("use private key: %d, %s", i, common::Encode::HexEncode(shard_sks[i]).c_str());
            std::string& sk = shard_sks[i];
            std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
            secptr->SetPrivateKey(sk);
            auto node_ptr = std::make_shared<GenisisNodeInfo>();
            node_ptr->prikey = sk;
            node_ptr->pubkey = secptr->GetPublicKey();
            node_ptr->id = secptr->GetAddress(node_ptr->pubkey);
            InitAggBlsForGenesis(node_ptr->id, secptr, prefix_db);
            auto keypair = bls::AggBls::Instance()->GetKeyPair();
            node_ptr->agg_bls_pk = keypair->pk();
            node_ptr->agg_bls_pk_proof = keypair->proof();
            node_ptr->nonce = 0;
            cons_genesis_nodes.push_back(node_ptr);   
            ZJC_DEBUG("shard: %d private key: %s, id: %s", 
                net_i,
                common::Encode::HexEncode(sk).c_str(), 
                common::Encode::HexEncode(node_ptr->id).c_str());     
        }
        
        cons_genesis_nodes_of_shards[net_i-network::kConsensusShardBeginNetworkId] = cons_genesis_nodes;
        fclose(sfd);
    }
    // }
}

void NetworkInit::InitAggBlsForGenesis(const std::string& node_id, std::shared_ptr<security::Security>& secptr, std::shared_ptr<protos::PrefixDb>& prefix_db) {
    libff::alt_bn128_Fr agg_bls_sk = libff::alt_bn128_Fr::zero();
    GetAggBlsSkFromFile(node_id, &agg_bls_sk);
    if (agg_bls_sk == libff::alt_bn128_Fr::zero()) {
        bls::AggBls::Instance()->Init(prefix_db, secptr);
        WriteAggBlsSkToFile(node_id, bls::AggBls::Instance()->agg_sk());
    } else {
        bls::AggBls::Instance()->InitBySk(agg_bls_sk, prefix_db, secptr);
    }
    return;
}

void NetworkInit::GetAggBlsSkFromFile(const std::string& node_id, libff::alt_bn128_Fr* agg_bls_sk) {
    std::string file = std::string("./agg_bls_sk_") + common::Encode::HexEncode(node_id);
    FILE* fd = fopen(file.c_str(), "r");
    if (fd != nullptr) {
        defer(fclose(fd));
        
        fseek(fd, 0, SEEK_END);
        long file_size = ftell(fd);
        fseek(fd, 0, SEEK_SET);

        if (file_size <= 0) {
            return;
        }

        char* data = new char[file_size+1];
        defer(delete[] data);
        
        if (fgets(data, file_size+1, fd) == nullptr) {
            return;
        }

        std::string tmp_data(data, strlen(data)-1);
        *agg_bls_sk = libff::alt_bn128_Fr(tmp_data.c_str());
    }
    return;
}

void NetworkInit::WriteAggBlsSkToFile(const std::string& node_id, const libff::alt_bn128_Fr& agg_bls_sk) {
    std::string file = std::string("./agg_bls_sk_") + common::Encode::HexEncode(node_id);
    FILE* fd = fopen(file.c_str(), "w");
    defer(fclose(fd));
    
    std::string val = libBLS::ThresholdUtils::fieldElementToString(agg_bls_sk) + "\n";
    fputs(val.c_str(), fd);
    return;
}

void NetworkInit::AddBlockItemToCache(
        std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
        db::DbWriteBatch& db_batch) {
    auto* block = &view_block->block_info();
    ZJC_DEBUG("cache new block coming sharding id: %u_%d_%lu, tx size: %u, hash: %s",
        view_block->qc().network_id(),
        view_block->qc().pool_index(),
        block->height(),
        block->tx_list_size(),
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str());
    if (network::IsSameToLocalShard(view_block->qc().network_id())) {
        pools_mgr_->UpdateLatestInfo(view_block, db_batch);
    } else {
        pools_mgr_->UpdateCrossLatestInfo(view_block, db_batch);
    }

    DbNewBlockCallback(view_block, db_batch);
}

// pool tx thread, thread safe
bool NetworkInit::DbNewBlockCallback(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
        db::DbWriteBatch& db_batch) {
    auto* block = &view_block->block_info();
    for (int32_t i = 0; i < block->tx_list_size(); ++i) {
        switch (block->tx_list(i).step()) {
        case pools::protobuf::kConsensusRootTimeBlock:
            HandleTimeBlock(view_block, block->tx_list(i), db_batch);
            break;
        case pools::protobuf::kConsensusRootElectShard:
            HandleElectionBlock(view_block, block->tx_list(i), db_batch);
            break;
        default:
            break;
        }
    }

    return true;
}

void NetworkInit::HandleTimeBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    ZJC_INFO("time block coming %u_%u_%lu, %u_%u_%lu, tm: %lu, vss: %lu",
        view_block->qc().network_id(), 
        view_block->qc().pool_index(), 
        view_block->qc().view(), 
        view_block->qc().network_id(), 
        view_block->qc().pool_index(), 
        view_block->block_info().height(),
        view_block->block_info().timer_block().timestamp(),
        view_block->block_info().timer_block().vss_random());
    auto& block = view_block->block_info();
    if (block.has_timer_block()) {
        auto vss_random = block.timer_block().vss_random();
        hotstuff_mgr_->OnTimeBlock(block.timer_block().timestamp(), block.height(), vss_random);
        vss_mgr_->OnTimeBlock(view_block);
        bls_mgr_->OnTimeBlock(block.timer_block().timestamp(), block.height(), vss_random);
        shard_statistic_->OnTimeBlock(block.timer_block().timestamp(), block.height(), vss_random);
        block_mgr_->OnTimeBlock(block.timer_block().timestamp(), block.height(), vss_random);
        tm_block_mgr_->OnTimeBlock(block.timer_block().timestamp(), block.height(), vss_random);
        ZJC_INFO("new time block called height: %lu, tm: %lu", block.height(), vss_random);
    }
}

void NetworkInit::HandleElectionBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
        const block::protobuf::BlockTx& block_tx,
        db::DbWriteBatch& db_batch) {
    auto* block = &view_block->block_info();
    ZJC_DEBUG("new elect block coming, net: %u, pool: %u, height: %lu",
        view_block->qc().network_id(), view_block->qc().pool_index(), block->height());
    auto elect_block = std::make_shared<elect::protobuf::ElectBlock>();
    auto prev_elect_block = std::make_shared<elect::protobuf::ElectBlock>();
    if (block->has_elect_block()) {
        *elect_block = block->elect_block();
    }

    if (block->has_prev_elect_block()) {
        *prev_elect_block = block->prev_elect_block();
    }

    if (!elect_block->has_shard_network_id() ||
            elect_block->shard_network_id() >= network::kConsensusShardEndNetworkId ||
            elect_block->shard_network_id() < network::kRootCongressNetworkId) {
        ZJC_FATAL("parse elect block failed!");
        return;
    }

    auto members = elect_mgr_->OnNewElectBlock(
        block->height(),
        elect_block,
        prev_elect_block,
        db_batch);
    if (members == nullptr) {
        ZJC_ERROR("elect manager handle elect block failed!");
        return;
    }

    // TODO log members
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

    network::Route::Instance()->OnNewElectBlock(
        sharding_id,
        elect_height,
        members,
        elect_block);
    hotstuff_mgr_->OnNewElectBlock(
        block->timestamp(),sharding_id, elect_height, members, common_pk, sec_key);
    block_mgr_->OnNewElectBlock(sharding_id, elect_height, members);
    bls_mgr_->OnNewElectBlock(sharding_id, block->height(), elect_block);
    pools_mgr_->OnNewElectBlock(sharding_id, elect_height, members);
    shard_statistic_->OnNewElectBlock(sharding_id, block->height(), elect_height);
    kv_sync_->OnNewElectBlock(sharding_id, block->height());
    network::UniversalManager::Instance()->OnNewElectBlock(
        sharding_id,
        elect_height,
        members,
        elect_block);
    ZJC_DEBUG("1 success called election block. height: %lu, "
        "elect height: %lu, used elect height: %lu, net: %u, "
        "local net id: %u, prev elect height: %lu",
        block->height(), elect_height,
        view_block->qc().elect_height(),
        elect_block->shard_network_id(),
        common::GlobalInfo::Instance()->network_id(),
        elect_block->prev_members().prev_elect_height());
    if (sharding_id + network::kConsensusWaitingShardOffset ==
            common::GlobalInfo::Instance()->network_id()) {
        join_elect_tick_.CutOff(
            3000000lu,
            std::bind(&NetworkInit::SendJoinElectTransaction, this));
        ZJC_DEBUG("now send join elect request transaction. first message.");
        another_join_elect_msg_needed_ = true;
    } else if (another_join_elect_msg_needed_ &&
            sharding_id == common::GlobalInfo::Instance()->network_id()) {
        join_elect_tick_.CutOff(
            3000000lu,
            std::bind(&NetworkInit::SendJoinElectTransaction, this));
        ZJC_DEBUG("now send join elect request transaction. second message.");
        another_join_elect_msg_needed_ = false;
    }
}

void NetworkInit::SendJoinElectTransaction() {
    if (common::GlobalInfo::Instance()->network_id() < network::kConsensusShardBeginNetworkId) {
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
    msg_ptr->address_info = account_mgr_->GetAccountInfo(security_->GetAddress());
    transport::protobuf::Header& msg = msg_ptr->header;
    dht::DhtKeyManager dht_key(des_sharding_id_);
    msg.set_src_sharding_id(des_sharding_id_);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    msg.set_hop_count(0);
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_nonce(msg_ptr->address_info->nonce() + 1);
    new_tx->set_pubkey(security_->GetPublicKeyUnCompressed());
    new_tx->set_step(pools::protobuf::kJoinElect);
    new_tx->set_gas_limit(consensus::kJoinElectGas + 10000000lu);
    new_tx->set_gas_price(1);
    new_tx->set_key(protos::kJoinElectVerifyG2);
    bls::protobuf::JoinElectInfo join_info;
    uint32_t pos = common::kInvalidUint32;
    prefix_db_->GetLocalElectPos(security_->GetAddress(), &pos);
    join_info.set_member_idx(pos);
    
    if (common::GlobalInfo::Instance()->network_id() >= network::kConsensusShardEndNetworkId) {
        join_info.set_shard_id(
            common::GlobalInfo::Instance()->network_id() -
            network::kConsensusWaitingShardOffset);
    } else {
        join_info.set_shard_id(common::GlobalInfo::Instance()->network_id());
    }
    
    if (pos == common::kInvalidUint32) {
        auto* req = join_info.mutable_g2_req();
        auto res = prefix_db_->GetBlsVerifyG2(security_->GetAddress(), req);
        if (!res) {
            CreateContribution(req);
        }
    }

    new_tx->set_value(join_info.SerializeAsString());
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign;
    if (security_->Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return;
    }

    new_tx->set_sign(sign);
    // msg_ptr->msg_hash = tx_hash; // TxPoolmanager::HandleElectTx 接收端计算了，这里不必传输
    network::Route::Instance()->Send(msg_ptr);
    ZJC_DEBUG("success send join elect request transaction: %u, join: %u, addr: %s, nonce: %lu, "
        "hash64: %lu, tx hash: %s, pk: %s sign: %s",
        des_sharding_id_, join_info.shard_id(),
        common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
        new_tx->nonce(),
        msg.hash64(),
        common::Encode::HexEncode(tx_hash).c_str(),
        common::Encode::HexEncode(new_tx->pubkey()).c_str(),
        common::Encode::HexEncode(new_tx->sign()).c_str());
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

}  // namespace init

}  // namespace shardora
