#include <_types/_uint64_t.h>
#include <iostream>
#include <queue>
#include <vector>

#include "init/http_handler.h"
#include <functional>

#include "common/encode.h"
#include "common/string_utils.h"
#include "dht/dht_key.h"
#include "network/route.h"
#include "pools/tx_utils.h"
#include "protos/transport.pb.h"
#include "transport/transport_utils.h"


using namespace zjchain;
    
int main(int argc, char** argv) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;

    init::HttpHandler* http_handler;
    std::string gid = "8476fcc1b0077380fe894cecb072a24f914b053bb10c94948eea5b399d061249";
    std::string from_pk = "042702ef617e594b27fac7cba1953470ee2f9774cfb73b44a43d866b36c61900351e5b41f1143be2031a6b632c34cbe37ac4e8a40c925395e44c40b99e2f6e85fc";
    std::string to = "d9ec5aff3001dece14e1f4a35a39ed506bd6274a";
    uint64_t amount = 10000000000000;
    uint64_t gas_limit = 100000;
    uint64_t gas_price = 1;
    uint64_t sign_v = 0;
    std::string sign_r = "74dc67830f7687c7845accba8de82929fcdd3c47b77bf504a81ab50acd5ffaa0";
    std::string sign_s = "4b4b589ed78b9a3ee081fd83784d398cd1329506f91dd7aca8fe98e41c2e4cf3";
    int32_t des_net_id = 3;
    
    auto from = http_handler->security_ptr()->GetAddress(from_pk);
    
    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    msg.set_hop_count(0);
    auto broadcast = msg.mutable_broadcast();
    broadcast->set_hop_limit(10);
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_gid(gid);
    new_tx->set_pubkey(from_pk);

    uint32_t step_val = 0;

    new_tx->set_step(static_cast<pools::protobuf::StepType>(step_val));
    new_tx->set_to(to);
    new_tx->set_amount(amount);
    new_tx->set_gas_limit(gas_limit);
    new_tx->set_gas_price(gas_price);
    

    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign = sign_r + sign_s + "0";// http_handler->security_ptr()->GetSign(sign_r, sign_s, sign_v);
    sign[64] = char(sign_v);
    if (http_handler->security_ptr()->Verify(
            tx_hash, from_pk, sign) != security::kSecuritySuccess) {

        return -1;
    }

    msg.set_sign(sign);

    http_handler->net_handler()->NewHttpServer(msg_ptr);
    
    return 0;
}
