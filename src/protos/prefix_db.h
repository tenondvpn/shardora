#pragma once

#include <evmc/evmc.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_init.hpp>
#include <libbls/tools/utils.h>
#include <protos/view_block.pb.h>

#include "common/encode.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/string_utils.h"
#include "common/tick.h"
#include "common/time_utils.h"
#include "db/db.h"
#include "protos/address.pb.h"
#include "protos/block.pb.h"
#include "protos/bls.pb.h"
#include "protos/c2c.pb.h"
#include "protos/elect.pb.h"
#include "protos/init.pb.h"
#include "protos/sync.pb.h"
#include "protos/timeblock.pb.h"
#include "protos/transport.pb.h"
#include "protos/tx_storage_key.h"
#include "protos/ws.pb.h"
#include "protos/view_block.pb.h"
#include "security/security.h"
#include <zjcvm/zjcvm_utils.h>
#include "consensus/hotstuff/types.h"

namespace shardora {

namespace protos {

typedef std::shared_ptr<address::protobuf::AddressInfo> AddressInfoPtr;
static const std::string kFieldBytesCode = "__bytes_code";
static const std::string kGenesisElectPrikeyEncryptKey = common::Encode::HexDecode(
    "17dfdd4d49509691361225e9059934675dea440d123aa8514441aa6788354016");

// transaction contract attr keys

static const std::string kAddressPrefix = "a\x01";
static const std::string kBlsVerifyPrefex = "b\x01";
static const std::string kBlsSwapKeyPrefex = "c\x01";
static const std::string kAddressStorageKeyPrefex = "d\x01";
static const std::string kBlsSecKeyPrefex = "e\x01";
static const std::string kAddressPubkeyPrefex = "f\x01";
static const std::string kBlsPrivateKeyPrefix = "g\x01";
static const std::string kLatestElectBlockPrefix = "h\x01";
static const std::string kLatestTimeBlockPrefix = "i\x01";
static const std::string kBlockHeightPrefix = "j\x01";
static const std::string kBlockPrefix = "k\x01";
static const std::string kToTxsHeightsPrefix = "l\x01";
static const std::string kLatestToTxsHeightsPrefix = "m\x01";
static const std::string kLatestPoolPrefix = "n\x01";
static const std::string kHeightTreePrefix = "o\x01";
static const std::string kGidPrefix = "p\x01";
static const std::string kContractGasPrepaymentPrefix = "q\x01";
static const std::string kBlsInfoPrefix = "r\x01";
static const std::string kBlsVerifyValuePrefix = "s\x01";
static const std::string kTemporaryKeyPrefix = "t\x01";
static const std::string kPresetPolynomialPrefix = "u\x01";
static const std::string kPresetVerifyValuePrefix = "v\x01";
static const std::string kStatisticHeightsPrefix = "w\x01";
static const std::string kConsensusedStatisticPrefix = "x\x01";
static const std::string kRootStatisticedPrefix = "y\x01";
static const std::string kElectNodesStokePrefix = "z\x01";
static const std::string kSaveLatestElectHeightPrefix = "aa\x01";
static const std::string kSaveChoosedJoinShardPrefix = "ab\x01";
static const std::string kGenesisTimeblockPrefix = "ac\x01";
static const std::string kLocalPolynomialPrefix = "ad\x01";
static const std::string kLocalVerifiedG2Prefix = "ae\x01";
static const std::string kLocalTempPolynomialPrefix = "af\x01";
static const std::string kLocalTempCommonPublicKeyPrefix = "ag\x01";
static const std::string kNodeVerificationVectorPrefix = "ah\x01";
static const std::string kNodeLocalElectPosPrefix = "ai\x01";
static const std::string kCrossCheckHeightPrefix = "aj\x01";
static const std::string kElectHeightWithBlsCommonPkPrefix = "ak\x01";
static const std::string kBftInvalidHeightHashs = "al\x01";
static const std::string kTempBftInvalidHeightHashs = "am\x01";
static const std::string kViewBlockInfoPrefix = "an\x01";

static const std::string kBandwidthPrefix = "ao\x01";
static const std::string kC2cSelloutPrefix = "ap\x01";
static const std::string kC2cSellorderPrefix = "aq\x01";
static const std::string kSaveHavePrevLatestElectHeightPrefix = "ar\x01";
static const std::string kLatestToTxBlock = "as\x01";
static const std::string kLatestPoolStatisticTagPrefix = "at\x01";
static const std::string kViewBlockHashKeyPrefix = "au\x01";
static const std::string kViewBlockParentHashKeyPrefix = "av\x01";
static const std::string kLatestLeaderProposeMessage = "aw\x01";
static const std::string kAggBlsPrivateKeyPrefix = "ax\x01";
static const std::string kCommitedGidPrefix = "ay\x01";
static const std::string kGidWithBlockHash = "az\x01";
static const std::string kViewBlockVaildParentHash = "ba\x01";
static const std::string kViewBlockVaildView = "bb\x01";

class PrefixDb {
public:
    PrefixDb(const std::shared_ptr<db::Db>& db_ptr) : db_(db_ptr) {
    }

    ~PrefixDb() {
        Destroy();
    }

    void InitGidManager() {
        // db_batch_tick_.CutOff(
        //     5000000lu,
        //     std::bind(&PrefixDb::DumpGidToDb, this));
    }

    void Destroy() {
    }

    void AddAddressInfo(
            const std::string& addr,
            const address::protobuf::AddressInfo& addr_info) {
        AddAddressInfo(addr, addr_info.SerializeAsString());
    }

    void AddAddressInfo(
            const std::string& addr,
            const address::protobuf::AddressInfo& addr_info,
            db::DbWriteBatch& db_batch) {
        db_batch.Put(kAddressPrefix + addr, addr_info.SerializeAsString());
        ZJC_DEBUG("success add addr: %s", common::Encode::HexEncode(kAddressPrefix + addr).c_str());
    }

