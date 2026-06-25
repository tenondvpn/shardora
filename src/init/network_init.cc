#include "init/network_init.h"

#include <fstream>
#include <functional>
#include <memory>

#include <bls/agg_bls.h>
#include <common/encode.h>
#include <common/log.h>
#include <common/utils.h>
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/hotstuff_manager.h>
#include <consensus/hotstuff/pacemaker.h>
#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <consensus/consensus_utils.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_init.hpp>
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
#include "security/ecdsa/sodium_private_key.h"
#include "timeblock/time_block_manager.h"
#include "timeblock/time_block_utils.h"
#include "transport/multi_thread.h"
#include "transport/processor.h"
#include "transport/tcp_transport.h"
#include "transport/transport_utils.h"
#include "shardoravm/execution.h"
#include "common/defer.h"

namespace shardora {

namespace init {

static const std::string kDefaultConfigPath("./conf/shardora.conf");
static const uint32_t kDefaultBufferSize = 1024u * 1024u;
static const std::string kInitJoinWaitingPoolDbKey = "__kInitJoinWaitingPoolDbKey";

NetworkInit::NetworkInit() {}

NetworkInit::~NetworkInit() {
    
}

int NetworkInit::Init(int argc, char** argv) {
    SHARDORA_DEBUG("init 0 0");
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

    int security_init_result = InitSecurity();
    if (security_init_result == kInitWaitingForPrivateKey) {
        INIT_WARN("Private key not found, starting HTTP server to wait for UpdatePrivateKey...");
        
        // Initialize minimal components needed for HTTP server
        std::string db_path = "./db";
        conf_.Get("shardora", "db_path", db_path);
        db_ = std::make_shared<db::Db>();
        if (!db_->Init(db_path)) {
            INIT_ERROR("init db failed!");
            return kInitError;
        }
        
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
        // Initialize HTTP server with private key update callback
        if (InitHttpServerForPrivateKeyWait() != kInitSuccess) {
            INIT_ERROR("InitHttpServerForPrivateKeyWait failed!");
            return kInitError;
        }
        
        INIT_INFO("HTTP server started, waiting for private key update via /update_private_key endpoint...");
        INIT_INFO("Please send POST request to http://%s:%d/update_private_key with private_key parameter",
            common::GlobalInfo::Instance()->config_local_ip().c_str(),
            common::GlobalInfo::Instance()->http_port());
        security_ = nullptr;
        // Wait for private key to be updated
        WaitForPrivateKeyUpdate();
        INIT_INFO("Private key received, continuing initialization...");
        if (!security_) {
            INIT_ERROR("InitSecurity failed after receiving private key!");
            return kInitError;
        }
    } else if (security_init_result != kInitSuccess) {
        INIT_ERROR("InitSecurity failed!");
        return kInitError;
    }

    std::string db_path = "./db";
    conf_.Get("shardora", "db_path", db_path);
    if (!db_) {
        db_ = std::make_shared<db::Db>();
        if (!db_->Init(db_path)) {
            INIT_ERROR("init db failed!");
            return kInitError;
        }
    }

    common::Ip::Instance();
    if (!prefix_db_) {
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    }
    SHARDORA_DEBUG("init 0 1");
    contract_mgr_ = std::make_shared<contract::ContractManager>();
    contract_mgr_->Init(security_);
    common::ParserArgs parser_arg;
    if (ParseParams(argc, argv, parser_arg) != kInitSuccess) {
        INIT_ERROR("parse params failed!");
        return kInitError;
    }

    int genesis_check = GenesisCmd(parser_arg);
    if (genesis_check != -1) {
        common::GlobalInfo::Instance()->set_global_stoped();
        std::cout << "genesis cmd over, exit." << std::endl;
        return genesis_check;
    }

    // Init agg bls
    if (bls::AggBls::Instance()->Init(prefix_db_, security_) != common::kCommonSuccess) {
        return kInitError;
    }    

    // uint32_t ws_server = 0;
    // conf_.Get("shardora", "ws_server", ws_server);
    // if (ws_server > 0) {
    //     if (ws_server_.Init(prefix_db_, security_, &net_handler_) != kInitSuccess) {
    //         SHARDORA_ERROR("init ws server failed!");
    //         return kInitError;
    //     }
    // }

    // random number
    vss_mgr_ = std::make_shared<vss::VssManager>();
    kv_sync_ = std::make_shared<sync::KeyValueSync>();
    SHARDORA_DEBUG("init 0 4");
    InitLocalNetworkId();
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        uint32_t config_net_id = 0;
        if (conf_.Get("shardora", "net_id", config_net_id) &&
                config_net_id >= network::kRootCongressNetworkId && 
                config_net_id <= network::kConsensusShardEndNetworkId) {
            common::GlobalInfo::Instance()->set_network_id(
                config_net_id + network::kConsensusWaitingShardOffset);
        } else {
            INIT_ERROR("init network id failed!");
            return kInitError;
        }
    }

    SHARDORA_DEBUG("id: %s, init sharding id: %u",
        common::Encode::HexEncode(security_->GetAddress()).c_str(),
        common::GlobalInfo::Instance()->network_id());
    SHARDORA_DEBUG("init 0 5");
    if (net_handler_.Init(db_, security_) != transport::kTransportSuccess) {
        return kInitError;
    }

    SHARDORA_DEBUG("init 0 6");
    SHARDORA_DEBUG("init 0 7");
    int transport_res = transport::TcpTransport::Instance()->Init(
        common::GlobalInfo::Instance()->config_local_ip() + ":" +
        std::to_string(common::GlobalInfo::Instance()->config_local_port()),
        128,
        true,
        &net_handler_);
    SHARDORA_DEBUG("init 0 8");
    if (transport_res != transport::kTransportSuccess) {
        INIT_ERROR("int tcp transport failed!");
        return kInitError;
    }

    SHARDORA_DEBUG("init 0 9");
    network::DhtManager::Instance();
    network::Route::Instance()->Init(security_);
    network::Route::Instance()->RegisterMessage(
        common::kInitMessage,
        std::bind(&NetworkInit::HandleMessage, this, std::placeholders::_1));
    transport::Processor::Instance()->RegisterProcessor(
        common::kPoolTimerMessage,
        std::bind(&NetworkInit::HandleMessage, this, std::placeholders::_1));
    account_mgr_ = std::make_shared<block::AccountManager>();
    network::UniversalManager::Instance()->Init(security_, db_, account_mgr_);
    SHARDORA_DEBUG("init 0 10");
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
    hotstuff_mgr_ = std::make_shared<consensus::HotstuffManager>();
    pools_mgr_ = std::make_shared<pools::TxPoolManager>(
        security_, db_, kv_sync_, account_mgr_, hotstuff_mgr_);
    account_mgr_->Init(db_, pools_mgr_);
    shardoravm::Execution::Instance()->Init(db_);
    auto new_db_cb = std::bind(
        &NetworkInit::DbNewBlockCallback,
        this,
        std::placeholders::_1);
    shard_statistic_ = std::make_shared<pools::ShardStatistic>(
        elect_mgr_, db_, security_, pools_mgr_, contract_mgr_);
    tm_block_mgr_ = std::make_shared<timeblock::TimeBlockManager>();
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

    SHARDORA_WARN("init hotstuff_mgr_ success.");
    kv_sync_->Init(
        block_mgr_,
        hotstuff_mgr_,
        pools_mgr_,
        db_,
        std::bind(&consensus::HotstuffManager::VerifySyncedViewBlock,
            hotstuff_mgr_, std::placeholders::_1));
    SHARDORA_WARN("init kv_sync_ success.");
    tm_block_mgr_->Init(vss_mgr_,account_mgr_);
    SHARDORA_WARN("init tm_block_mgr_ success.");
    if (elect_mgr_->Init() != elect::kElectSuccess) {
        INIT_ERROR("init elect manager failed!");
        return kInitError;
    }

