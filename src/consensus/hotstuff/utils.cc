#include "consensus/hotstuff/utils.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <consensus/hotstuff/types.h>
#include "consensus/hotstuff/view_block_chain.h"
#include "pools/tx_utils.h"
#include "zjcvm/zjc_host.h"

namespace shardora {

namespace hotstuff {

std::string GetBlockHash(const view_block::protobuf::ViewBlockItem &view_block) {
    std::string serialized;
    google::protobuf::io::StringOutputStream string_stream(&serialized);
    google::protobuf::io::CodedOutputStream coded_output(&string_stream);
    coded_output.SetSerializationDeterministic(true); 
    auto& block = view_block.block_info();
    if (!block.SerializePartialToCodedStream(&coded_output)) {
        return "";
    }

    coded_output.Trim();
    uint32_t sharding_id = view_block.qc().network_id();
    serialized.append((char*)&sharding_id, sizeof(sharding_id));
    uint32_t pool_index = view_block.qc().pool_index();
    serialized.append((char*)&pool_index, sizeof(pool_index));
    serialized.append(view_block.parent_hash());
    auto hash = common::Hash::keccak256(serialized);

    SHARDORA_DEBUG("get block hash: %s, sharding_id: %u, pool_index: %u, "
        "phash: %s, vss_random: %lu, height: %lu, "
        "timeblock_height: %lu, timestamp: %lu, msg: %s",
        common::Encode::HexEncode(hash).c_str(),
        view_block.qc().network_id(), 
        view_block.qc().pool_index(), 
        common::Encode::HexEncode(view_block.parent_hash()).c_str(), 
        block.consistency_random(), 
        block.height(), 
        block.timeblock_height(), 
        block.timestamp(),
        ProtobufToJson(block).c_str());

    return hash;
}

int CheckTransactionValid(
        const std::string& parent_hash, 
        std::shared_ptr<ViewBlockChain> view_block_chain,
        const address::protobuf::AddressInfo& addr_info, 
        pools::protobuf::TxMessage& tx_info,
        uint64_t* now_nonce) {
    if (pools::IsUserTransaction(tx_info.step())) {
        return view_block_chain->CheckTxNonceValid(
            addr_info.addr(), 
            tx_info.nonce(), 
            parent_hash,
            now_nonce);
    }
    
    zjcvm::ZjchainHost zjc_host;
    zjc_host.parent_hash_ = parent_hash;
    zjc_host.view_block_chain_ = view_block_chain;
    std::string val;
    if (zjc_host.GetKeyValue(tx_info.to(), tx_info.key(), &val) == zjcvm::kZjcvmSuccess) {
        SHARDORA_DEBUG("not user tx unique hash exists to: %s, unique hash: %s, step: %d",
            common::Encode::HexEncode(tx_info.to()).c_str(),
            common::Encode::HexEncode(tx_info.key()).c_str(),
            (int32_t)tx_info.step());
        return 1;
    }

    SHARDORA_DEBUG("not user tx unique hash success to: %s, unique hash: %s",
        common::Encode::HexEncode(tx_info.to()).c_str(),
        common::Encode::HexEncode(tx_info.key()).c_str());
    return 0;
}

bool BlockViewCommited(
        std::shared_ptr<protos::PrefixDb> prefix_db, 
        uint32_t network_id, View view) {
    if (commited_view_.find(view) != commited_view_.end()) {
        return true;
    }

    if (prefix_db->ViewBlockIsValidView(network_id, pool_index_, view)) {
        return true;
    }

    return false;
}


bool ViewBlockIsCheckedParentHash(
        std::shared_ptr<protos::PrefixDb> prefix_db, 
        const std::string& hash) {
    return prefix_db->ParentHashExists(hash);
}

} // namespace consensus

} // namespace shardora
