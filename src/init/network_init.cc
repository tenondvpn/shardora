#include "init/network_init.h"

#include <functional>

#include "block/block_manager.h"
#include "common/global_info.h"
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
#include "protos/prefix_db.h"
#include "security/ecdsa/ecdsa.h"
#include "timeblock/time_block_manager.h"
#include "timeblock/time_block_utils.h"
#include "transport/multi_thread.h"
#include "transport/tcp_transport.h"
#include "transport/transport_utils.h"
#include "zjcvm/execution.h"

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
        return genesis_check;
    }

    std::string db_path = "./db";
    conf_.Get("zjchain", "db_path", db_path);
    db_ = std::make_shared<db::Db>();
    if (!db_->Init(db_path)) {
        INIT_ERROR("init db failed!");
        return kInitError;
    }

    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    vss_mgr_ = std::make_shared<vss::VssManager>(security_);
    kv_sync_ = std::make_shared<sync::KeyValueSync>();
    kv_sync_->Init(db_);
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
    block_mgr_ = std::make_shared<block::BlockManager>();
    bls_mgr_ = std::make_shared<bls::BlsManager>(security_, db_);
    elect_mgr_ = std::make_shared<elect::ElectManager>(
        vss_mgr_, block_mgr_, security_, bls_mgr_, db_,
        nullptr);
    pools_mgr_ = std::make_shared<pools::TxPoolManager>(security_, db_, kv_sync_);
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
    shard_statistic_ = std::make_shared<pools::ShardStatistic>(elect_mgr_, db_, pools_mgr_);
    block_mgr_->Init(
        account_mgr_,
        db_,
        pools_mgr_,
        shard_statistic_,
        security_->GetAddress(),
        new_db_cb);
    tm_block_mgr_ = std::make_shared<timeblock::TimeBlockManager>();
    bft_mgr_ = std::make_shared<consensus::BftManager>();
    auto bft_init_res = bft_mgr_->Init(
        contract_mgr_,
        gas_prepayment_,
        vss_mgr_,
        account_mgr_,
        block_mgr_,
        elect_mgr_,
        pools_mgr_,
        security_,
        tm_block_mgr_,
        db_,
        nullptr,
        common::GlobalInfo::Instance()->message_handler_thread_count() - 1,
        std::bind(&NetworkInit::AddBlockItemToCache, this,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    if (bft_init_res != consensus::kConsensusSuccess) {
        INIT_ERROR("init bft failed!");
        return kInitError;
    }

    tm_block_mgr_->Init(vss_mgr_);
    if (elect_mgr_->Init() != elect::kElectSuccess) {
        INIT_ERROR("init elect manager failed!");
        return kInitError;
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
    std::cout << "handle init message." << std::endl;
    if (msg_ptr->header.init_proto().has_addr_req()) {
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

    if (msg_ptr->header.init_proto().has_addr_res()) {
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

        if (sharding_id == common::kInvalidUint32) {
            return;
        }

        auto waiting_network_id = sharding_id + network::kConsensusWaitingShardOffset;
        if (elect_mgr_->Join(msg_ptr->thread_idx, waiting_network_id) != elect::kElectSuccess) {
            INIT_ERROR("join waiting pool network[%u] failed!", waiting_network_id);
            return;
        }

        std::cout << "success handle init res message. join waiting shard: " << waiting_network_id << std::endl;
        common::GlobalInfo::Instance()->set_network_id(waiting_network_id);
    }
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
    auto local_node_account_info = prefix_db_->GetAddressInfo(security_->GetAddress());
    if (local_node_account_info == nullptr) {
        return;
    }

    elect::ElectBlockManager elect_block_mgr;
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
            if (id == security_->GetAddress()) {
                ZJC_DEBUG("should join network: %u", sharding_id);
                if (local_node_account_info->sharding_id() != sharding_id) {
                    break;
                }

                common::GlobalInfo::Instance()->set_network_id(sharding_id);
                break;
            }
        }
    }

    if (common::GlobalInfo::Instance()->network_id() != common::kInvalidUint32) {
        return;
    }

    auto waiting_network_id = local_node_account_info->sharding_id() + network::kConsensusWaitingShardOffset;
    if (elect_mgr_->Join(0, waiting_network_id) != elect::kElectSuccess) {
        INIT_ERROR("join waiting pool network[%u] failed!", waiting_network_id);
        return;
    }

    common::GlobalInfo::Instance()->set_network_id(waiting_network_id);
}

void NetworkInit::SendJoinElectTransaction(uint8_t thread_idx) {
    if (common::GlobalInfo::Instance()->network_id() < network::kConsensusShardEndNetworkId) {
        return;
    }

    if (common::GlobalInfo::Instance()->network_id() >= network::kConsensusWaitingShardEndNetworkId) {
        return;
    }

    auto des_net_id = common::GlobalInfo::Instance()->network_id() -
        network::kConsensusWaitingShardOffset;
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
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
    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign;
    if (security_->Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return;
    }

    msg.set_sign(sign);
    msg_ptr->thread_idx = thread_idx;
    network::Route::Instance()->Send(msg_ptr);
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

        http_handler_.Init(&net_handler_, security_, http_server_);
        http_server_.Start();
    }

    return kInitSuccess;
}

