#pragma once

#include <string>

#include "common/encode.h"

namespace shardora {

namespace protos {

static const std::string kRootCreateAddressKey = "_kRootCreateAddressKey";
static const std::string kContractBytesStartCode = common::Encode::HexDecode("60806040");
static const std::string kContractDestruct = "__destruct";
static const std::string kLibrary = "__library";
static const std::string kNormalTos = "__normal_tos";
static const std::string kConsensusLocalNormalTos = "__consensus_local_tos";
static const std::string kCreateContractCallerSharding = "__new_contract_user_shard";
static const std::string kCreateContractBytesCode = "__kCreateContractBytesCode";
static const std::string kCreateContractLocalInfo = "__kCreateContractLocalInfo";
static const std::string kShardElection = "__shard_election";
static const std::string kShardElectionPrevInfo = "__shard_elect_prev";
static const std::string kElectNodeStoke = "__elect_node_stoke";
static const std::string kShardCross = "__shard_cross";
static const std::string kJoinElectVerifyG2 = "__join_g2";
static const std::string kRootCross = "__root_cross";

static const std::string kElectNodeAttrKeyAllBloomfilter = "__elect_allbloomfilter";
static const std::string kElectNodeAttrKeyWeedoutBloomfilter = "__elect_weedoutbloomfilter";
static const std::string kElectNodeAttrKeyAllPickBloomfilter = "__elect_allpickbloomfilter";
static const std::string kElectNodeAttrKeyPickInBloomfilter = "__elect_pickinbloomfilter";
static const std::string kElectNodeAttrElectBlock = "__elect_block";

static const std::string kAttrTimerBlock = "__tmblock_tmblock";
static const std::string kAttrTimerBlockHeight = "__tmblock_tmblock_height";
static const std::string kAttrTimerBlockTm = "__tmblock_tmblock_tm";
static const std::string kAttrGenesisTimerBlock = "__tmblock_genesis";
static const std::string kVssRandomAttr = "__vssrandomattr";

static const std::string kStatisticAttr = "__kStatisticAttr";
static const std::string kNodePublicKey = "__node_pk";
static const std::string kAggBlsPublicKey = "__agg_bls_pk";
static const std::string kAggBlsPopProof = "__agg_bls_pk_proof";

static const std::string kSingleTxHashTag = "__single_tx_hash";
static const std::string kPoolStatisticTag = "__pool_st_tag";

};  // namespace protos

};  // namespace shardora
