#pragma once

#include "block/account_manager.h"
#include "block/block_manager.h"
#include "bls/bls_manager.h"
#include "common/utils.h"
#include "common/config.h"
#include "common/parse_args.h"
#include "common/tick.h"
#include "consensus/hotstuff/hotstuff_manager.h"
#include <consensus/hotstuff/crypto.h>
#include <consensus/hotstuff/elect_info.h>
#include <consensus/hotstuff/pacemaker.h>
#include <consensus/hotstuff/hotstuff_syncer.h>
#include "consensus/zbft/contract_gas_prepayment.h"
#include "contract/contract_manager.h"
#include "db/db.h"
#include "elect/elect_manager.h"
#include "http/http_server.h"
#include "init/command.h"
#include "init/http_handler.h"
#include "init/init_utils.h"
#include "init/ws_server.h"
#include "pools/shard_statistic.h"
#include "pools/tx_pool_manager.h"
#include "protos/elect.pb.h"
#include "security/security.h"
#include "sync/key_value_sync.h"
#include "timeblock/time_block_manager.h"
#include "transport/multi_thread.h"
#include "vss/vss_manager.h"
#include <libff/algebra/curves/alt_bn128/alt_bn128_init.hpp>
#include <protos/prefix_db.h>
#include <yaml-cpp/node/node.h>

namespace shardora {

namespace init {

class NetworkInit {
public:
    NetworkInit();
    ~NetworkInit();
    int Init(int argc, char** argv);
    void Destroy();

private:
    int InitConfigWithArgs(int argc, char** argv);
    int ParseParams(int argc, char** argv, common::ParserArgs& parser_arg);
    int ResetConfig(common::ParserArgs& parser_arg);
    int InitNetworkSingleton();
    int InitCommand();
    int InitHttpServer();
    int InitSecurity();
    int CheckJoinWaitingPool();
    int GenesisCmd(common::ParserArgs& parser_arg, std::string& net_name);
    void AddCmds();
    void GetNetworkNodesFromConf(
        uint32_t cons_shard_node_count,
        std::vector<GenisisNodeInfoPtr>&, 
        std::vector<GenisisNodeInfoPtrVector>&, 
        const std::shared_ptr<db::Db>&);
    void InitAggBlsForGenesis(const std::string& node_id, std::shared_ptr<security::Security>& security_ptr, std::shared_ptr<protos::PrefixDb>&);
    void GetAggBlsSkFromFile(const std::string& node_id, libff::alt_bn128_Fr* agg_bls_sk);
    void WriteAggBlsSkToFile(const std::string& node_id, const libff::alt_bn128_Fr& agg_bls_sk);
    void AddBlockItemToCache(
        std::shared_ptr<view_block::protobuf::ViewBlockItem>& block,
        db::DbWriteBatch& db_batch);
    void InitLocalNetworkId();
    bool DbNewBlockCallback(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& block,
        db::DbWriteBatch& db_batch);
    void HandleTimeBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleElectionBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch);
    void HandleMessage(const transport::MessagePtr& msg_ptr);
    void HandleAddrReq(const transport::MessagePtr& msg_ptr);
    void HandleAddrRes(const transport::MessagePtr& msg_ptr);
    void GetAddressShardingId();
    void BroadcastInvalidPools(
        std::shared_ptr<LeaderRotationInfo> leader_rotation,
        int32_t mod_num);
    void RegisterFirewallCheck();
    int FirewallCheckMessage(transport::MessagePtr& msg_ptr);
    int InitWsServer();
    void CreateInitAddress(uint32_t net_id);
    void SendJoinElectTransaction();
    void CreateContribution(bls::protobuf::VerifyVecBrdReq* bls_verify_req);
        
    static const uint32_t kInvalidPoolFactor = 50u;  // 50%
    static const uint32_t kMinValodPoolCount = 4u;  // 64 must finish all

    common::Config conf_;
    bool inited_{ false };
    Command cmd_;
    transport::MultiThreadHandler net_handler_;
    std::string config_path_;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr_ = nullptr;
    std::shared_ptr<hotstuff::HotstuffSyncer> hotstuff_syncer_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<consensus::ContractGasPrepayment> gas_prepayment_ = nullptr;
    std::shared_ptr<pools::ShardStatistic> shard_statistic_ = nullptr;    
    http::HttpServer http_server_;
    HttpHandler http_handler_;
    common::Tick init_tick_;
    common::Tick join_elect_tick_;
    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    uint32_t des_sharding_id_ = common::kInvalidUint32;
    uint32_t invalid_pools_[common::kInvalidPoolIndex] = { 0 };
    uint64_t latest_elect_height_ = 0;
    std::shared_ptr<LeaderRotationInfo> rotation_leaders_ = nullptr;
    // 是否还需要发送一次 JoinElect
    bool another_join_elect_msg_needed_ = false;
    WsServer ws_server_;

    DISALLOW_COPY_AND_ASSIGN(NetworkInit);
};

}  // namespace init

}  // namespace shardora
