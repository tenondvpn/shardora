#pragma once

#include "block/account_manager.h"
#include "block/block_manager.h"
#include "bls/bls_manager.h"
#include "common/utils.h"
#include "common/config.h"
#include "common/parse_args.h"
#include "common/tick.h"
#include "consensus/zbft/bft_manager.h"
#include "db/db.h"
#include "elect/elect_manager.h"
#include "http/http_server.h"
#include "init/command.h"
#include "init/http_handler.h"
#include "pools/tx_pool_manager.h"
#include "security/security.h"
#include "sync/key_value_sync.h"
#include "timeblock/time_block_manager.h"
#include "transport/multi_thread.h"
#include "vss/vss_manager.h"

namespace zjchain {

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
    int GenesisCmd(common::ParserArgs& parser_arg);
    void ElectBlockCallback(
        uint32_t sharding_id,
        uint64_t elect_height,
        common::MembersPtr& members);
    void AddBlockItemToCache(
        uint8_t thread_idx,
        std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch);
    void InitLocalNetworkId();
    void DbNewBlockCallback(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch);
    void HandleTimeBlock(
        uint8_t thread_idx,
        const std::shared_ptr<block::protobuf::Block>& block,
        db::DbWriteBatch& db_batch);

    common::Config conf_;
    bool inited_{ false };
    Command cmd_;
    transport::MultiThreadHandler net_handler_;
    std::string config_path_;
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<consensus::BftManager> bft_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    http::HttpServer http_server_;
    HttpHandler http_handler_;
    uint8_t main_thread_idx_ = 255;

    DISALLOW_COPY_AND_ASSIGN(NetworkInit);
};

}  // namespace init

}  // namespace zjchain