    void AddAddressInfo(const std::string& addr, const std::string& val) {
        auto st = db_->Put(kAddressPrefix + addr, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    void AddAddressInfo(
            const std::string& addr,
            const std::string& val,
            db::DbWriteBatch& write_batch) {
        write_batch.Put(kAddressPrefix + addr, val);
    }

    void AddNowElectHeight2Plege(const std::string& addr , const uint64_t height , db::DbWriteBatch& db_batch) {
        auto key = common::Encode::HexDecode(addr) + common::Encode::HexDecode("0000000000000000000000000000000000000000000000000000000000000000");
        evmc::bytes32 tmp_val{};
        zjcvm::Uint64ToEvmcBytes32(tmp_val, height);

        auto value =  std::string((char*)tmp_val.bytes, sizeof(tmp_val.bytes));
        SaveTemporaryKv(key, value, db_batch);
    }

    std::shared_ptr<address::protobuf::AddressInfo> GetAddressInfo(const std::string& addr) {
        std::string val;
        auto st = db_->Get(kAddressPrefix + addr, &val);
        if (!st.ok()) {
            ZJC_DEBUG("failed get addr: %s", common::Encode::HexEncode(kAddressPrefix + addr).c_str());
            return nullptr;
        }

        auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
        if (!addr_info->ParseFromString(val)) {
            ZJC_INFO("failed parse addr: %s", common::Encode::HexEncode(kAddressPrefix + addr).c_str());
            return nullptr;
        }

        return addr_info;
    }

    void SaveSwapKey(
            uint32_t local_member_idx,
            uint64_t height,
            uint32_t local_idx,
            uint32_t peer_idx,
            const std::string& seckey) {
        std::string key;
        key.reserve(32);
        key.append(kBlsSwapKeyPrefex);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&height, sizeof(height));
        key.append((char*)&local_idx, sizeof(local_idx));
        key.append((char*)&peer_idx, sizeof(peer_idx));
        auto st = db_->Put(key, seckey);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    bool GetSwapKey(
            uint32_t local_member_idx,
            uint64_t height,
            uint32_t local_idx,
            uint32_t peer_idx,
            std::string* seckey) {
        std::string key;
        key.reserve(32);
        key.append(kBlsSwapKeyPrefex);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&height, sizeof(height));
        key.append((char*)&local_idx, sizeof(local_idx));
        key.append((char*)&peer_idx, sizeof(peer_idx));
        auto st = db_->Get(key, seckey);
        if (!st.ok()) {
            return false;
        }

        return true;
    }

    void SaveLatestElectBlock(
            const elect::protobuf::ElectBlock& elect_block,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(48);
        key.append(kLatestElectBlockPrefix);
        auto sharding_id = elect_block.shard_network_id();
        key.append((char*)&sharding_id, sizeof(sharding_id));
        db_batch.Put(key, elect_block.SerializeAsString());

        key.clear();
        key.reserve(64);
        key.append(kSaveLatestElectHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        auto st = db_->Get(key, &val);
        std::set<uint64_t> height_set;
        if (st.ok()) {
            uint64_t* tmp = (uint64_t*)val.c_str();
            uint32_t len = val.size() / 8;
            for (uint32_t i = 0; i < len; ++i) {
                height_set.insert(tmp[i]);
            }
        }

        auto height = elect_block.elect_height();
        height_set.insert(height);
        auto biter = height_set.begin();
        while (height_set.size() > kSaveElectHeightCount) {
            height_set.erase(biter++);
        }

        char data[8 * kSaveElectHeightCount];
        uint64_t* tmp = (uint64_t*)data;
        uint32_t idx = 0;
        for (auto hiter = height_set.begin(); hiter != height_set.end(); ++hiter) {
            tmp[idx++] = *hiter;
        }

        std::string db_val(data, sizeof(data));
        db_batch.Put(key, db_val);

        if (elect_block.has_prev_members()) {
            key.clear();
            key.reserve(64);
            key.append(kSaveHavePrevLatestElectHeightPrefix);
            key.append((char*)&sharding_id, sizeof(sharding_id));
            db_batch.Put(key, elect_block.SerializeAsString());
        }

        ZJC_DEBUG("save elect block sharding id: %u, height: %lu",
            sharding_id, elect_block.elect_height());
    }

    bool GetHavePrevlatestElectBlock(
            uint32_t sharding_id,
            elect::protobuf::ElectBlock* elect_block) {
        std::string key;
        key.reserve(48);
        key.append(kSaveHavePrevLatestElectHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        ZJC_DEBUG("get elect block sharding id: %u",
            sharding_id);
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!elect_block->ParseFromString(val)) {
            return false;
        }

        ZJC_DEBUG("success get elect block sharding id: %u, height: %lu",
            sharding_id, elect_block->elect_height());
        return true;
    }

    bool GetLatestElectBlock(uint32_t sharding_id,
            elect::protobuf::ElectBlock* elect_block) {
        std::string key;
        key.reserve(48);
        key.append(kLatestElectBlockPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        ZJC_DEBUG("get elect block sharding id: %u",
            sharding_id);
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!elect_block->ParseFromString(val)) {
            return false;
        }

        ZJC_DEBUG("success get elect block sharding id: %u, height: %lu",
            sharding_id, elect_block->elect_height());
        return true;
    }

    void SaveLatestTimeBlock(uint64_t block_height, db::DbWriteBatch& db_batch) {
        std::string key(kLatestTimeBlockPrefix);
        timeblock::protobuf::TimeBlock tmblock;
        if (GetLatestTimeBlock(&tmblock)) {
            if (tmblock.height() >= block_height) {
                return;
            }
        }

        tmblock.set_height(block_height);
        db_batch.Put(key, tmblock.SerializeAsString());
        ZJC_DEBUG("dddddd success latest time block: %lu", block_height);
    }

    bool GetLatestTimeBlock(timeblock::protobuf::TimeBlock* tmblock) {
        std::string key(kLatestTimeBlockPrefix);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!tmblock->ParseFromString(val)) {
            return false;
        }

        ZJC_DEBUG("dddddd success get latest time block: %lu", tmblock->height());
        return true;
    }

    void SaveBlockHashWithBlockHeight(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            const std::string& block_hash,
            db::DbWriteBatch& batch) {
        std::string key;
        key.reserve(32);
        key.append(kBlockHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        batch.Put(key, block_hash);
        ZJC_DEBUG("save sync key value %u_%u_%lu, success save block with height: %u, %u, %lu",
            sharding_id, pool_index, height, sharding_id, pool_index, height);
    }

    bool GetBlockHashWithBlockHeight(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            std::string* block_hash) {
        std::string key;
        key.reserve(32);
        key.append(kBlockHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        auto st = db_->Get(key, block_hash);
        if (!st.ok()) {
            ZJC_DEBUG("failed get sync key value %u_%u_%lu, success get block with height: %u, %u, %lu",
                sharding_id, pool_index, height, sharding_id, pool_index, height);
            return false;
        }

        ZJC_DEBUG("get sync key value %u_%u_%lu, success get block with height: %u, %u, %lu",
            sharding_id, pool_index, height, sharding_id, pool_index, height);
        return true;
    }

    bool SaveBlock(const view_block::protobuf::ViewBlockItem& view_block, db::DbWriteBatch& batch) {
        assert(!view_block.qc().view_block_hash().empty());
        if (BlockExists(view_block.qc().view_block_hash())) {
            auto* block_item = &view_block.block_info();
            ZJC_DEBUG("view_block.qc().view_block_hash() exists: %s, "
                "new block coming sharding id: %u_%d_%lu, view: %u_%u_%lu,"
                "tx size: %u, hash: %s, elect height: %lu, tm height: %lu",
                common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(),
                view_block.qc().network_id(),
                view_block.qc().pool_index(),
                block_item->height(),
                view_block.qc().network_id(),
                view_block.qc().pool_index(),
                view_block.qc().view(),
                block_item->tx_list_size(),
                common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(),
                view_block.qc().elect_height(),
                block_item->timeblock_height());
            std::string block_hash;
            assert(GetBlockHashWithBlockHeight(
                view_block.qc().network_id(),
                view_block.qc().pool_index(),
                block_item->height(),
                &block_hash));
            return false;
        }

        std::string key;
        key.reserve(48);
        key.append(kBlockPrefix);
        key.append(view_block.qc().view_block_hash());
        auto& block = view_block.block_info();
        SaveBlockHashWithBlockHeight(
            view_block.qc().network_id(),
            view_block.qc().pool_index(),
            block.height(),
            view_block.qc().view_block_hash(),
            batch);
        batch.Put(key, view_block.SerializeAsString());
        std::string view_key;
        view_key.reserve(48);
        view_key.append(kViewBlockVaildView);
        char key_data[16];
        uint32_t *u32_arr = (uint32_t*)key_data;
        u32_arr[0] = view_block.qc().network_id();
        u32_arr[1] = view_block.qc().pool_index();
        uint64_t* u64_arr = (uint64_t*)(key_data + 8);
        u64_arr[0] = view_block.qc().view();
        view_key.append(std::string(key_data, sizeof(key_data)));
        batch.Put(view_key, "1");
        return true;
    }

    bool ViewBlockIsValidView(uint32_t network_id, uint32_t pool_index, uint64_t view) {
        std::string view_key;
        view_key.reserve(48);
        view_key.append(kViewBlockVaildView);
        char key_data[16];
        uint32_t *u32_arr = (uint32_t*)key_data;
        u32_arr[0] = network_id;
        u32_arr[1] = pool_index;
        uint64_t* u64_arr = (uint64_t*)(key_data + 8);
        u64_arr[0] = view;
        view_key.append(std::string(key_data, sizeof(key_data)));
        return db_->Exist(view_key);
    }

    void SaveGidWithBlockHash(
            const std::string& gid, 
            const std::string& hash, 
            db::DbWriteBatch& batch) {
        std::string key;
        key.reserve(48);
        key.append(kGidWithBlockHash);
        key.append(gid);
        batch.Put(key, hash);
    }

    bool GetBlockWithGid(const std::string& gid, view_block::protobuf::ViewBlockItem* block) {
        std::string key;
        key.reserve(48);
        key.append(kGidWithBlockHash);
        key.append(gid);
        std::string hash;
        auto st = db_->Get(key, &hash);
        if (!st.ok()) {
            return false;
        }

        return GetBlock(hash, block);
    }

    bool GetBlock(const std::string& block_hash, view_block::protobuf::ViewBlockItem* block) {
        std::string key;
        key.reserve(48);
        key.append(kBlockPrefix);
        key.append(block_hash);
        std::string block_str;
        auto st = db_->Get(key, &block_str);
        if (!st.ok()) {
            ZJC_DEBUG("failed get view block: %s", common::Encode::HexEncode(block_hash).c_str());
            return false;
        }

        if (!block->ParseFromString(block_str)) {
            return false;
        }

        return true;
    }

    bool GetBlockString(const std::string& block_hash, std::string* block_str) {
        std::string key;
        key.reserve(48);
        key.append(kBlockPrefix);
        key.append(block_hash);
        auto st = db_->Get(key, block_str);
        if (!st.ok()) {
            return false;
        }

        return true;
    }

    bool BlockExists(const std::string& block_hash) {
        std::string key;
        key.reserve(48);
        key.append(kBlockPrefix);
        key.append(block_hash);
        return db_->Exist(key);
    }

    bool BlockExists(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height) {
        std::string key;
        key.reserve(32);
        key.append(kBlockHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        return db_->Exist(key);
    }

    bool GetBlockWithHeight(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            view_block::protobuf::ViewBlockItem* block) {
        std::string block_hash;
        ZJC_DEBUG("GetBlockWithHeight.");
        if (!GetBlockHashWithBlockHeight(sharding_id, pool_index, height, &block_hash)) {
            return false;
        }

        return GetBlock(block_hash, block);
    }

    bool GetBlockStringWithHeight(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            std::string* block_str) {
        std::string block_hash;
        ZJC_DEBUG("GetBlockStringWithHeight.");
        if (!GetBlockHashWithBlockHeight(sharding_id, pool_index, height, &block_hash)) {
            return false;
        }

        return GetBlockString(block_hash, block_str);
    }

    void SaveToTxsHeights(const pools::protobuf::ToTxHeights& heights) {
        std::string key;
        key.reserve(48);
        key.append(kToTxsHeightsPrefix);
        auto val = heights.SerializeAsString();
        auto hash = common::Hash::keccak256(val);
        key.append(hash);
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    bool GetToTxsHeights(const std::string& hash, pools::protobuf::ToTxHeights* heights) {
        std::string key;
        key.reserve(48);
        key.append(kToTxsHeightsPrefix);
        key.append(hash);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!heights->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveLatestToTxsHeights(const pools::protobuf::ShardToTxItem& heights) {
        std::string key;
        key.reserve(48);
        key.append(kLatestToTxsHeightsPrefix);
        uint32_t sharding_id = heights.sharding_id();
        key.append((char*)&sharding_id, sizeof(sharding_id));
        auto st = db_->Put(key, heights.SerializeAsString());
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    void SaveLatestToTxsHeights(
            const pools::protobuf::ShardToTxItem& heights,
            db::DbWriteBatch& batch) {
        std::string key;
        key.reserve(48);
        key.append(kLatestToTxsHeightsPrefix);
        uint32_t sharding_id = heights.sharding_id();
        key.append((char*)&sharding_id, sizeof(sharding_id));
        batch.Put(key, heights.SerializeAsString());
    }

    bool GetLatestToTxsHeights(uint32_t sharding_id, pools::protobuf::ShardToTxItem* heights) {
        std::string key;
        key.reserve(48);
        key.append(kLatestToTxsHeightsPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!heights->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveTemporaryKv(
            const std::string& tmp_key,
            const std::string& val) {
        std::string key = kTemporaryKeyPrefix + tmp_key;
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    void SaveTemporaryKv(
            const std::string& tmp_key,
            const std::string& val,
            db::DbWriteBatch& db_batch) {
        std::string key = kTemporaryKeyPrefix + tmp_key;
        db_batch.Put(key, val);
    }

    bool GetTemporaryKv(
            const std::string& tmp_key,
            std::string* val) {
        std::string key = kTemporaryKeyPrefix + tmp_key;
        auto st = db_->Get(key, val);
        return st.ok();
    }

    void SaveLatestPoolInfo(
            uint32_t sharding_id,
            uint32_t pool_index,
            const pools::protobuf::PoolLatestInfo& pool_info,
            db::DbWriteBatch& batch) {
        std::string key;
        key.reserve(48);
        key.append(kLatestPoolPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        batch.Put(key, pool_info.SerializeAsString());
        ZJC_DEBUG("save latest pool info: %s, %u_%u_%lu, synced_height: %lu, hash: %s",
            ProtobufToJson(pool_info).c_str(), 
            sharding_id, 
            pool_index, 
            pool_info.height(), 
            pool_info.synced_height(), 
            common::Encode::HexEncode(pool_info.hash()).c_str());
    }

    bool GetLatestPoolInfo(
            uint32_t sharding_id,
            uint32_t pool_index,
            pools::protobuf::PoolLatestInfo* pool_info) {
        std::string key;
        key.reserve(48);
        key.append(kLatestPoolPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!pool_info->ParseFromString(val)) {
            return false;
        }

        ZJC_DEBUG("get latest pool info: %s, %u_%u_%lu, synced_height: %lu, hash: %s",
            ProtobufToJson(*pool_info).c_str(), 
            sharding_id, 
            pool_index, 
            pool_info->height(), 
            pool_info->synced_height(), 
            common::Encode::HexEncode(pool_info->hash()).c_str());        
        return true;
    }

    void SaveHeightTree(
            uint32_t net_id,
            uint32_t pool_index,
            uint32_t level,
            uint64_t node_index,
            const sync::protobuf::FlushDbItem& flush_db,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kHeightTreePrefix);
        key.append((char*)&net_id, sizeof(net_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&level, sizeof(level));
        key.append((char*)&node_index, sizeof(node_index));
        db_batch.Put(key, flush_db.SerializeAsString());
    }

    bool GetHeightTree(
            uint32_t net_id,
            uint32_t pool_index,
            uint32_t level,
            uint64_t node_index,
            sync::protobuf::FlushDbItem* flush_db) {
        std::string key;
        key.reserve(64);
        key.append(kHeightTreePrefix);
        key.append((char*)&net_id, sizeof(net_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&level, sizeof(level));
        key.append((char*)&node_index, sizeof(node_index));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!flush_db->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    bool JustCheckCommitedGidExists(const std::string& gid) {
        // TODO: perf test
        return false;
        std::string key = kCommitedGidPrefix + gid;
        if (db_->Exist(key)) {
            return true;
        }

        return false;
    }

    bool CheckAndSaveGidExists(const std::string& gid) {
        // TODO: perf test
        return false;
        std::string key = kGidPrefix + gid;
        if (db_->Exist(key)) {
            return true;
        }
        
        // db_->Put(key, "1");
        // ZJC_DEBUG("success save tx gid: %s, res: %d", common::Encode::HexEncode(gid).c_str(), false);
        return false;
    }

    void SaveCommittedGids(
            const google::protobuf::RepeatedPtrField<block::protobuf::BlockTx>& tx_list, 
            db::DbWriteBatch& db_batch) {
        for (uint32_t i = 0; i < tx_list.size(); ++i) {
            std::string key = kCommitedGidPrefix + tx_list[i].gid();
            db_batch.Put(key, "1");
        }
    }

    void SaveContractUserPrepayment(
            const std::string& contract_addr,
            const std::string& user_addr,
            uint64_t height,
            uint64_t prepayment,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kContractGasPrepaymentPrefix);
        key.append(contract_addr);
        key.append(user_addr);
        char data[16];
        uint64_t* tmp = (uint64_t*)data;
        tmp[0] = height;
        tmp[1] = prepayment;
        std::string val(data, sizeof(data));
        db_batch.Put(key, val);
        ZJC_DEBUG("success save contract user prepayment: %s, %lu, %lu",
            common::Encode::HexEncode(key).c_str(), height, prepayment);
    }

    bool GetContractUserPrepayment(
            const std::string& contract_addr,
            const std::string& user_addr,
            uint64_t* height,
            uint64_t* prepayment) {
        std::string key;
        key.reserve(128);
        key.append(kContractGasPrepaymentPrefix);
        key.append(contract_addr);
        key.append(user_addr);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get contract user prepayment failed: %s, %lu, %lu",
                common::Encode::HexEncode(key).c_str(), *height, *prepayment);
            return false;
        }

        uint64_t* data = (uint64_t*)val.c_str();
        *height = data[0];
        *prepayment = data[1];
        return true;
    }

    bool ExistsBlsVerifyG2(const std::string& id) {
        std::string key = kBlsVerifyPrefex + id;
        return db_->Exist(key);
    }
    
    void AddBlsVerifyG2(
            const std::string& id,
            const bls::protobuf::VerifyVecBrdReq& verfy_req) {
        std::string key = kBlsVerifyPrefex + id;
        std::string val = verfy_req.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    void AddBlsVerifyG2(
            const std::string& id,
            const bls::protobuf::VerifyVecBrdReq& verfy_req,
            db::DbWriteBatch& db_batch) {
        std::string key = kBlsVerifyPrefex + id;
        std::string val = verfy_req.SerializeAsString();
        db_batch.Put(key, val);
    }

    bool GetBlsVerifyG2(
            const std::string& id,
            bls::protobuf::VerifyVecBrdReq* verfy_req) {
        std::string key = kBlsVerifyPrefex + id;
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!verfy_req->ParseFromString(val)) {
            assert(false);
            return false;
        }

        return true;
    }

    void SaveBlsPrikey(
            uint64_t elect_height,
            uint32_t sharding_id,
            const std::string& node_addr,
            const std::string& bls_prikey) {
        std::string key;
        key.reserve(48);
        key.append(kBlsPrivateKeyPrefix);
        key.append((char*)&elect_height, sizeof(elect_height));
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append(node_addr);
        auto st = db_->Put(key, bls_prikey);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
        
        ZJC_DEBUG("save bls success: %lu, %u, %s, bls_prikey: %s", elect_height,
            sharding_id,
            common::Encode::HexEncode(node_addr).c_str(),
            bls_prikey.c_str());
    }

    bool GetBlsPrikey(
            std::shared_ptr<security::Security>& security_ptr,
            uint64_t elect_height,
            uint32_t sharding_id,
            std::string* bls_prikey) {
        std::string key;
        key.reserve(48);
        key.append(kBlsPrivateKeyPrefix);
        key.append((char*)&elect_height, sizeof(elect_height));
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append(security_ptr->GetAddress());
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get bls failed: %lu, %u, %s",
                elect_height,
                sharding_id,
                common::Encode::HexEncode(security_ptr->GetAddress()).c_str());
            return false;
        }

        if (security_ptr->Decrypt(
                val,
                security_ptr->GetPrikey(),
                bls_prikey) != security::kSecuritySuccess) {
            return false;
        }

        ZJC_DEBUG("get bls success: %lu, %u, %s, bls_prikey: %s", elect_height,
            sharding_id,
            common::Encode::HexEncode(security_ptr->GetAddress()).c_str(),
            bls_prikey->c_str());
        return true;
    }

    void SaveBlsVerifyValue(
            const std::string& id,
            uint32_t idx,
            uint32_t valid_t,
            const libff::alt_bn128_G2& verfiy) {
        std::string key;
        key.reserve(128);
        key.append(kBlsVerifyValuePrefix);
        key.append(id);
        key.append((char*)&idx, sizeof(idx));
        key.append((char*)&valid_t, sizeof(valid_t));
        elect::protobuf::VerifyVecValue verfiy_value;
        verfiy_value.set_x_c0(libBLS::ThresholdUtils::fieldElementToString(verfiy.X.c0));
        verfiy_value.set_x_c1(libBLS::ThresholdUtils::fieldElementToString(verfiy.X.c1));
        verfiy_value.set_y_c0(libBLS::ThresholdUtils::fieldElementToString(verfiy.Y.c0));
        verfiy_value.set_y_c1(libBLS::ThresholdUtils::fieldElementToString(verfiy.Y.c1));
        verfiy_value.set_z_c0(libBLS::ThresholdUtils::fieldElementToString(verfiy.Z.c0));
        verfiy_value.set_z_c1(libBLS::ThresholdUtils::fieldElementToString(verfiy.Z.c1));
        verfiy_value.set_valid_t(valid_t);
        std::string val = verfiy_value.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    bool GetBlsVerifyValue(
            const std::string& id,
            uint32_t idx,
            uint32_t max_t,
            uint32_t* valid_t,
            libff::alt_bn128_G2* verfiy) {
        for (int32_t i = (int32_t)max_t; i > 0; --i) {
            std::string key;
            key.reserve(128);
            key.append(kBlsVerifyValuePrefix);
            key.append(id);
            key.append((char*)&idx, sizeof(idx));
            key.append((char*)&i, sizeof(i));
            if (!db_->Exist(key)) {
                continue;
            }

            std::string val;
            auto st = db_->Get(key, &val);
            if (!st.ok()) {
                ZJC_ERROR("write db failed!");
                return false;
            }

            elect::protobuf::VerifyVecValue item;
            if (!item.ParseFromString(val)) {
                return false;
            }

            auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
            auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
            auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
            auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
            auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
            auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
            auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
            auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
            auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
            *verfiy = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
            *valid_t = i;
            return true;
        }
        
        return false;
    }

    void SavePresetPolynomial(const bls::protobuf::LocalBlsItem& bls_polynomial) {
        std::string key = kPresetPolynomialPrefix;
        std::string val = bls_polynomial.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    bool GetPresetPolynomial(bls::protobuf::LocalBlsItem* bls_polynomial) {
        std::string key = kPresetPolynomialPrefix;
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_ERROR("write db failed!");
            return false;
        }

        if (!bls_polynomial->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SavePresetVerifyValue(
            uint32_t idx, uint32_t pos, const bls::protobuf::BlsVerifyValue& verify_val) {
        std::string key;
        key.reserve(64);
        key.append(kPresetVerifyValuePrefix);
        key.append((char*)&idx, sizeof(idx));
        key.append((char*)&pos, sizeof(pos));
        std::string val = verify_val.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    bool ExistsPresetVerifyValue(uint32_t idx, uint32_t pos) {
        std::string key;
        key.reserve(64);
        key.append(kPresetVerifyValuePrefix);
        key.append((char*)&idx, sizeof(idx));
        key.append((char*)&pos, sizeof(pos));
        return db_->Exist(key);
    }

    bool GetPresetVerifyValue(
            uint32_t idx,
            uint32_t pos,
            bls::protobuf::BlsVerifyValue* verify_val) {
        std::string key;
        key.reserve(64);
        key.append(kPresetVerifyValuePrefix);
        key.append((char*)&idx, sizeof(idx));
        key.append((char*)&pos, sizeof(pos));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_ERROR("write db failed!");
            return false;
        }

        if (!verify_val->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveConsensusedStatisticTimeBlockHeight(
            uint32_t sharding_id,
            uint64_t timeblock_height,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kConsensusedStatisticPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val((char*)&timeblock_height, sizeof(timeblock_height));
        db_batch.Put(key, val);
    }

    bool GetConsensusedStatisticTimeBlockHeight(uint32_t sharding_id, uint64_t* timeblock_height) {
        std::string key;
        key.reserve(64);
        key.append(kConsensusedStatisticPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_ERROR("write db failed!");
            return false;
        }

        if (val.size() != sizeof(*timeblock_height)) {
            return false;
        }

        uint64_t* tm_height = (uint64_t*)val.c_str();
        *timeblock_height = tm_height[0];
        return true;
    }

    void SaveStatisticedShardingHeight(
            uint32_t sharding_id,
            uint64_t tm_height,
            const pools::protobuf::ElectStatistic& elect_statistic,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kRootStatisticedPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&tm_height, sizeof(tm_height));
        std::string val = elect_statistic.SerializeAsString();
        db_batch.Put(key, val);
    }

    bool GetStatisticedShardingHeight(
            uint32_t sharding_id,
            uint64_t tm_height,
            pools::protobuf::ElectStatistic* elect_statistic) {
        std::string key;
        key.reserve(64);
        key.append(kRootStatisticedPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&tm_height, sizeof(tm_height));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_WARN("get data from db failed!");
            return false;
        }

        if (!elect_statistic->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    bool ExistsStatisticedShardingHeight(
            uint32_t sharding_id,
            uint64_t tm_height) {
        std::string key;
        key.reserve(64);
        key.append(kRootStatisticedPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&tm_height, sizeof(tm_height));
        return db_->Exist(key);
    }

    void SaveElectNodeStoke(
            const std::string& id,
            uint64_t elect_height,
            uint64_t balance,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kElectNodesStokePrefix);
        key.append(id);
        key.append((char*)&elect_height, sizeof(elect_height));
        char data[8];
        uint64_t* tmp = (uint64_t*)data;
        tmp[0] = balance;
        std::string val(data, sizeof(data));
        db_batch.Put(key, val);
    }

    void GetElectNodeMinStoke(uint32_t sharding_id, const std::string& id, uint64_t* stoke) {
        std::string key;
        key.reserve(64);
        key.append(kSaveLatestElectHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        auto st = db_->Get(key, &val);
        std::set<uint64_t> height_set;
        if (st.ok()) {
            uint64_t* tmp = (uint64_t*)val.c_str();
            uint32_t len = val.size() / 8;
            for (uint32_t i = 0; i < len; ++i) {
                height_set.insert(tmp[i]);
            }
        }

        for (auto hiter = height_set.begin(); hiter != height_set.end(); ++hiter) {
            std::string key;
            key.reserve(64);
            key.append(kElectNodesStokePrefix);
            key.append(id);
            uint64_t elect_height = *hiter;
            key.append((char*)&elect_height, sizeof(elect_height));
            std::string val;
            auto st = db_->Get(key, &val);
            if (!st.ok()) {
                continue;
            }

            uint64_t* balance = (uint64_t*)val.c_str();
            if (balance[0] < *stoke || *stoke == 0) {
                *stoke = balance[0];
            }
        }
    }

    void SaveAddressPubkey(const std::string& id, const std::string& pubkey) {
        std::string key;
        key.reserve(64);
        key.append(kAddressPubkeyPrefex);
        key.append(id);
        auto st = db_->Put(key, pubkey);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    bool GetAddressPubkey(const std::string& id, std::string* pubkey) {
        std::string key;
        key.reserve(64);
        key.append(kAddressPubkeyPrefex);
        key.append(id);
        auto st = db_->Get(key, pubkey);
        if (!st.ok()) {
            ZJC_ERROR("write db failed!");
            return false;
        }

        return true;
    }

    void SaveJoinShard(uint32_t sharding_id, uint32_t des_sharding_id) {
        std::string key;
        key.reserve(64);
        key.append(kSaveChoosedJoinShardPrefix);
        char data[8];
        uint32_t* tmp = (uint32_t*)data;
        tmp[0] = sharding_id;
        tmp[1] = des_sharding_id;
        std::string val(data, sizeof(data));
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    bool GetJoinShard(uint32_t* sharding_id, uint32_t* des_sharding_id) {
        std::string key;
        key.reserve(64);
        key.append(kSaveChoosedJoinShardPrefix);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        uint32_t* tmp = (uint32_t*)val.c_str();
        *sharding_id = tmp[0];
        *des_sharding_id = tmp[1];
        return true;
    }

    void SaveGenesisTimeblock(uint64_t block_height, uint64_t genesis_tm, db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kGenesisTimeblockPrefix);
        char data[8];
        uint32_t* tmp = (uint32_t*)data;
        tmp[0] = block_height;
        tmp[1] = genesis_tm;
        std::string val(data, sizeof(data));
        db_batch.Put(key, val);
    }

    bool GetGenesisTimeblock(uint64_t* block_height, uint64_t* genesis_tm) {
        std::string key;
        key.reserve(64);
        key.append(kGenesisTimeblockPrefix);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_ERROR("get db failed!");
            return false;
        }

        if (val.size() != 16) {
            return false;
        }

        uint32_t* tmp = (uint32_t*)val.c_str();
        *block_height = tmp[0];
        *genesis_tm = tmp[1];
        return true;
    }

    void SaveLocalPolynomial(
            std::shared_ptr<security::Security>& security_ptr,
            const std::string& id,
            const bls::protobuf::LocalPolynomial& local_poly,
            bool for_temp = false) {
        std::string key;
        key.reserve(128);
        if (for_temp) {
            key.append(kLocalTempPolynomialPrefix);
        } else {
            key.append(kLocalPolynomialPrefix);
        }        key.append(id);
        std::string tmp_val = local_poly.SerializeAsString();
        char tmp_data[4];
        uint32_t* tmp = (uint32_t*)tmp_data;
        tmp[0] = tmp_val.size();
        std::string val = std::string(tmp_data, sizeof(tmp_data)) + tmp_val;
        std::string enc_data;
        if (security_ptr->Encrypt(
                val,
                security_ptr->GetPrikey(),
                &enc_data) != security::kSecuritySuccess) {
            ZJC_FATAL("encrypt data failed!");
            return;
        }

        auto st = db_->Put(key, enc_data);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    void SaveLocalPolynomial(
            std::shared_ptr<security::Security>& security_ptr,
            const std::string& id,
            const bls::protobuf::LocalPolynomial& local_poly,
            bool for_temp,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        if (for_temp) {
            key.append(kLocalTempPolynomialPrefix);
        } else {
            key.append(kLocalPolynomialPrefix);
        }        key.append(id);
        std::string tmp_val = local_poly.SerializeAsString();
        char tmp_data[4];
        uint32_t* tmp = (uint32_t*)tmp_data;
        tmp[0] = tmp_val.size();
        std::string val = std::string(tmp_data, sizeof(tmp_data)) + tmp_val;
        std::string enc_data;
        if (security_ptr->Encrypt(
                val,
                security_ptr->GetPrikey(),
                &enc_data) != security::kSecuritySuccess) {
            ZJC_FATAL("encrypt data failed!");
            return;
        }

        db_batch.Put(key, enc_data);
    }

    bool GetLocalPolynomial(
            std::shared_ptr<security::Security>& security_ptr,
            const std::string& id,
            bls::protobuf::LocalPolynomial* local_poly,
            bool for_temp = false) {
        std::string key;
        key.reserve(128);
        if (for_temp) {
            key.append(kLocalTempPolynomialPrefix);
        } else {
            key.append(kLocalPolynomialPrefix);
        }
        key.append(id);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
//             assert(false);
            ZJC_ERROR("get db failed!");
            return false;
        }

        std::string dec_data;
        if (security_ptr->Decrypt(
                val,
                security_ptr->GetPrikey(),
                &dec_data) != security::kSecuritySuccess) {
            assert(false);
            ZJC_ERROR("decrypt db failed!");
            return false;
        }

        uint32_t* tmp = (uint32_t*)dec_data.c_str();
        if (!local_poly->ParseFromArray(dec_data.c_str() + 4, tmp[0])) {
            assert(false);
            ZJC_ERROR("parse db failed!");
            return false;
        }

        return true;
    }

    void SaveVerifiedG2s(
            uint32_t local_member_idx,
            const std::string& id,
            uint32_t valid_t,
            const bls::protobuf::JoinElectBlsInfo& verfy_final_vals,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kLocalVerifiedG2Prefix);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&valid_t, sizeof(valid_t));
        key.append(id);
        std::string val = verfy_final_vals.SerializeAsString();
        db_batch.Put(key, val);
    }

    void SaveVerifiedG2s(
            uint32_t local_member_idx,
            const std::string& id,
            uint32_t valid_t,
            const bls::protobuf::JoinElectBlsInfo& verfy_final_vals) {
        std::string key;
        key.reserve(128);
        key.append(kLocalVerifiedG2Prefix);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&valid_t, sizeof(valid_t));
        key.append(id);
        std::string val = verfy_final_vals.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    void DeleteVerifiedG2s(
            uint32_t local_member_idx,
            const std::string& id,
            uint32_t valid_t) {
        std::string key;
        key.reserve(128);
        key.append(kLocalVerifiedG2Prefix);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&valid_t, sizeof(valid_t));
        key.append(id);
        db_->Delete(key);
    }

    bool GetVerifiedG2s(
            uint32_t local_member_idx,
            const std::string& id,
            uint32_t valid_t,
            bls::protobuf::JoinElectBlsInfo* verfy_final_vals) {
        std::string key;
        key.reserve(128);
        key.append(kLocalVerifiedG2Prefix);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&valid_t, sizeof(valid_t));
        key.append(id);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get data from db failed local_member_idx: %u, valid_t: %u, id: %s",
                local_member_idx, valid_t, common::Encode::HexEncode(id).c_str());
            return false;
        }

        if (!verfy_final_vals->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveNodeVerificationVector(
            const std::string& addr,
            const bls::protobuf::JoinElectInfo& join_info,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kNodeVerificationVectorPrefix);
        key.append(addr);
        std::string val = join_info.SerializeAsString();
        db_batch.Put(key, val);
    }

    bool GetNodeVerificationVector(
            const std::string& addr,
            bls::protobuf::JoinElectInfo* join_info) {
        std::string key;
        key.reserve(128);
        key.append(kNodeVerificationVectorPrefix);
        key.append(addr);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_ERROR("get data from db failed!");
            return false;
        }

        if (!join_info->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveLocalElectPos(const std::string& addr, uint32_t pos, db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kNodeLocalElectPosPrefix);
        key.append(addr);
        char data[4];
        uint32_t* tmp = (uint32_t*)data;
        tmp[0] = pos;
        std::string val(data, sizeof(data));
        db_batch.Put(key, val);
    }

    void SaveLocalElectPos(const std::string& addr, uint32_t pos) {
        std::string key;
        key.reserve(128);
        key.append(kNodeLocalElectPosPrefix);
        key.append(addr);
        char data[4];
        uint32_t* tmp = (uint32_t*)data;
        tmp[0] = pos;
        std::string val(data, sizeof(data));
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    bool GetLocalElectPos(const std::string& addr, uint32_t* pos) {
        std::string key;
        key.reserve(128);
        key.append(kNodeLocalElectPosPrefix);
        key.append(addr);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_INFO("get db failed!");
            return false;
        }

        uint32_t* tmp = (uint32_t*)val.c_str();
        *pos = tmp[0];
        return true;
    }

    void SaveCheckCrossHeight(
            uint32_t local_shard,
            uint32_t des_shard,
            uint64_t height,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kCrossCheckHeightPrefix);
        key.append((char*)&local_shard, sizeof(local_shard));
        key.append((char*)&des_shard, sizeof(des_shard));
        char data[8];
        uint64_t* tmp = (uint64_t*)data;
        tmp[0] = height;
        std::string val(data, sizeof(data));
        db_batch.Put(key, val);
    }

    bool GetCheckCrossHeight(
            uint32_t local_shard,
            uint32_t des_shard,
            uint64_t* height) {
        std::string key;
        key.reserve(128);
        key.append(kCrossCheckHeightPrefix);
        key.append((char*)&local_shard, sizeof(local_shard));
        key.append((char*)&des_shard, sizeof(des_shard));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_INFO("get db failed!");
            return false;
        }

        uint64_t* tmp = (uint64_t*)val.c_str();
        *height = tmp[0];
        return true;
    }

    void SaveElectHeightCommonPk(
            uint32_t des_shard,
            uint64_t height,
            const elect::protobuf::PrevMembers& prv_info,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kElectHeightWithBlsCommonPkPrefix);
        key.append((char*)&des_shard, sizeof(des_shard));
        key.append((char*)&height, sizeof(height));
        std::string val = prv_info.SerializeAsString();
        db_batch.Put(key, val);
        ZJC_DEBUG("save elect height prev info success: %u, %lu", des_shard, height);
        assert(prv_info.has_common_pubkey());
        assert(!prv_info.common_pubkey().x_c0().empty());
    }

    bool GetElectHeightCommonPk(
            uint32_t des_shard,
            uint64_t height,
            elect::protobuf::PrevMembers* prv_info) {
        std::string key;
        key.reserve(128);
        key.append(kElectHeightWithBlsCommonPkPrefix);
        key.append((char*)&des_shard, sizeof(des_shard));
        key.append((char*)&height, sizeof(height));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get elect height prev info failed: %u, %lu", des_shard, height);
            return false;
        }

        if (!prv_info->ParseFromString(val)) {
            ZJC_DEBUG("get elect height prev info failed: %u, %lu", des_shard, height);
            return false;
        }

        ZJC_DEBUG("get elect height prev info success: %u, %lu", des_shard, height);
        assert(prv_info->has_common_pubkey());
        assert(!prv_info->common_pubkey().x_c0().empty());
        return true;
    }

    void SaveHeightInvalidHashs(
            uint32_t shard_id,
            uint32_t pool_index,
            uint64_t height,
            const std::set<std::string>& hashs) {
        std::string key;
        key.reserve(128);
        key.append(kBftInvalidHeightHashs);
        key.append((char*)&shard_id, sizeof(shard_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        std::string val;
        for (auto iter = hashs.begin(); iter != hashs.end(); ++iter) {
            val += *iter;
        }

        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }

        ZJC_DEBUG("save height invalid hashs success: %u, %u, %lu, %s",
            shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
    }

    void SaveHeightInvalidHashs(
            uint32_t shard_id,
            uint32_t pool_index,
            uint64_t height,
            const std::set<std::string>& hashs,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kBftInvalidHeightHashs);
        key.append((char*)&shard_id, sizeof(shard_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        std::string val;
        for (auto iter = hashs.begin(); iter != hashs.end(); ++iter) {
            val += *iter;
        }

        db_batch.Put(key, val);
        ZJC_DEBUG("save height invalid hashs success: %u, %u, %lu, %s",
            shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
    }

    bool GetHeightInvalidHashs(
            uint32_t shard_id,
            uint32_t pool_index,
            uint64_t height,
            std::set<std::string>* hashs) {
        std::string key;
        key.reserve(128);
        key.append(kBftInvalidHeightHashs);
        key.append((char*)&shard_id, sizeof(shard_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get height invalid hashs failed: %u, %u, %lu, %s",
                shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
            return false;
        }

        auto count = val.size() / 32;
        for (uint32_t i = 0; i < count; ++i) {
            std::string tmp_hash(val.c_str() + i * 32, 32);
            hashs->insert(tmp_hash);
        }

        ZJC_DEBUG("get height invalid hashs success: %u, %u, %lu, %s",
            shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
        return true;
    }

    void SaveTempHeightInvalidHashs(
            uint32_t shard_id,
            uint32_t pool_index,
            uint64_t height,
            const std::set<std::string>& hashs) {
        std::string key;
        key.reserve(128);
        key.append(kTempBftInvalidHeightHashs);
        key.append((char*)&shard_id, sizeof(shard_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        std::string val;
        for (auto iter = hashs.begin(); iter != hashs.end(); ++iter) {
            val += *iter;
        }

        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }

        ZJC_DEBUG("save height invalid hashs success: %u, %u, %lu, %s",
            shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
    }

    bool GetTempHeightInvalidHashs(
            uint32_t shard_id,
            uint32_t pool_index,
            uint64_t height,
            std::set<std::string>* hashs) {
        std::string key;
        key.reserve(128);
        key.append(kTempBftInvalidHeightHashs);
        key.append((char*)&shard_id, sizeof(shard_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get height invalid hashs failed: %u, %u, %lu, %s",
                shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
            return false;
        }

        auto count = val.size() / 32;
        for (uint32_t i = 0; i < count; ++i) {
            std::string tmp_hash(val.c_str() + i * 32, 32);
            hashs->insert(tmp_hash);
        }

        ZJC_DEBUG("get height invalid hashs success: %u, %u, %lu, %s",
            shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
        return true;
    }

    bool SaveIdBandwidth(
            const std::string& id,
            uint64_t bw,
            uint64_t* all_bw) {
        std::string key;
        key.reserve(128);
        key.append(kBandwidthPrefix);
        key.append(id);
        ws::protobuf::BandwidthItem item;
        uint64_t day = common::TimeUtils::TimestampDays();
        std::string val;
        auto st = db_->Get(key, &val);
        if (st.ok()) {
            if (item.ParseFromString(val)) {
                if (item.timestamp() == day) {
                    bw += item.bandwidth();
                }
            }
        }

        *all_bw = bw;
        item.set_bandwidth(*all_bw);
        item.set_timestamp(day);
        st = db_->Put(key, item.SerializeAsString());
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }

        return true;
    }

    void SaveSellout(
            const std::string& id,
            const ws::protobuf::SellInfo& sell_info) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSelloutPrefix);
        key.append(id);
        auto st = db_->Put(key, sell_info.SerializeAsString());
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    void GetAllSellout(std::vector<std::shared_ptr<ws::protobuf::SellInfo>>* sells) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSelloutPrefix);
        std::map<std::string, std::string> sell_map;
        db_->GetAllPrefix(key, sell_map);
        for (auto iter = sell_map.begin(); iter != sell_map.end(); ++iter) {
            auto sell_ptr = std::make_shared<ws::protobuf::SellInfo>();
            auto& sell_info = *sell_ptr;
            if (!sell_info.ParseFromString(iter->second)) {
                continue;
            }

            sells->push_back(sell_ptr);
        }
    }

    bool GetSellout(
            const std::string& id,
            ws::protobuf::SellInfo* sell_info) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSelloutPrefix);
        key.append(id);
        std::string val;
        auto pst = db_->Get(key, &val);
        if (!pst.ok()) {
            return false;
        }

        return sell_info->ParseFromString(val);
    }

    void SaveSellOrder(
            const std::string& id,
            const ws::protobuf::SellInfo& sell_info) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSellorderPrefix);
        key.append(id);
        auto st = db_->Put(key, sell_info.SerializeAsString());
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    void GetAllOrder(std::vector<std::shared_ptr<ws::protobuf::SellInfo>>* orders) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSellorderPrefix);
        std::map<std::string, std::string> sell_map;
        db_->GetAllPrefix(key, sell_map);
        for (auto iter = sell_map.begin(); iter != sell_map.end(); ++iter) {
            auto sell_ptr = std::make_shared<ws::protobuf::SellInfo>();
            auto& sell_info = *sell_ptr;
            if (!sell_info.ParseFromString(iter->second)) {
                continue;
            }

            orders->push_back(sell_ptr);
        }
    }

    bool GetOrder(
            const std::string& id,
            ws::protobuf::SellInfo* sell_info) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSellorderPrefix);
        key.append(id);
        std::string val;
        auto pst = db_->Get(key, &val);
        if (!pst.ok()) {
            return false;
        }

        return sell_info->ParseFromString(val);
    }

    void SaveLatestToBlock(
            const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block, 
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kLatestToTxBlock);
        std::string value;
        value.resize(24);
        uint64_t* val_data = (uint64_t*)value.data();
        memset(val_data, 0, 24);
        val_data[0] = view_block->qc().network_id();
        val_data[1] = view_block->qc().pool_index();
        val_data[2] = view_block->block_info().height();
        db_batch.Put(key, value);
        ZJC_DEBUG("success save latest to block: %u_%u_%lu",
            view_block->qc().network_id(), 
            view_block->qc().pool_index(), 
            view_block->block_info().height());
    }

    bool GetLatestToBlock(view_block::protobuf::ViewBlockItem* block) {
        std::string key;
        key.reserve(64);
        key.append(kLatestToTxBlock);
        std::string value;
        auto st = db_->Get(key, &value);
        if (!st.ok()) {
            return false;
        }

        uint64_t* val_data = (uint64_t*)value.data();
        ZJC_DEBUG("success get latest to block: %u_%u_%lu", static_cast<uint32_t>(val_data[0]), 
            static_cast<uint32_t>(val_data[1]), 
            val_data[2]);
        return GetBlockWithHeight(
            static_cast<uint32_t>(val_data[0]), 
            static_cast<uint32_t>(val_data[1]), 
            val_data[2],
            block);
    }

    void SaveLatestPoolStatisticTag(
            uint64_t network_id, 
            const pools::protobuf::PoolStatisticTxInfo& statistic_info) {
        std::string key;
        key.reserve(64);
        key.append(kLatestPoolStatisticTagPrefix);
        key.append((char*)&network_id, sizeof(network_id));
        auto st = db_->Put(key, statistic_info.SerializeAsString());
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
        
        ZJC_INFO("success SaveLatestPoolStatisticTag network: %u, message: %s",
            network_id, ProtobufToJson(statistic_info).c_str());
    }

    void SaveLatestPoolStatisticTag(
            uint64_t network_id, 
            const pools::protobuf::PoolStatisticTxInfo& statistic_info, 
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kLatestPoolStatisticTagPrefix);
        key.append((char*)&network_id, sizeof(network_id));
        db_batch.Put(key, statistic_info.SerializeAsString());
        ZJC_INFO("success SaveLatestPoolStatisticTag network: %u, message: %s",
            network_id, ProtobufToJson(statistic_info).c_str());
    }

    bool GetLatestPoolStatisticTag(
            uint64_t network_id, 
            pools::protobuf::PoolStatisticTxInfo* statistic_info) {
        std::string key;
        key.reserve(64);
        key.append(kLatestPoolStatisticTagPrefix);
        key.append((char*)&network_id, sizeof(network_id));
        std::string value;
        auto st = db_->Get(key, &value);
        if (!st.ok()) {
            return false;
        }

        if (!statistic_info->ParseFromString(value)) {
            return false;
        }

        ZJC_INFO("success GetLatestPoolStatisticTag network: %u, message: %s",
            network_id, ProtobufToJson(*statistic_info).c_str());
        return true;
    }

    void SaveLatestLeaderProposeMessage(const transport::protobuf::Header& header) {
        std::string key;
        key.append(kLatestLeaderProposeMessage);
        uint32_t network_id = header.hotstuff().pro_msg().view_item().qc().network_id();
        key.append((char*)&network_id, sizeof(network_id));
        uint32_t pool_idx = header.hotstuff().pro_msg().view_item().qc().pool_index();
        key.append((char*)&pool_idx, sizeof(pool_idx));
        auto st = db_->Put(key, header.SerializeAsString());
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    bool GetLatestLeaderProposeMessage(
            uint32_t network_id, 
            uint32_t pool_idx, 
            transport::protobuf::Header* header) {
        std::string key;
        key.append(kLatestLeaderProposeMessage);
        key.append((char*)&network_id, sizeof(network_id));
        key.append((char*)&pool_idx, sizeof(pool_idx));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!header->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    // 用于保存 agg bls 的私钥，目前私钥与 elect_height 无关
    void SaveAggBlsPrikey(
            std::shared_ptr<security::Security>& security_ptr,
            const libff::alt_bn128_Fr& bls_prikey) {
        std::string key;
        key.reserve(32);
        key.append(kAggBlsPrivateKeyPrefix);
        key.append(security_ptr->GetAddress());

        std::string enc_data;
        std::string sec_key = libBLS::ThresholdUtils::fieldElementToString(bls_prikey);
        if (security_ptr->Encrypt(
                    sec_key,
                    security_ptr->GetPrikey(),
                    &enc_data) != security::kSecuritySuccess) {
            return;
        }        
        
        auto st = db_->Put(key, enc_data);
        if (!st.ok()) {
            ZJC_FATAL("write block to db failed: %d, status: %s", 1, st.ToString());
        }
    }

    bool GetAggBlsPrikey(
            std::shared_ptr<security::Security>& security_ptr,
            libff::alt_bn128_Fr* bls_prikey) {
        std::string key;
        key.reserve(32);
        key.append(kAggBlsPrivateKeyPrefix);
        key.append(security_ptr->GetAddress());
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get agg bls failed: %s",
                common::Encode::HexEncode(security_ptr->GetAddress()).c_str());
            return false;
        }

        std::string prikey_str;
        if (security_ptr->Decrypt(
                val,
                security_ptr->GetPrikey(),
                &prikey_str) != security::kSecuritySuccess) {
            return false;
        }

        ZJC_DEBUG("save agg bls success: %s",
            common::Encode::HexEncode(security_ptr->GetAddress()).c_str());

        *bls_prikey = libff::alt_bn128_Fr(prikey_str.c_str());
        return true;
    }

    bool ParentHashExists(const std::string& hash) {
        std::string key;
        key.reserve(48);
        key.append(kViewBlockVaildParentHash);
        key.append(hash);
        return db_->Exist(key);
    }

    void SaveValidViewBlockParentHash(
            const std::string& parent_hash, 
            uint32_t network_id, 
            uint32_t pool_index, 
            uint64_t view,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(48);
        key.append(kViewBlockVaildParentHash);
        key.append(parent_hash);
        char value[16] = {0};
        uint32_t *u32_arr = (uint32_t*)value;
        u32_arr[0] = network_id;
        u32_arr[1] = pool_index;
        uint64_t* u64_arr = (uint64_t*)(value + 8);
        u64_arr[0] = view;
        db_batch.Put(key, std::string(value, sizeof(value)));
    }

private:
    static const uint32_t kSaveElectHeightCount = 4u;

    std::shared_ptr<db::Db> db_ = nullptr;
    uint64_t prev_gid_tm_us_ = 0;
    common::Tick db_batch_tick_;
    volatile bool dumped_gid_ = false;

    DISALLOW_COPY_AND_ASSIGN(PrefixDb);
};

};  // namespace protos

};  // namespace shardora
