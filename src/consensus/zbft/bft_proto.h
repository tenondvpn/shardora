#pragma once

#include "bls/bls_manager.h"
#include "common/utils.h"
#include "consensus/zbft/zbft.h"
#include "dht/dht_utils.h"
#include "protos/zbft.pb.h"
#include "protos/transport.pb.h"
#include "pools/tx_pool.h"
#include "security/security.h"

namespace zjchain {

namespace consensus {

class BftProto {
public:
    static bool LeaderCreatePrepare(
        int32_t leader_idx,
        const ZbftPtr& bft_ptr,
        const std::string& precommit_gid,
        const std::string& commit_gid,
        transport::protobuf::Header& msg,
        zbft::protobuf::ZbftMessage* pipeline_msg);
    static bool BackupCreatePrepare(
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        const ZbftPtr& bft_ptr,
        bool agree,
        const std::string& pre_commit_gid,
        zbft::protobuf::ZbftMessage* pipeline_msg);
    static bool LeaderCreatePreCommit(
        int32_t leader_idx,
        const ZbftPtr& bft_ptr,
        bool oppose,
        const std::string& commit_gid,
        transport::protobuf::Header& msg);
    static bool BackupCreatePreCommit(
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        const ZbftPtr& bft_ptr,
        bool agree,
        transport::protobuf::Header& msg);
    static bool LeaderCreateCommit(
        int32_t leader_idx,
        const ZbftPtr& bft_ptr,
        bool agree,
        transport::protobuf::Header& msg);

    DISALLOW_COPY_AND_ASSIGN(BftProto);
};

}  // namespace consensus

}  // namespace zjchain