void NetworkInit::Destroy() {
    cmd_.Destroy();
    net_handler_.Destroy();
    db_->Destroy();
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

    std::string tcp_spec;
    conf_.Get("zjchain", "tcp_spec", tcp_spec);
    common::Split<> tcp_spec_split(tcp_spec.c_str(), ':', tcp_spec.size());
    std::string tcp_spec_ip = "0.0.0.0";
    std::string tcp_spec_port = "0";
    if (tcp_spec_split.Count() > 1) {
        tcp_spec_ip = tcp_spec_split[0];
        tcp_spec_port = tcp_spec_split[1];
    }

    std::string local_ip;
    parser_arg.Get("a", local_ip);
    if (!local_ip.empty()) {
        if (!conf_.Set("zjchain", "local_ip", local_ip)) {
            INIT_ERROR("set config failed [node][local_ip][%s]", local_ip.c_str());
            return kInitError;
        }

        tcp_spec = local_ip + ":" + tcp_spec_port;
        tcp_spec_ip = local_ip;
    }

    uint16_t local_port = 0;
    if (parser_arg.Get("l", local_port) == common::kParseSuccess) {
        if (!conf_.Set("zjchain", "local_port", local_port)) {
            INIT_ERROR("set config failed [node][local_port][%d]", local_port);
            return kInitError;
        }

        tcp_spec = tcp_spec_ip + ":" + std::to_string(local_port + 1);
    }

    if (!conf_.Set("zjchain", "tcp_spec", tcp_spec)) {
        INIT_ERROR("set config failed [node][id][%s]", tcp_spec.c_str());
        return kInitError;
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
    parser_arg.AddArgType('S', "gen_shard", common::kNoValue);
    parser_arg.AddArgType('1', "root_nodes", common::kMaybeValue);
    parser_arg.AddArgType('2', "shard_nodes", common::kMaybeValue);

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
    if (parser_arg.Has("U")) {
        db_ = std::make_shared<db::Db>();
        if (!db_->Init("./root_db")) {
            INIT_ERROR("init db failed!");
            return kInitError;
        }

        account_mgr_ = std::make_shared<block::AccountManager>();
        block_mgr_ = std::make_shared<block::BlockManager>();
        init::GenesisBlockInit genesis_block(account_mgr_, block_mgr_, db_);
        std::vector<dht::NodePtr> root_genesis_nodes;
        if (parser_arg.Has("1")) {
            std::string value;
            if (parser_arg.Get("1", value) != common::kParseSuccess) {
                return kInitError;
            }

            common::Split<2048> nodes_split(value.c_str(), ',', value.size());
            for (uint32_t i = 0; i < nodes_split.Count(); ++i) {
                common::Split<> node_info(nodes_split[i], ':', nodes_split.SubLen(i));
                if (node_info.Count() != 3) {
                    continue;
                }

                auto node_ptr = std::make_shared<dht::Node>();
                node_ptr->pubkey_str = common::Encode::HexDecode(node_info[0]);
                node_ptr->public_ip = node_info[1];
                node_ptr->id = security_->GetAddress(node_ptr->pubkey_str);
                if (!common::StringUtil::ToUint16(node_info[2], &node_ptr->public_port)) {
                    continue;
                }

                root_genesis_nodes.push_back(node_ptr);
            }
        }

        std::vector<dht::NodePtr> cons_genesis_nodes;
        if (parser_arg.Has("2")) {
            std::string value;
            if (parser_arg.Get("2", value) != common::kParseSuccess) {
                return kInitError;
            }

            common::Split<2048> nodes_split(value.c_str(), ',', value.size());
            for (uint32_t i = 0; i < nodes_split.Count(); ++i) {
                common::Split<> node_info(nodes_split[i], ':', nodes_split.SubLen(i));
                if (node_info.Count() != 3) {
                    continue;
                }

                auto node_ptr = std::make_shared<dht::Node>();
                node_ptr->pubkey_str = common::Encode::HexDecode(node_info[0]);
                node_ptr->public_ip = node_info[1];
                node_ptr->id = security_->GetAddress(node_ptr->pubkey_str);
                if (!common::StringUtil::ToUint16(node_info[2], &node_ptr->public_port)) {
                    continue;
                }

                cons_genesis_nodes.push_back(node_ptr);
            }
        }

        if (genesis_block.CreateGenesisBlocks(
                network::kRootCongressNetworkId,
                root_genesis_nodes,
                cons_genesis_nodes) != 0) {
            return kInitError;
        }

        return kInitSuccess;
    }

    if (parser_arg.Has("S")) {
        ZJC_DEBUG("save shard db: shard_db");
        db_ = std::make_shared<db::Db>();
        if (!db_->Init("./shard_db")) {
            INIT_ERROR("init db failed!");
            return kInitError;
        }

        account_mgr_ = std::make_shared<block::AccountManager>();
        block_mgr_ = std::make_shared<block::BlockManager>();
        init::GenesisBlockInit genesis_block(account_mgr_, block_mgr_, db_);
        std::vector<dht::NodePtr> root_genesis_nodes;
        if (parser_arg.Has("1")) {
            std::string value;
            if (parser_arg.Get("1", value) != common::kParseSuccess) {
                return kInitError;
            }

            common::Split<2048> nodes_split(value.c_str(), ',', value.size());
            for (uint32_t i = 0; i < nodes_split.Count(); ++i) {
                common::Split<> node_info(nodes_split[i], ':', nodes_split.SubLen(i));
                if (node_info.Count() != 3) {
                    continue;
                }

                auto node_ptr = std::make_shared<dht::Node>();
                node_ptr->pubkey_str = common::Encode::HexDecode(node_info[0]);
                node_ptr->public_ip = node_info[1];
                node_ptr->id = security_->GetAddress(node_ptr->pubkey_str);
                if (!common::StringUtil::ToUint16(node_info[2], &node_ptr->public_port)) {
                    continue;
                }

                root_genesis_nodes.push_back(node_ptr);
            }
        }

        std::vector<dht::NodePtr> cons_genesis_nodes;
        if (parser_arg.Has("2")) {
            std::string value;
            if (parser_arg.Get("2", value) != common::kParseSuccess) {
                return kInitError;
            }

            common::Split<2048> nodes_split(value.c_str(), ',', value.size());
            for (uint32_t i = 0; i < nodes_split.Count(); ++i) {
                common::Split<> node_info(nodes_split[i], ':', nodes_split.SubLen(i));
                if (node_info.Count() != 3) {
                    continue;
                }

                auto node_ptr = std::make_shared<dht::Node>();
                node_ptr->pubkey_str = common::Encode::HexDecode(node_info[0]);
                node_ptr->public_ip = node_info[1];
                node_ptr->id = security_->GetAddress(node_ptr->pubkey_str);
                if (!common::StringUtil::ToUint16(node_info[2], &node_ptr->public_port)) {
                    continue;
                }

                cons_genesis_nodes.push_back(node_ptr);
            }
        }

        if (genesis_block.CreateGenesisBlocks(
            network::kConsensusShardBeginNetworkId,
            root_genesis_nodes,
            cons_genesis_nodes) != 0) {
            return kInitError;
        }

        return kInitSuccess;
    }

    return -1;
}

void NetworkInit::AddBlockItemToCache(
        uint8_t thread_idx,
        std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch) {
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
    pools_mgr_->UpdateLatestInfo(
        thread_idx,
        block->network_id(),
        block->pool_index(),
        block->height(),
        block->hash(),
        db_batch);
    // one block must be one consensus pool
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        if (tx_list[i].status() != consensus::kConsensusSuccess) {
            continue;
        }

        switch (tx_list[i].step()) {
        case pools::protobuf::kNormalFrom:
        case pools::protobuf::kRootCreateAddress:
            account_mgr_->NewBlockWithTx(thread_idx, block, tx_list[i], db_batch);
            break;
        case pools::protobuf::kConsensusLocalTos:
        case pools::protobuf::kContractUserCreateCall:
        case pools::protobuf::kContractExcute:
            account_mgr_->NewBlockWithTx(thread_idx, block, tx_list[i], db_batch);
            gas_prepayment_->NewBlockWithTx(thread_idx, block, tx_list[i], db_batch);
            break;
        default:
            break;
        }
    }
}