    SHARDORA_WARN("init elect_mgr_ success.");
    if (common::GlobalInfo::Instance()->network_id() != common::kInvalidUint32 &&
            common::GlobalInfo::Instance()->network_id() >= network::kConsensusShardEndNetworkId) {
        if (elect_mgr_->Join(
                common::GlobalInfo::Instance()->network_id()) != elect::kElectSuccess) {
            INIT_ERROR("join waiting pool network[%u] failed!",
                common::GlobalInfo::Instance()->network_id());
            return kInitError;
        }
    }

    SHARDORA_WARN("init shard_statistic_ success.");
    block_mgr_->LoadLatestBlocks();
    RegisterFirewallCheck();
    if (shard_statistic_->Init() != pools::kPoolsSuccess) {
        INIT_ERROR("init shard statistic failed!");
    }

    hotstuff_mgr_->Start(); // The above should be placed in the hotstuff instance initialization and receive the genesis block
    SHARDORA_WARN("init hotstuff_mgr_ start success.");
    AddCmds();
    net_handler_.Start();
    transport::TcpTransport::Instance()->Start(false);
    SHARDORA_DEBUG("init 6");
    if (InitHttpServer() != kInitSuccess) {
        INIT_ERROR("InitHttpServer failed!");
        return kInitError;
    }

    if (InitWsServer() != kInitSuccess) {
        INIT_ERROR("InitWsServer failed!");
        // return kInitError;
    }

    SHARDORA_DEBUG("init 7");
    if (InitCommand() != kInitSuccess) {
        INIT_ERROR("InitCommand failed!");
        return kInitError;
    }

    SHARDORA_DEBUG("init 8");
    JoinInitNodes();
    inited_ = true;
    common::GlobalInfo::Instance()->set_main_inited_success();
    SHARDORA_DEBUG("init 9");
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
    // conf_.Get("shardora", "ws_server", ws_server);
    // if (ws_server > 0) {
    //     if (ws_server_.Init(prefix_db_, security_, &net_handler_) != kInitSuccess) {
    //         SHARDORA_ERROR("init ws server failed!");
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

