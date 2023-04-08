#include "elect/nodes_stoke_manager.h"

#include "common/global_info.h"
#include "dht/base_dht.h"
#include "elect/elect_proto.h"
#include "network/dht_manager.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "transport/multi_thread.h"

namespace zjchain {

namespace elect {

void NodesStokeManager::SyncAddressStoke(const std::vector<std::string>& addrs) {
    // std::map<uint32_t, std::vector<std::pair<std::string, uint64_t>>> sync_map;
    // for (auto iter = addrs.begin(); iter != addrs.end(); ++iter) {
        // auto acc_info = block::AccountManager::Instance()->GetAccountInfo(*iter);
        // if (acc_info == nullptr) {
        //     continue;
        // }

        // uint32_t netid = common::kInvalidUint32;
        // if (acc_info->GetConsensuseNetId(&netid) != block::kBlockSuccess) {
        //     continue;
        // }

    //     uint64_t synced_tm_height = 0;
    //     {
    //         std::lock_guard<std::mutex> g(sync_nodes_map_mutex_);
    //         auto synced_iter = sync_nodes_map_.find(*iter);
    //         if (synced_iter != sync_nodes_map_.end()) {
    //             // if (synced_iter->second.first ==
    //             //         tmblock::TimeBlockManager::Instance()->LatestTimestampHeight()) {
    //             //     continue;
    //             // }

    //             synced_tm_height = synced_iter->second.first;
    //         } else {
    //             sync_nodes_map_[*iter] = std::make_pair(0, 0);
    //         }
    //     }

    //     auto siter = sync_map.find(netid);
    //     if (siter != sync_map.end()) {
    //         siter->second.push_back(std::make_pair(*iter, synced_tm_height));
    //     } else {
    //         sync_map[netid] = std::vector<std::pair<std::string, uint64_t>>{
    //             std::make_pair(*iter, synced_tm_height) };
    //     }

    //     ELECT_DEBUG("TTTTTTTTT add sync addr: %s, height: %lu, now height: %lu, netid: %u",
    //         common::Encode::HexEncode(*iter).c_str(),
    //         synced_tm_height,
    //         tmblock::TimeBlockManager::Instance()->LatestTimestampHeight(),
    //         netid);
    // }

    // auto dht = network::DhtManager::Instance()->GetDht(
    //     common::GlobalInfo::Instance()->network_id());
    // if (!dht) {
    //     return;
    // }

    // for (auto iter = sync_map.begin(); iter != sync_map.end(); ++iter) {
    //     transport::protobuf::Header msg;
        // auto res = elect::ElectProto::CreateSyncStokeRequest(
        //     security_ptr_,
        //     dht->local_node(),
        //     iter->first,
        //     iter->second,
        //     msg);
        // if (res) {
        //     network::Route::Instance()->Send(nullptr);
        // }
    // }
}

uint64_t NodesStokeManager::GetAddressStoke(const std::string& addr) {
    // std::lock_guard<std::mutex> g(sync_nodes_map_mutex_);
    // auto iter = sync_nodes_map_.find(addr);
    // if (iter != sync_nodes_map_.end()) {
    //     return iter->second.second;
    // }

    return 0;
}

void NodesStokeManager::HandleSyncAddressStoke(
        const transport::protobuf::Header& header,
        const protobuf::ElectMessage& ec_msg) {
    // transport::protobuf::Header msg;
    // msg.set_src_sharding_id(header.des_dht_key());
    // msg.set_des_dht_key(header.src_dht_key());
    // msg.set_type(common::kElectMessage);
    // protobuf::ElectMessage res_ec_msg;
    // auto sync_stoke_res = res_ec_msg.mutable_sync_stoke_res();
    // for (int32_t i = 0; i < ec_msg.sync_stoke_req().sync_item_size(); ++i) {
        // auto acc_info = block::AccountManager::Instance()->GetAccountInfo(
        //     ec_msg.sync_stoke_req().sync_item(i).id());
        // if (acc_info == nullptr) {
        //     ELECT_DEBUG("TTTTTTT get account info failed: %s",
        //         common::Encode::HexEncode(ec_msg.sync_stoke_req().sync_item(i).id()).c_str());
        //     continue;
        // }

        // std::string block_str;
        // acc_info->GetAccountTmHeightBlock(
        //     ec_msg.sync_stoke_req().now_tm_height(),
        //     ec_msg.sync_stoke_req().sync_item(i).synced_tm_height(),
        //     &block_str);
        // if (!block_str.empty()) {
        //     auto block_item = std::make_shared<block::protobuf::Block>();
        //     if (!block_item->ParseFromString(block_str)) {
        //         ELECT_DEBUG("TTTTTTT get account info failed: %s",
        //             common::Encode::HexEncode(ec_msg.sync_stoke_req().sync_item(i).id()).c_str());
        //         continue;
        //     }

        //     auto& tx_list = block_item->tx_list();
        //     for (int32_t tx_idx = 0; tx_idx < tx_list.size(); ++tx_idx) {
        //         std::lock_guard<std::mutex> g(sync_nodes_map_mutex_);
        //         std::string addr;
        //         if (tx_list[tx_idx].to_add()) {
        //             if (ec_msg.sync_stoke_req().sync_item(i).id() != tx_list[tx_idx].to()) {
        //                 continue;
        //             }

        //             addr = tx_list[tx_idx].to();
        //         } else {
        //             if (ec_msg.sync_stoke_req().sync_item(i).id() != tx_list[tx_idx].from()) {
        //                 continue;
        //             }

        //             addr = tx_list[tx_idx].from();
        //         }

        //         auto res_item = sync_stoke_res->add_items();
        //         res_item->set_id(addr);
        //         res_item->set_balance(tx_list[tx_idx].balance());
        //         ELECT_DEBUG("TTTTTTTTT response sync addr: %s, height: %lu, balance: %lu",
        //             common::Encode::HexEncode(addr).c_str(),
        //             ec_msg.sync_stoke_req().sync_item(i).synced_tm_height(),
        //             tx_list[tx_idx].balance());
        //     }
        // }
    // }

    // sync_stoke_res->set_now_tm_height(ec_msg.sync_stoke_req().now_tm_height());
    // msg.set_data(res_ec_msg.SerializeAsString());
    // transport::MultiThreadHandler::Instance()->tcp_transport()->Send(
    //     header.from_ip(), header.from_port(), 0, msg);
    // ELECT_DEBUG("TTTTTTT send back ip: %s, port: %d", header.from_ip().c_str(), header.from_port());
}

void NodesStokeManager::HandleSyncStokeResponse(
        const transport::protobuf::Header& header,
        const protobuf::ElectMessage& ec_msg) {
    // if (common::GlobalInfo::Instance()->network_id() != network::kRootCongressNetworkId) {
    //     return;
    // }

    // ELECT_DEBUG("TTTTTTTTT get HandleSyncStokeResponse.");
    // for (int32_t i = 0; i < ec_msg.sync_stoke_res().items_size(); ++i) {
    //     std::lock_guard<std::mutex> g(sync_nodes_map_mutex_);
    //     ELECT_DEBUG("TTTTTTTTT get HandleSyncStokeResponse: %s", common::Encode::HexEncode(ec_msg.sync_stoke_res().items(i).id()).c_str());
    //     auto iter = sync_nodes_map_.find(ec_msg.sync_stoke_res().items(i).id());
    //     if (iter == sync_nodes_map_.end()) {
    //         continue;
    //     }

    //     sync_nodes_map_[ec_msg.sync_stoke_res().items(i).id()] = std::make_pair(
    //         ec_msg.sync_stoke_res().now_tm_height(),
    //         ec_msg.sync_stoke_res().items(i).balance());
    //     ELECT_DEBUG("TTTTTTTTT get response sync addr: %s, height: %lu, balance: %lu",
    //         common::Encode::HexEncode(ec_msg.sync_stoke_res().items(i).id()).c_str(),
    //         ec_msg.sync_stoke_res().now_tm_height(),
    //         ec_msg.sync_stoke_res().items(i).balance());
    // }
}

}  // namespace elect

}  // namespace zjchain
