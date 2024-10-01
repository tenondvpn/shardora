
#include <iomanip>

// #include "protos/block.pb.h"

#include "elect/elect_utils.h"
#include "protos/block.pb.h"
#include "protos/tx_storage_key.h"

namespace shardora {
namespace elect {

class ElectPlege {
public:
    static std::string gen_elect_plege_contract_addr(uint64_t shard_id);
    uint64_t getStoke(uint32_t shard_id, std::string addr, uint64_t elect_height);
    static int gen_elect_plege_GensisBlocks(uint64_t shard_id, block::protobuf::BlockTx *tx_info);

private:
    static std::string toHexString(uint64_t shard_id, size_t width);
};


} // namespace elect
} // namespace shardora