    // Start the tx-subscription WebSocket service.
    std::string ws_ip = "0.0.0.0";
    uint16_t ws_port = 0;
    conf_.Get("shardora", "tx_ws_ip", ws_ip);
    conf_.Get("shardora", "tx_ws_port", ws_port);
    SHARDORA_DEBUG("now init tx ws server: %d", ws_port);
    if (ws_port > 0) {
        if (tx_ws_server_.Init(ws_ip, ws_port) != 0) {
            INIT_ERROR("[TxWsServer] init failed on %s:%u", ws_ip.c_str(), ws_port);
            return kInitError;
        }
        // Forward terminal tx status changes to WS subscribers.
        pools_mgr_->SetTxStatusCallback(
            [this](const std::string& tx_hash_hex, transport::MessageHandleStatus status) {
                tx_ws_server_.OnTxStatusChange(tx_hash_hex, status);
            });
        SHARDORA_DEBUG("[TxWsServer] tx subscription websocket started on %s:%u", ws_ip.c_str(), ws_port);
    }

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
    // SHARDORA_DEBUG("common::kPoolTimerMessage coming.");
    if (msg_ptr->header.type() == common::kPoolTimerMessage) {
        ADD_DEBUG_PROCESS_TIMESTAMP();
        HandleNewBlock();
        ADD_DEBUG_PROCESS_TIMESTAMP();
        bls_mgr_->PoolTimerMessage();
        ADD_DEBUG_PROCESS_TIMESTAMP();
        pools_mgr_->PoolTimerMessage();
        ADD_DEBUG_PROCESS_TIMESTAMP();
    }
}


bool NetworkInit::InitLocalNetworkIdWithLatestElectBlock() {
    for (uint32_t i = network::kRootCongressNetworkId;
            i < network::kConsensusShardEndNetworkId; ++i) {
        elect::protobuf::ElectBlock elect_block;
        if (!prefix_db_->GetHavePrevlatestElectBlock(i, &elect_block)) {
            SHARDORA_ERROR("get elect latest block failed: %u", i);
            break;
        }

        if (!elect_block.has_prev_members()) {
            return false;
        }

        for (int32_t midx = 0; midx < elect_block.in_size(); ++midx) {
            auto& member = elect_block.in(midx);
            if (security_->GetAddressWithPublicKey(member.pubkey()) == security_->GetAddress()) {
                common::GlobalInfo::Instance()->set_network_id(i);
                return true;
            }
        }
    }

    return false;
}

void NetworkInit::InitLocalNetworkId() {
    uint32_t got_sharding_id = common::kInvalidUint32;
    if (!prefix_db_->GetJoinShard(&got_sharding_id, &des_sharding_id_)) {
        auto local_node_account_info = prefix_db_->GetAddressInfo(security_->GetAddress());
        if (local_node_account_info == nullptr) {
            if (!InitLocalNetworkIdWithLatestElectBlock()) {
                SHARDORA_DEBUG("failed get local account info id: %s",
                    common::Encode::HexEncode(security_->GetAddress()).c_str());
                return;
            }

            got_sharding_id = common::GlobalInfo::Instance()->network_id();
            des_sharding_id_ = got_sharding_id;
        } else {
            got_sharding_id = local_node_account_info->sharding_id();
            des_sharding_id_ = got_sharding_id;
        }

        prefix_db_->SaveJoinShard(got_sharding_id, des_sharding_id_);
        SHARDORA_DEBUG("success save local sharding %u, %u", got_sharding_id, des_sharding_id_);
    }

    for (uint32_t sharding_id = network::kRootCongressNetworkId;
            sharding_id < network::kConsensusShardEndNetworkId; ++sharding_id) {
        elect::protobuf::ElectBlock elect_block;
        if (!prefix_db_->GetLatestElectBlock(sharding_id, &elect_block)) {
            SHARDORA_WARN("get latest elect block failed: %u", sharding_id);
            break;
        }

        auto& in = elect_block.in();
        for (int32_t member_idx = 0; member_idx < in.size(); ++member_idx) {
            auto id = security_->GetAddressWithPublicKey(in[member_idx].pubkey());
            SHARDORA_DEBUG("network: %d get member id: %s, local id: %s",
                sharding_id, common::Encode::HexEncode(id).c_str(),
                common::Encode::HexEncode(security_->GetAddress()).c_str()); // If the pubkey of this node is the same as the one recorded in the elect block, it will be assigned to the corresponding sharding
            if (id == security_->GetAddress()) {
                SHARDORA_DEBUG("should join network: %u", sharding_id);
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
    SHARDORA_DEBUG("should join waiting network: %u", waiting_network_id);
}

int NetworkInit::InitSecurity() {
    std::string prikey;
    if (!conf_.Get("shardora", "prikey", prikey) || prikey.empty()) {
        INIT_WARN("Private key is empty or not found in config, waiting for UpdatePrivateKey...");
        // Return a special status to indicate we need to wait for private key update
        return kInitWaitingForPrivateKey;
    }
    SHARDORA_DEBUG("prikey1: %s", prikey.c_str());
    SHARDORA_DEBUG("prikey2: %s", common::Encode::HexEncode(common::Encode::HexDecode(prikey)).c_str());

    security_ = std::make_shared<security::Ecdsa>();
    auto bytes_prikey = common::Encode::HexDecode(prikey);
    if (bytes_prikey.size() == security::kPrivateKeySize) {
        if (security_->SetPrivateKey(bytes_prikey) != security::kSecuritySuccess) { 
            INIT_ERROR("init security failed!");
            return kInitError;
        }
    } else {
        if (security::KeyManager::Instance().Initialize(bytes_prikey) != security::kSecuritySuccess) {
            INIT_ERROR("init security failed!");
            return kInitError;
        }

        if (security_->SetPrivateKey(
                (const char*)security::KeyManager::Instance().GetProtectedKey(), 
                security::KeyManager::Instance().GetKeyLength()) != security::kSecuritySuccess) { 
            INIT_ERROR("init security failed!");
            return kInitError;
        }
    }
    

    return kInitSuccess;
}

int NetworkInit::UpdatePrivateKey(const std::string& new_private_key) {
    SHARDORA_DEBUG("Updating private key...");
    if (http_private_key_inited_) {
        SHARDORA_ERROR("Private key already inited!");
        return kInitError;
    }

    if (new_private_key.empty()) {
        SHARDORA_ERROR("New private key is empty!");
        return kInitError;
    }
    
    // Create new security object to verify private key
    auto new_security = std::make_shared<security::Ecdsa>();
    
    // Determine if decryption is needed based on private key length
    if (new_private_key.size() == security::kPrivateKeySize) {
        // Raw private key (32 bytes)
        if (new_security->SetPrivateKey(new_private_key) != security::kSecuritySuccess) { 
            SHARDORA_ERROR("Failed to set new private key (raw format)!");
            return kInitError;
        }
    } else {
        // Encrypted private key, needs decryption first
        if (security::KeyManager::Instance().Initialize(new_private_key) != security::kSecuritySuccess) {
            SHARDORA_ERROR("Failed to initialize KeyManager with new private key!");
            return kInitError;
        }

        if (new_security->SetPrivateKey(
                (const char*)security::KeyManager::Instance().GetProtectedKey(), 
                security::KeyManager::Instance().GetKeyLength()) != security::kSecuritySuccess) { 
            SHARDORA_ERROR("Failed to set new private key (encrypted format)!");
            return kInitError;
        }
    }
    
    // Get new address
    std::string new_address = new_security->GetAddress();
    
    // Check if this is initial private key setup or update
    bool is_initial_setup = (security_ == nullptr || private_key_received_ == false);
    
    if (!is_initial_setup) {
        std::string old_address = security_->GetAddress();
        SHARDORA_DEBUG("Private key update: old address: %s, new address: %s",
            common::Encode::HexEncode(old_address).c_str(),
            common::Encode::HexEncode(new_address).c_str());
    } else {
        SHARDORA_DEBUG("Initial private key setup: new address: %s",
            common::Encode::HexEncode(new_address).c_str());
    }
    
    // // Update private key in configuration file (for persistence)
    // std::string prikey_hex = common::Encode::HexEncode(new_private_key);
    // if (conf_.Set("shardora", "prikey", prikey_hex)) {
    //     SHARDORA_DEBUG("Configuration updated with new private key");
    // } else {
    //     SHARDORA_ERROR("Failed to update configuration with new private key");
    //     return kInitError;
    // }
    
    security_ = new_security;
    if (is_initial_setup) {
        // For initial setup, just notify the waiting thread
        // The security_ object will be properly initialized by InitSecurity() after wait returns
        SHARDORA_DEBUG("Private key received for initial setup");
        
        // Notify waiting thread that private key has been received
        {
            std::lock_guard<std::mutex> lock(private_key_wait_mutex_);
            private_key_received_ = true;
        }
        http_private_key_inited_ = true;
        private_key_wait_cv_.notify_one();
    } 

    
    // NOTE: We do NOT call Init() on Route, UniversalManager, or Bootstrap
    // because they create new threads which would terminate the old running threads.
    // These components will automatically use the updated security_ pointer
    // since they hold shared_ptr references to it.
    
    SHARDORA_DEBUG("Security object updated with new private key");
    
    SHARDORA_DEBUG("Private key updated successfully! New address: %s",
        common::Encode::HexEncode(new_address).c_str());
    
    return kInitSuccess;
}

int NetworkInit::InitHttpServerForPrivateKeyWait() {
    std::string http_ip = "0.0.0.0";
    uint16_t http_port = 0;
    conf_.Get("shardora", "http_ip", http_ip);
    if (!conf_.Get("shardora", "http_port", http_port) || http_port == 0) {
        INIT_ERROR("HTTP port not configured, cannot wait for private key update!");
        return kInitError;
    }
    
    // Create minimal account manager for HTTP handler
    account_mgr_ = std::make_shared<block::AccountManager>();
    
    // Create a temporary empty security object just for HTTP handler initialization
    // This will be replaced with the real one after private key is received
    auto temp_security = std::make_shared<security::Ecdsa>();
    
    // Set private key update callback
    http_handler_.SetPrivateKeyUpdateCallback(
        [this](const std::string& new_private_key) -> int {
            return this->UpdatePrivateKey(new_private_key);
        });
    
    // Initialize HTTP handler with minimal components
    http_handler_.Init(
        account_mgr_, 
        nullptr,  // net_handler not initialized yet
        temp_security,  // temporary security object
        prefix_db_, 
        nullptr,  // contract_mgr not initialized yet
        http_ip, 
        http_port);
    
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    SHARDORA_DEBUG("HTTP server initialized for private key waiting on %s:%u", http_ip.c_str(), http_port);
    
    return kInitSuccess;
}

void NetworkInit::WaitForPrivateKeyUpdate() {
    INIT_INFO("Waiting for private key update...");
    std::unique_lock<std::mutex> lock(private_key_wait_mutex_);
    private_key_wait_cv_.wait(lock, [this] { return private_key_received_.load(); });
    INIT_INFO("Private key received, resuming initialization...");
}

static std::condition_variable wait_con_;
static std::mutex wait_mutex_;

int NetworkInit::InitHttpServer() {
    std::string http_ip = "0.0.0.0";
    uint16_t http_port = 0;
    conf_.Get("shardora", "http_ip", http_ip);
    if (conf_.Get("shardora", "http_port", http_port) && http_port != 0) {
        if (private_key_received_) {
            http_handler_.set_net_handler(&net_handler_);
            http_handler_.set_contract_mgr(contract_mgr_);
            http_handler_.set_elect_mgr(elect_mgr_);
            http_handler_.set_hotstuff_mgr(hotstuff_mgr_);
        } else {
            http_handler_.SetPrivateKeyUpdateCallback(
                [this](const std::string& new_private_key) -> int {
                    return this->UpdatePrivateKey(new_private_key);
                });
            http_handler_.Init(
                account_mgr_, 
                &net_handler_, 
                security_, 
                prefix_db_, 
                contract_mgr_, 
                http_ip, 
                http_port);
            http_handler_.set_elect_mgr(elect_mgr_);
            http_handler_.set_hotstuff_mgr(hotstuff_mgr_);
            private_key_received_ = true;
        }
       
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        // Note: HTTP client check removed as we migrated from httplib to uWebSockets
        // The server will be ready after the sleep delay
        SHARDORA_DEBUG("http init wait response coming.");
        {
            std::unique_lock<std::mutex> lock(wait_mutex_);
            wait_con_.notify_one();
        }

        SHARDORA_DEBUG("http init waiting response coming.");
        {
            std::unique_lock<std::mutex> lock(wait_mutex_);
            wait_con_.wait_for(lock, std::chrono::milliseconds(10000));
        }
        SHARDORA_DEBUG("http init waiting response coming success.");
    }

    return kInitSuccess;
}

void NetworkInit::Destroy() {
    if (destroy_) {
        return;
    }

    destroy_ = true;
    cmd_.Destroy();
    net_handler_.Destroy();
//     if (db_ != nullptr) {
//         db_->Destroy();
//     }
}

int NetworkInit::InitCommand() {
    bool first_node = false;
    if (!conf_.Get("shardora", "first_node", first_node)) {
        INIT_ERROR("get conf shardora first_node failed!");
        return kInitError;
    }

    bool show_cmd = false;
    if (!conf_.Get("shardora", "show_cmd", show_cmd)) {
        INIT_ERROR("get conf shardora show_cmd failed!");
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

    if (parser_arg.Has("K")) {
        std::string hex_input;
        parser_arg.Get("K", hex_input);
        if (hex_input.empty()) {
            std::cout << "Error: Input hex string is empty." << std::endl;
            exit(1);
        }

        std::string raw_data = common::Encode::HexDecode(hex_input);
        if (raw_data.empty() && !hex_input.empty()) {
            std::cout << "Error: Invalid hex string input." << std::endl;
            exit(1);
        }

        auto security = std::make_shared<security::Ecdsa>();
        if (raw_data.size() == security::kPrivateKeySize) {
            if (security->SetPrivateKey(raw_data) != security::kSecuritySuccess) { 
                INIT_ERROR("init security failed!");
                return kInitError;
            }
        } else {
            if (security::KeyManager::Instance().Initialize(raw_data) != security::kSecuritySuccess) {
                INIT_ERROR("init security failed!");
                return kInitError;
            }

            if (security->SetPrivateKey(
                    (const char*)security::KeyManager::Instance().GetProtectedKey(), 
                    security::KeyManager::Instance().GetKeyLength()) != security::kSecuritySuccess) { 
                INIT_ERROR("init security failed!");
                return kInitError;
            }
        }
        
        std::string encrypted_binary = shardora::security::KeyManager::SealKey(raw_data);
        if (!encrypted_binary.empty()) {
            std::cout << encrypted_binary << ":" << common::Encode::HexEncode(security->GetAddress()) << std::endl;
            exit(0);
        } else {
            std::cout << "Error: Encryption failed." << std::endl;
            exit(1);
        }
    }

    if (parser_arg.Has("A")) {
        if (!parser_arg.Has("D")) {
            exit(1);
        }

        std::string src_prikey_file;
        parser_arg.Get("A", src_prikey_file);
        std::ifstream infile(src_prikey_file);
        if (!infile.is_open()) {
            std::cout << "Error: Cannot open input file: " << src_prikey_file << std::endl;
            exit(1);
        }

        std::string des_prikey_file;
        parser_arg.Get("D", des_prikey_file);
        std::ofstream outfile(des_prikey_file);
        if (!outfile.is_open()) {
            std::cout << "Error: Cannot open output file: " << des_prikey_file << std::endl;
            exit(1);
        }

        std::string line;
        uint32_t count = 0;
        while (std::getline(infile, line)) {
            // 1. Remove possible \r at end of line (handle Windows/DOS format files)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // 2. Strict empty line check: skip lines with no content at all
            if (line.empty()) continue;

            size_t tab_pos = line.find('\t');
            std::string first_column;
            std::string remaining_part = "";

            if (tab_pos != std::string::npos) {
                first_column = line.substr(0, tab_pos);
                remaining_part = line.substr(tab_pos);
            } else {
                first_column = line;
            }

            if (first_column.empty()) {
                outfile << line << "\n";
                continue;
            }

            std::string encrypted = shardora::security::KeyManager::SealKey(common::Encode::HexDecode(first_column));
            if (!encrypted.empty()) {
                outfile << encrypted << remaining_part << "\n";
                count++;
            } else {
                outfile << line << "\n";
            }
        }

        std::cout << "Successfully processed " << count << " lines to " << des_prikey_file << std::endl;
        infile.close();
        outfile.close();
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
        if (!conf_.Set("shardora", "country", country)) {
            INIT_ERROR("set config failed [node][country][%s]", country.c_str());
            return kInitError;
        }
    }

    std::string local_ip;
    parser_arg.Get("a", local_ip);
    if (!local_ip.empty()) {
        if (!conf_.Set("shardora", "local_ip", local_ip)) {
            INIT_ERROR("set config failed [node][local_ip][%s]", local_ip.c_str());
            return kInitError;
        }
    }

    uint16_t local_port = 0;
    if (parser_arg.Get("l", local_port) == common::kParseSuccess) {
        if (!conf_.Set("shardora", "local_port", local_port)) {
            INIT_ERROR("set config failed [node][local_port][%d]", local_port);
            return kInitError;
        }
    }

    std::string prikey;
    parser_arg.Get("k", prikey);
    if (!prikey.empty()) {
        if (!conf_.Set("shardora", "prikey", prikey)) {
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

        if (!conf_.Set("shardora", "first_node", first_node)) {
            INIT_ERROR("set config failed [node][first_node][%d]", first_node);
            return kInitError;
        }
    }

    std::string network_ids;
    if (parser_arg.Get("n", network_ids) == common::kParseSuccess) {
        if (!conf_.Set("shardora", "net_ids", network_ids)) {
            INIT_ERROR("set config failed [node][net_id][%s]", network_ids.c_str());
            return kInitError;
        }
    }

    std::string peer;
    parser_arg.Get("p", peer);
    if (!peer.empty()) {
        if (!conf_.Set("shardora", "bootstrap", peer)) {
            INIT_ERROR("set config failed [node][bootstrap][%s]", peer.c_str());
            return kInitError;
        }
    }

    std::string id;
    parser_arg.Get("i", id);
    if (!id.empty()) {
        if (!conf_.Set("shardora", "id", id)) {
            INIT_ERROR("set config failed [node][id][%s]", peer.c_str());
            return kInitError;
        }
    }

    int show_cmd = 1;
    if (parser_arg.Get("g", show_cmd) == common::kParseSuccess) {
        if (!conf_.Set("shardora", "show_cmd", show_cmd == 1)) {
            INIT_ERROR("set config failed [node][show_cmd][%d]", show_cmd);
            return kInitError;
        }
    }

    int vpn_vip_level = 0;
    if (parser_arg.Get("V", vpn_vip_level) == common::kParseSuccess) {
        if (!conf_.Set("shardora", "vpn_vip_level", vpn_vip_level)) {
            INIT_ERROR("set config failed [node][vpn_vip_level][%d]", vpn_vip_level);
            return kInitError;
        }
    }

    std::string log_path;
    if (parser_arg.Get("L", log_path) != common::kParseSuccess) {
        log_path = "log/shardora.log";
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
    parser_arg.AddArgType('E', "end_net", common::kMaybeValue);
    parser_arg.AddArgType('N', "node_count", common::kMaybeValue);
    parser_arg.AddArgType('C', "cross_latest", common::kNoValue);
    parser_arg.AddArgType('A', "src_transform_prikey", common::kMaybeValue);
    parser_arg.AddArgType('D', "des_transform_prikey", common::kMaybeValue);
    parser_arg.AddArgType('K', "encrypt_prikey", common::kMaybeValue);
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
    auto file_name = common::GlobalInfo::Instance()->RootPathFile(
        std::string("init_accounts") + std::to_string(net_id));
    if (common::isFileExist(file_name)) {
        return;
    }

    auto fd = fopen(file_name.c_str(), "w");
    uint32_t address_count_now = 0;
    for (uint32_t j = 0; j < 64; j++) {
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
    }

    fclose(fd);
}

int NetworkInit::GenesisCmd(common::ParserArgs& parser_arg) {
    int consensus_shard_node_count = 4;
    if (parser_arg.Has("N")) {
        if (parser_arg.Get("N", consensus_shard_node_count) != common::kParseSuccess) {
            return -1;
        }
    }
    
    uint32_t end_shard_id = 4;
    parser_arg.Get("E", end_shard_id);
    std::set<uint32_t> valid_net_ids_set;
    for (uint32_t i = network::kRootCongressNetworkId; i < end_shard_id; i++) {
        valid_net_ids_set.insert(i);
    }

    SHARDORA_DEBUG("now consensus_shard_node_count: %u", consensus_shard_node_count);
    if (parser_arg.Has("U")) {
        std::cout << "now genisis root" << std::endl;
        std::string valid_arg_i_value;
        for (uint32_t net_id = network::kConsensusShardBeginNetworkId; 
                net_id < end_shard_id; ++net_id) {
            CreateInitAddress(net_id);
        }
        
        auto db = std::make_shared<db::Db>();
        if (!db->Init("./shard_db_2")) {
            INIT_ERROR("init db failed!");
            return kInitError;
        }

        account_mgr_ = std::make_shared<block::AccountManager>();
        // account_mgr_->Init(db, pools_mgr_);
        block_mgr_ = std::make_shared<block::BlockManager>(net_handler_, nullptr);
        init::GenesisBlockInit genesis_block(account_mgr_, block_mgr_, db);
        std::vector<GenisisNodeInfoPtr> root_genesis_nodes;
        std::map<uint32_t, std::vector<GenisisNodeInfoPtr>> cons_genesis_nodes_of_shards;
        GetNetworkNodesFromConf(
            end_shard_id,
            consensus_shard_node_count, 
            root_genesis_nodes, 
            cons_genesis_nodes_of_shards, 
            db);
        if (genesis_block.CreateGenesisBlocks(
                network::kRootCongressNetworkId,
                root_genesis_nodes,
                cons_genesis_nodes_of_shards) != 0) {
            return kInitError;
        }

        std::cout << "root genisis success!" << std::endl;
        return kInitSuccess;
    }

    if (parser_arg.Has("S")) {
        std::cout << "now genisis shards" << std::endl;
        for (uint32_t i = 3; i < end_shard_id; i++) {
            std::cout << "now genisis shard" << i << std::endl;
            std::string net_id_str = std::to_string(i);
            auto db = std::make_shared<db::Db>();
            if (!db->Init("./shard_db_" + net_id_str)) {
                INIT_ERROR("init db failed!");
                return kInitError;
            }

            auto account_mgr = std::make_shared<block::AccountManager>();
            auto block_mgr = std::make_shared<block::BlockManager>(net_handler_, nullptr);
            init::GenesisBlockInit genesis_block(account_mgr, block_mgr, db);
            std::vector<GenisisNodeInfoPtr> root_genesis_nodes;
            std::map<uint32_t, std::vector<GenisisNodeInfoPtr>> cons_genesis_nodes_of_shards;
            GetNetworkNodesFromConf(
                end_shard_id,
                consensus_shard_node_count, 
                root_genesis_nodes, 
                cons_genesis_nodes_of_shards, 
                db);
            if (genesis_block.CreateGenesisBlocks(
                    i,
                    root_genesis_nodes,
                    cons_genesis_nodes_of_shards) != 0) {
                return kInitError;
            }

            SaveLatestBlock(db, i);
            std::cout << "shard" << i << " genisis success!" << std::endl;
        }
            
        return kInitSuccess;
    }

    if (parser_arg.Has("C")) {
        SaveCrossBlockToEachShard();
        return kInitSuccess;
    }

    return -1;
}

void NetworkInit::SaveLatestBlock(std::shared_ptr<db::Db> db, uint32_t sharding_id) {
    FILE* fd = fopen("./latest_blocks", "a+");
    if (fd == nullptr) {
        SHARDORA_FATAL("open latest_blocks failed!");
        return;
    }

    defer(fclose(fd));
    auto prefix_db = std::make_shared<protos::PrefixDb>(db);
    for (uint64_t i = 0; i < 128llu; i++) {
        SHARDORA_DEBUG("save block height: %u_%u_%llu", sharding_id, common::kGlobalPoolIndex, i);
        view_block::protobuf::ViewBlockItem view_block_item;
        if (!prefix_db->GetBlockWithHeight(sharding_id, common::kGlobalPoolIndex, i, &view_block_item)) {
            return;
        }

        auto data = common::Encode::HexEncode(view_block_item.SerializeAsString());
        fwrite(data.c_str(), 1, data.size(), fd);
        fwrite("\n", 1, 1, fd);
        SHARDORA_DEBUG("success save block height: %u_%u_%lu", 
            sharding_id, common::kGlobalPoolIndex, i);
    }

    fflush(fd);
}
    
void NetworkInit::SaveCrossBlockToEachShard() {
    FILE* fd = fopen("./latest_blocks", "r");
    if (fd == nullptr) {
        SHARDORA_FATAL("open latest_blocks failed!");
        return;
    }

    defer(fclose(fd));
    std::vector<view_block::protobuf::ViewBlockItem> blocks;
    char line_buf[1024 * 1024];
    while (fgets(line_buf, sizeof(line_buf), fd) != nullptr) {
        std::string line(line_buf);
        common::StringUtil::Trim(line);
        view_block::protobuf::ViewBlockItem block_item;
        block_item.ParseFromString(common::Encode::HexDecode(line));
        blocks.push_back(block_item);
    }

    for (uint32_t net_i = network::kRootCongressNetworkId;
            net_i < network::kConsensusShardEndNetworkId; net_i++) {
        auto db = std::make_shared<db::Db>();
        if (net_i == network::kRootCongressNetworkId) {
            if (!db->Init(std::string("./root_db"))) {
                if (!db->Init(std::string("./shard_db_") + std::to_string(net_i))) {
                    INIT_WARN("init db failed!");
                    return;
                }
            }
        } else {
            if (!db->Init(std::string("./shard_db_") + std::to_string(net_i))) {
                INIT_WARN("init db failed!");
                return;
            }
        }

        auto prefix_db = std::make_shared<protos::PrefixDb>(db);
        db::DbWriteBatch batch;
        for (auto iter = blocks.begin(); iter != blocks.end(); ++iter) {
            prefix_db->SaveBlock(*iter, batch);
            auto& view_block = *iter;
            pools::protobuf::PoolLatestInfo pool_info;
            pool_info.set_height(view_block.block_info().height());
            pool_info.set_hash(view_block.qc().view_block_hash());
            pool_info.set_timestamp(view_block.block_info().timestamp());
            pool_info.set_view(view_block.qc().view());
            prefix_db->SaveLatestPoolInfo(
                view_block.qc().network_id(), view_block.qc().pool_index(), pool_info, batch);
        }

        auto st = db->Put(batch);
        if (!st.ok()) {
            SHARDORA_FATAL("write db failed!");
        }
    }
}

void NetworkInit::GetNetworkNodesFromConf(
        uint32_t end_shard_id,
        uint32_t cons_shard_node_count,
        std::vector<GenisisNodeInfoPtr>& root_genesis_nodes,
        std::map<uint32_t, std::vector<GenisisNodeInfoPtr>>& cons_genesis_nodes_of_shards,
        const std::shared_ptr<db::Db>& db) {
    auto prefix_db = std::make_shared<protos::PrefixDb>(db);
    auto get_sks_func = [](FILE *fd, std::vector<std::string>& sks, int32_t count, bool reuse) {
        if (reuse) {
            char data[1024*1024] = {0};
            fread(data, 1, sizeof(data), fd);
            auto lines = common::Split<1024>(data, '\n');
            for (uint32_t i = 0; i < lines.Count(); ++i) {
                auto items = common::Split<>(lines[i], '\t');
                if (items.Count() != 2) {
                    break;
                }

                sks.push_back(common::Encode::HexDecode(items[0]));
                SHARDORA_DEBUG("reuse private key: %s", items[0]);
            }
        } else {
            for (int32_t i = 0; i < count; i++) {
                sks.push_back(common::Random::RandomString(32));
                std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
                secptr->SetPrivateKey(sks[i]);
                auto data = common::Encode::HexEncode(sks[i]) + "\t" + common::Encode::HexEncode(secptr->GetPublicKey()) + "\n";
                fwrite(data.c_str(), 1, data.size(), fd);
                SHARDORA_DEBUG("random private key: %s", common::Encode::HexEncode(sks[i]).c_str());
            }
        }
    };
        
    uint32_t n = cons_shard_node_count;
    // bool reuse_root = common::isFileExist(common::GlobalInfo::Instance()->RootPathFile("shards2"));
    // auto rfd = fopen(common::GlobalInfo::Instance()->RootPathFile("shards2").c_str(), (reuse_root ? "r" : "w"));
    // //assert(rfd != nullptr);
    // std::vector<std::string> root_sks;
    // get_sks_func(rfd, root_sks, n, reuse_root);
    // for (uint32_t i = 0; i < root_sks.size(); i++) {
    //     std::string& sk = root_sks[i];
    //     std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
    //     secptr->SetPrivateKey(sk);
    //     auto node_ptr = std::make_shared<GenisisNodeInfo>();
    //     node_ptr->prikey = sk;
    //     node_ptr->pubkey = secptr->GetPublicKey();
    //     node_ptr->id = secptr->GetAddressWithPublicKey(node_ptr->pubkey);
    //     node_ptr->nonce = 0;
    //     root_genesis_nodes.push_back(node_ptr);
    //     SHARDORA_DEBUG("root private key: %s, id: %s", 
    //         common::Encode::HexEncode(sk).c_str(), 
    //         common::Encode::HexEncode(node_ptr->id).c_str());
    // }
    // fclose(rfd);
    uint32_t t = common::GetSignerCount(n);
    for (uint32_t net_i = network::kRootCongressNetworkId; net_i < end_shard_id; net_i++) {
        auto filename = common::GlobalInfo::Instance()->RootPathFile(
            std::string("shards") + std::to_string(net_i));
        bool reuse_shard = common::isFileExist(filename);
        std::cout << filename << ", reuse: " << reuse_shard << std::endl;
        auto sfd = fopen(filename.c_str(), (reuse_shard ? "r" : "w"));
        if (sfd == nullptr) {
            SHARDORA_FATAL("open file failed: %s", filename.c_str());
            break;
        }

        std::vector<std::string> shard_sks;
        get_sks_func(sfd, shard_sks, n, reuse_shard);
        std::vector<GenisisNodeInfoPtr> cons_genesis_nodes;
        for (uint32_t i = 0; i < n; i++) {
            SHARDORA_DEBUG("use private key: %d, %s", i, common::Encode::HexEncode(shard_sks[i]).c_str());
            std::string& sk = shard_sks[i];
            std::shared_ptr<security::Security> secptr = std::make_shared<security::Ecdsa>();
            secptr->SetPrivateKey(sk);
            auto node_ptr = std::make_shared<GenisisNodeInfo>();
            node_ptr->prikey = sk;
            node_ptr->pubkey = secptr->GetPublicKey();
            node_ptr->id = secptr->GetAddressWithPublicKey(node_ptr->pubkey);
            node_ptr->nonce = 0;
            cons_genesis_nodes.push_back(node_ptr);   
            SHARDORA_DEBUG("shard: %d private key: %s, id: %s", 
                net_i,
                common::Encode::HexEncode(sk).c_str(), 
                common::Encode::HexEncode(node_ptr->id).c_str());     
        }
        
        if (net_i == network::kRootCongressNetworkId) {
            root_genesis_nodes = cons_genesis_nodes;
        } else {
            cons_genesis_nodes_of_shards[net_i] = cons_genesis_nodes;
        }
        
        fclose(sfd);
    }
    // }
}

void NetworkInit::InitAggBlsForGenesis(
        const std::string& node_id, 
        std::shared_ptr<security::Security>& secptr, 
        std::shared_ptr<protos::PrefixDb>& prefix_db) {
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
    if (network::IsSameToLocalShard(view_block->qc().network_id())) {
        // thread thafe
        pools_mgr_->UpdateLatestInfo(view_block, db_batch);
    } else {
        // thread thafe
        pools_mgr_->UpdateCrossLatestInfo(view_block, db_batch);
    }

    auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
    new_blocks_queue_[thread_idx].push(view_block);
    SHARDORA_DEBUG("cache new block coming sharding id: %u_%d_%lu, tx size: %u, hash: %s, size: %u",
        view_block->qc().network_id(),
        view_block->qc().pool_index(),
        block->height(),
        block->tx_list_size(),
        common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str(),
        new_blocks_queue_[thread_idx].size());

}

void NetworkInit::HandleNewBlock() {
    for (uint32_t i = 0; i < common::kMaxThreadCount; i++) {
        while (new_blocks_queue_[i].size() > 0) {
            std::shared_ptr<view_block::protobuf::ViewBlockItem> view_block;
            new_blocks_queue_[i].pop(&view_block);
            auto* block = &view_block->block_info();
            SHARDORA_DEBUG("handle new block coming sharding id: %u_%d_%lu, tx size: %u, hash: %s",
                view_block->qc().network_id(),
                view_block->qc().pool_index(),
                block->height(),
                block->tx_list_size(),
                common::Encode::HexEncode(view_block->qc().view_block_hash()).c_str());
            if (view_block) {
                DbNewBlockCallback(view_block);
            }
        }
    }
}

bool NetworkInit::DbNewBlockCallback(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block) {
    auto* block = &view_block->block_info();
    for (int32_t i = 0; i < block->tx_list_size(); ++i) {
        switch (block->tx_list(i).step()) {
        case pools::protobuf::kConsensusRootTimeBlock:
            HandleTimeBlock(view_block, block->tx_list(i));
            break;
        case pools::protobuf::kConsensusRootElectShard:
            HandleElectionBlock(view_block, block->tx_list(i));
            break;
        default:
            break;
        }
    }

    // Push transaction details to WebSocket clients subscribed to the matching txhash.
    tx_ws_server_.OnNewBlock(view_block);

    return true;
}

void NetworkInit::HandleTimeBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
        const block::protobuf::BlockTx& tx) {
    SHARDORA_DEBUG("time block coming %u_%u_%lu, %u_%u_%lu, tm: %lu, vss: %lu",
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
        hotstuff_mgr_->OnTimeBlock(block.timer_block().timestamp(), block.height(), vss_random, tx.nonce());
        bls_mgr_->OnTimeBlock(block.timer_block().timestamp(), block.height(), vss_random);
        tm_block_mgr_->OnTimeBlock(block.timer_block().timestamp(), block.height(), vss_random);
        vss_mgr_->OnTimeBlock(view_block);
        SHARDORA_DEBUG("new time block called height: %lu, tm: %lu", block.height(), vss_random);
    }
}

void NetworkInit::HandleElectionBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block,
        const block::protobuf::BlockTx& block_tx) {
    auto* block = &view_block->block_info();
    SHARDORA_DEBUG("new elect block coming, net: %u, pool: %u, height: %lu, block info: %s",
        view_block->qc().network_id(), view_block->qc().pool_index(), block->height(),
        ProtobufToJson(view_block->block_info()).c_str());
    auto elect_block = std::make_shared<elect::protobuf::ElectBlock>();
    auto prev_elect_block = std::make_shared<elect::protobuf::ElectBlock>();
    if (block->has_elect_block()) {
        *elect_block = block->elect_block();
        if (network::IsSameToLocalShard(elect_block->shard_network_id())) {
            if (elect_block->has_prev_members() &&
                    elect_block->prev_members().prev_elect_height() > latest_valid_elect_height_) {
                latest_valid_elect_height_ = elect_block->prev_members().prev_elect_height();
            }
        }
    }

    if (block->has_prev_elect_block()) {
        *prev_elect_block = block->prev_elect_block();
    }

    if (!elect_block->has_shard_network_id() ||
            elect_block->shard_network_id() >= network::kConsensusShardEndNetworkId ||
            elect_block->shard_network_id() < network::kRootCongressNetworkId) {
        SHARDORA_FATAL("parse elect block failed!");
        return;
    }

    auto members = elect_mgr_->OnNewElectBlock(
        block->height(),
        elect_block,
        prev_elect_block);
    if (members == nullptr) {
        SHARDORA_ERROR("elect manager handle elect block failed!");
        return;
    }

    auto sharding_id = elect_block->shard_network_id();
    common::GlobalInfo::Instance()->set_now_valid_end_shard(sharding_id);
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
    hotstuff_mgr_->OnNewElectBlock(sharding_id, elect_height, members, common_pk, sec_key);
    bls_mgr_->OnNewElectBlock(
        sharding_id, 
        block->height(), 
        latest_valid_elect_height_,
        elect_block);
    pools_mgr_->OnNewElectBlock(sharding_id, elect_height, members);
    kv_sync_->OnNewElectBlock(sharding_id, block->height());
    network::UniversalManager::Instance()->OnNewElectBlock(
        sharding_id,
        elect_height,
        members,
        elect_block);
    SHARDORA_DEBUG("%s success called election block. height: %lu, "
        "elect height: %lu, latest_valid_elect_height_: %lu, used elect height: %lu, net: %u, "
        "local net id: %u, prev elect height: %lu",
        common::Encode::HexEncode(security_->GetAddress()).c_str(),
        block->height(), elect_height,
        latest_valid_elect_height_,
        view_block->qc().elect_height(),
        elect_block->shard_network_id(),
        common::GlobalInfo::Instance()->network_id(),
        elect_block->prev_members().prev_elect_height());
    if (sharding_id + network::kConsensusWaitingShardOffset ==
            common::GlobalInfo::Instance()->network_id()) {
        join_elect_tick_.CutOff(
            3000000lu,
            std::bind(&NetworkInit::SendJoinElectTransaction, this));
        SHARDORA_DEBUG("now send join elect request transaction. first message.");
        another_join_elect_msg_needed_ = true;
    } else if (another_join_elect_msg_needed_ &&
            sharding_id == common::GlobalInfo::Instance()->network_id()) {
        join_elect_tick_.CutOff(
            3000000lu,
            std::bind(&NetworkInit::SendJoinElectTransaction, this));
        SHARDORA_DEBUG("now send join elect request transaction. second message.");
        another_join_elect_msg_needed_ = false;
    }
}

void NetworkInit::JoinInitNodes() {
    std::string init_nodes;
    conf_.Get("shardora", "bootstrap", init_nodes);
    common::Split<10240> nodes(init_nodes.c_str(), ',');
    for (uint32_t i = 0; i < nodes.Count(); ++i) {
        common::Split<> items(nodes[i], ':');
        if (items.Count() != 4) {
            continue;
        }

        auto node = std::make_shared<dht::Node>();
        node->pubkey_str = common::Encode::HexDecode(items[0]);
        node->id = security_->GetAddressWithPublicKey(node->pubkey_str);
        node->public_ip = items[1];
        common::StringUtil::ToUint16(items[2], &node->public_port);
        common::StringUtil::ToInt32(items[3], &node->sharding_id);
        SHARDORA_DEBUG("join init node: %s:%d, %d, pk: %s, id: %s", 
            node->public_ip.c_str(), node->public_port, node->sharding_id,
            items[0], common::Encode::HexEncode(node->id).c_str());
        if (network::IsSameToLocalShard(node->sharding_id)) {
            network::DhtManager::Instance()->Join(node);
        }

        node->join_way = dht::kJoinFromInit;
        network::UniversalManager::Instance()->Join(node);
    }
}

void NetworkInit::SendJoinElectTransaction() {
    if (common::GlobalInfo::Instance()->network_id() < network::kConsensusShardBeginNetworkId) {
        return;
    }

    if (common::GlobalInfo::Instance()->network_id() >= network::kConsensusWaitingShardEndNetworkId) {
        return;
    }

    auto local_node_account_info = prefix_db_->GetAddressInfo(security_->GetAddress());
    if (local_node_account_info == nullptr) {
        SHARDORA_DEBUG("failed get address info: %s",
            common::Encode::HexEncode(security_->GetAddress()).c_str());
        return;
    }
    
    if (des_sharding_id_ == common::kInvalidUint32) {
        if (local_node_account_info == nullptr) {
            SHARDORA_DEBUG("failed get address info: %s",
                common::Encode::HexEncode(security_->GetAddress()).c_str());
            return;
        }

        des_sharding_id_ = local_node_account_info->sharding_id();
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
    new_tx->set_gas_limit(consensus::kBlockMaxGasLimit);
    new_tx->set_gas_price(1);
    new_tx->set_key(protos::kJoinElectVerifyG2);
    bls::protobuf::JoinElectInfo join_info;
    uint32_t pos = common::kInvalidUint32;
    prefix_db_->GetLocalElectPos(security_->GetAddress(), &pos);
    join_info.set_member_idx(pos);
    join_info.set_public_key(security_->GetPublicKey());
    if (common::GlobalInfo::Instance()->network_id() >= network::kConsensusShardEndNetworkId) {
        join_info.set_shard_id(
            common::GlobalInfo::Instance()->network_id() -
            network::kConsensusWaitingShardOffset);
    } else {
        join_info.set_shard_id(common::GlobalInfo::Instance()->network_id());
    }
    
    // Check if user already has stake info
    uint64_t existing_stake = 0;
    uint64_t existing_timestamp = 0;
    bool has_existing_stake = prefix_db_->GetStakeInfo(
        security_->GetAddress(), &existing_stake, &existing_timestamp);
    
    // Get stake amount from config (in units of 8 * 10^8)
    uint64_t stake_units = 0;
    conf_.Get("shardora", "stake_units", stake_units);
    
    if (has_existing_stake) {
        // User already has stake, no need to stake again
        // Send join_elect with stake_amount = 0 to participate using existing stake
        join_info.set_stake_op(bls::protobuf::STAKE_OP_NONE);
        join_info.set_stake_amount(0);
        SHARDORA_DEBUG("User already has stake: %lu coins, sending join_elect without additional stake",
            existing_stake);
    } else if (stake_units > 0) {
        // User doesn't have stake, and config specifies stake amount
        // Minimum stake unit is 8 * 10^8 coins (8 SHARDORA)
        static const uint64_t kMinStakeUnit = 8 * 100000000llu;
        uint64_t stake_amount = stake_units * kMinStakeUnit;
        
        // Check if account has enough balance
        if (msg_ptr->address_info->balance() >= stake_amount) {
            join_info.set_stake_op(bls::protobuf::STAKE_OP_STAKE);
            join_info.set_stake_amount(stake_amount);
            // Note: stake_timestamp will be set by consensus using block timestamp
            // to prevent user timestamp manipulation
            
            // Get current elect height for stake tracking
            elect::protobuf::ElectBlock elect_block;
            if (prefix_db_->GetLatestElectBlock(des_sharding_id_, &elect_block)) {
                join_info.set_stake_elect_height(elect_block.elect_height());
            }
            
            SHARDORA_DEBUG("Sending initial stake transaction: %lu coins (%lu units) to root shard",
                stake_amount, stake_units);
        } else {
            SHARDORA_ERROR("Insufficient balance for staking: have %lu, need %lu",
                msg_ptr->address_info->balance(), stake_amount);
            return;
        }
    } else {
        // No existing stake and no stake_units configured
        // Normal join_elect without staking
        join_info.set_stake_op(bls::protobuf::STAKE_OP_NONE);
        join_info.set_stake_amount(0);
        SHARDORA_DEBUG("Sending join_elect without stake");
    }
    
    // Check if user has already sent g2_req in a previous join_elect transaction
    // by checking if NodeVerificationVector exists in database (saved from consensus block)
    bls::protobuf::JoinElectInfo previous_join_info;
    bool has_sent_g2_req = prefix_db_->GetNodeVerificationVector(
        security_->GetAddress(), &previous_join_info);
    
    auto n = common::GlobalInfo::Instance()->each_shard_max_members();
    auto t = common::GetSignerCount(n);
    
    if (has_sent_g2_req && previous_join_info.has_g2_req() && 
        previous_join_info.g2_req().verify_vec_size() == t) {
        // User has already sent g2_req in a previous transaction (confirmed in consensus block)
        // No need to send it again, leave join_info.g2_req empty
        SHARDORA_DEBUG("User has already sent g2_req in previous join_elect (found in consensus block), "
            "skipping g2_req in this transaction. verify_vec_size: %d",
            previous_join_info.g2_req().verify_vec_size());
    } else {
        // First time sending join_elect, or previous g2_req was invalid
        // Must include g2_req in this transaction
        auto* req = join_info.mutable_g2_req();
        auto res = prefix_db_->GetBlsVerifyG2(security_->GetAddress(), req);
        if (!res || req->verify_vec_size() != t) {
            CreateContribution(req);
        }
#ifndef NDEBUG
        //assert(req->verify_vec_size() >= t);
#endif
        SHARDORA_DEBUG("First time sending join_elect or no valid g2_req in consensus block, "
            "including g2_req in transaction. verify_vec_size: %d", req->verify_vec_size());
    }

    new_tx->set_value(SerializeDeterministic(join_info));
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign;
    if (security_->Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        //assert(false);
        return;
    }

    new_tx->set_sign(sign);
    // msg_ptr->msg_hash = tx_hash; // TxPoolmanager::HandleElectTx The receiving end has calculated it, so there is no need to transmit it here
    network::Route::Instance()->Send(msg_ptr);
    SHARDORA_DEBUG("success send join elect request transaction: %u, join: %u, addr: %s, nonce: %lu, "
        "stake_amount: %lu, hash64: %lu, tx hash: %s, pk: %s sign: %s",
        des_sharding_id_, join_info.shard_id(),
        common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
        new_tx->nonce(),
        join_info.stake_amount(),
        msg.hash64(),
        common::Encode::HexEncode(tx_hash).c_str(),
        common::Encode::HexEncode(new_tx->pubkey()).c_str(),
        common::Encode::HexEncode(new_tx->sign()).c_str());
}

void NetworkInit::SendRedeemStakeTransaction() {
    if (common::GlobalInfo::Instance()->network_id() < network::kConsensusShardBeginNetworkId) {
        return;
    }

    if (common::GlobalInfo::Instance()->network_id() >= network::kConsensusWaitingShardEndNetworkId) {
        return;
    }

    auto local_node_account_info = prefix_db_->GetAddressInfo(security_->GetAddress());
    if (local_node_account_info == nullptr) {
        SHARDORA_DEBUG("failed get address info: %s",
            common::Encode::HexEncode(security_->GetAddress()).c_str());
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    msg_ptr->address_info = account_mgr_->GetAccountInfo(security_->GetAddress());
    transport::protobuf::Header& msg = msg_ptr->header;
    dht::DhtKeyManager dht_key(network::kRootCongressNetworkId);  // Send to root shard
    msg.set_src_sharding_id(common::GlobalInfo::Instance()->network_id());
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    msg.set_hop_count(0);
    
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_nonce(msg_ptr->address_info->nonce() + 1);
    new_tx->set_pubkey(security_->GetPublicKeyUnCompressed());
    new_tx->set_step(pools::protobuf::kJoinElect);  // Use kJoinElect
    new_tx->set_gas_limit(consensus::kJoinElectGas + 10000000lu);
    new_tx->set_gas_price(1);
    new_tx->set_key(protos::kJoinElectVerifyG2);
    
    bls::protobuf::JoinElectInfo join_info;
    join_info.set_stake_op(bls::protobuf::STAKE_OP_REDEEM);  // Redeem operation
    join_info.set_stake_amount(0);  // No amount needed for redeem
    join_info.set_addr(security_->GetAddress());
    join_info.set_public_key(security_->GetPublicKey());
    join_info.set_shard_id(common::GlobalInfo::Instance()->network_id());
    
    // Still need g2_req for validation
    uint32_t pos = common::kInvalidUint32;
    prefix_db_->GetLocalElectPos(security_->GetAddress(), &pos);
    join_info.set_member_idx(pos);
    
    // Check if user has already sent g2_req in a previous join_elect transaction
    // by checking if NodeVerificationVector exists in database (saved from consensus block)
    bls::protobuf::JoinElectInfo previous_join_info;
    bool has_sent_g2_req = prefix_db_->GetNodeVerificationVector(
        security_->GetAddress(), &previous_join_info);
    
    auto n = common::GlobalInfo::Instance()->each_shard_max_members();
    auto t = common::GetSignerCount(n);
    
    if (has_sent_g2_req && previous_join_info.has_g2_req() && 
        previous_join_info.g2_req().verify_vec_size() == t) {
        // User has already sent g2_req in a previous transaction (confirmed in consensus block)
        // No need to send it again for redeem operation
        SHARDORA_DEBUG("Redeem: User has already sent g2_req in previous join_elect (found in consensus block), "
            "skipping g2_req in redeem transaction. verify_vec_size: %d",
            previous_join_info.g2_req().verify_vec_size());
    } else {
        // First time or previous g2_req was invalid, must include g2_req
        auto* req = join_info.mutable_g2_req();
        auto res = prefix_db_->GetBlsVerifyG2(security_->GetAddress(), req);
        if (!res || req->verify_vec_size() != t) {
            CreateContribution(req);
        }
        SHARDORA_DEBUG("Redeem: Including g2_req in transaction. verify_vec_size: %d", req->verify_vec_size());
    }
    
    new_tx->set_value(SerializeDeterministic(join_info));
    transport::TcpTransport::Instance()->SetMessageHash(msg);
    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign;
    if (security_->Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        //assert(false);
        return;
    }

    new_tx->set_sign(sign);
    network::Route::Instance()->Send(msg_ptr);
    
    SHARDORA_DEBUG("Sent redeem stake transaction to root shard: addr=%s, nonce=%lu",
        common::Encode::HexEncode(msg_ptr->address_info->addr()).c_str(),
        new_tx->nonce());
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