// pool tx thread, thread safe
void NetworkInit::DbNewBlockCallback(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch) {
    if (block->tx_list_size() == 1) {
        switch (block->tx_list(0).step()) {
        case pools::protobuf::kConsensusRootTimeBlock:
            HandleTimeBlock(thread_idx, block, db_batch);
            break;
        case pools::protobuf::kConsensusRootElectShard:
            HandleElectionBlock(thread_idx, block, db_batch);
            break;
        default:
            break;
        }
    }

    shard_statistic_->OnNewBlock(*block);
    if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId &&
            block->network_id() == network::kRootCongressNetworkId &&
            block->pool_index() == common::kRootChainPoolIndex) {
        pools_mgr_->UpdateLatestInfo(
            thread_idx,
            block->network_id(),
            block->pool_index(),
            block->height(),
            block->hash(),
            db_batch);
    }
}

void NetworkInit::HandleTimeBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch) {
    auto& tx = block->tx_list(0);
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
            break;
        }
    }
}

void NetworkInit::HandleElectionBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch) {
    ZJC_DEBUG("election block coming: %lu", block->height());
    auto elect_block = std::make_shared<elect::protobuf::ElectBlock>();
    for (int32_t i = 0; i < block->tx_list(0).storages_size(); ++i) {
        if (block->tx_list(0).storages(i).key() == protos::kElectNodeAttrElectBlock) {
            if (!elect_block->ParseFromString(block->tx_list(0).storages(i).val_hash())) {
                ZJC_FATAL("parse elect block failed!");
                return;
            }

            break;
        }
    }

    if (!elect_block->has_shard_network_id() ||
            elect_block.shard_network_id() >= network::kConsensusShardEndNetworkId ||
            elect_block.shard_network_id() < network::kRootCongressNetworkId) {
        ZJC_FATAL("parse elect block failed!");
        return;
    }

    auto members = elect_mgr_->OnNewElectBlock(thread_idx, block->height(), elect_block, db_batch);
    if (members == nullptr) {
        ZJC_FATAL("elect manager handle elect block failed!");
        return;
    }

    ZJC_DEBUG("success called election block.");
    auto elect_height = block->height();
    auto sharding_id = elect_block->shard_network_id();
    bft_mgr_->OnNewElectBlock(sharding_id, members);
    block_mgr_->OnNewElectBlock(sharding_id, members);
    vss_mgr_->OnNewElectBlock(sharding_id, elect_height, members);
    bls_mgr_->OnNewElectBlock(sharding_id, elect_height, members, elect_block);
    shard_statistic_->OnNewElectBlock(sharding_id, elect_height);
    network::UniversalManager::Instance()->OnNewElectBlock(sharding_id, elect_height, members);
    if (sharding_id + network::kConsensusWaitingShardOffset ==
            common::GlobalInfo::Instance()->network_id()) {
        join_elect_tick_.CutOff(
            uint64_t(rand()) % (common::kTimeBlockCreatePeriodSeconds / 4 * 3 * 1000000lu),
            std::bind(&NetworkInit::SendJoinElectTransaction, this, std::placeholders::_1));
    }
}

}  // namespace init

}  // namespace zjchain
