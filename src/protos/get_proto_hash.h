#pragma once

#include "protos/transport.pb.h"

namespace zjchain {

namespace protos {

void GetProtoHash(const transport::protobuf::Header& msg, std::string* msg_for_hash);
std::string GetElectBlockHash(const elect::protobuf::ElectBlock& msg);

};  // namespace protos

};  // namespace zjchain