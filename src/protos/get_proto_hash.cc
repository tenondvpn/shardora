#include "protos/get_proto_hash.h"

#include "common/hash.h"
#include "common/unique_map.h"
#include "protos/address.pb.h"

namespace zjchain {

namespace protos {


static void GetTxProtoHash(
        const pools::protobuf::TxMessage& pools_msg,
        std::string* msg) {
    std::string& msg_for_hash = *msg;
    msg_for_hash.append(pools_msg.gid());
    msg_for_hash.append(pools_msg.pubkey());
    uint64_t gas_limit = pools_msg.gas_limit();
    msg_for_hash.append(std::string((char*)&gas_limit, sizeof(gas_limit)));
    uint64_t gas_price = pools_msg.gas_price();
    msg_for_hash.append(std::string((char*)&gas_price, sizeof(gas_price)));
    auto type = pools_msg.step();
    msg_for_hash.append(std::string((char*)&type, sizeof(type)));
    msg_for_hash.append(pools_msg.key());
    msg_for_hash.append(pools_msg.value());
    msg_for_hash.append(pools_msg.to());
    uint64_t amount = pools_msg.amount();
    msg_for_hash.append(std::string((char*)&amount, sizeof(amount)));
}

void GetProtoHash(const transport::protobuf::Header& msg, std::string* msg_for_hash) {
    if (msg.has_tx_proto()) {
        GetTxProtoHash(msg.tx_proto(), msg_for_hash);
    }
}

};  // namespace protos

};  // namespace zjchain