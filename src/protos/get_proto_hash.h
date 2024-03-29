#pragma once

#include "protos/transport.pb.h"

namespace shardora {

namespace protos {

void GetProtoHash(const transport::protobuf::Header& msg, std::string* msg_for_hash);
std::string GetElectBlockHash(const elect::protobuf::ElectBlock& msg);
std::string GetJoinElectReqHash(const bls::protobuf::JoinElectInfo& join_req);

};  // namespace protos

};  // namespace shardora