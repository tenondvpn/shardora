// #include <stdlib.h>
// #include <math.h>
// 
// #include <iostream>
// 
// #include <gtest/gtest.h>
// 
// #include "bzlib.h"
// 
// #define private public
// #include "block/shard_statistic.h"
// #include "common/random.h"
// #include "common/time_utils.h"
// #include "db/db.h"
// #include "elect/elect_pool_manager.h"
// #include "elect/elect_manager.h"
// #include "elect/elect_manager.h"
// #include "network/network_utils.h"
// #include "security/secp256k1.h"
// #include "security/crypto_utils.h"
// #include "security/security.h"
// 
// namespace zjchain {
// 
// namespace elect {
// 
// namespace test {
// 
// static const char* kRootNodeIdEndFix = "2f72f72efffee770264ec22dc21c9d2bab63aec39941aad09acda57b485";
// static const char* kWaitingNodeIdEndFix = "1f72f72efffee770264ec22dc21c9d2bab63aec39941aad09acda57b4851";
// 
// class TestElectPoolManager : public testing::Test {
// public:
//     static void WriteDefaultLogConf(
//         const std::string& log_conf_path,
//         const std::string& log_path) {
//         FILE* file = NULL;
//         file = fopen(log_conf_path.c_str(), "w");
//         if (file == NULL) {
//             return;
//         }
//         std::string log_str = ("# log4cpp.properties\n"
//             "log4cpp.rootCategory = WARN\n"
//             "log4cpp.category.sub1 = WARN, programLog\n"
//             "log4cpp.appender.rootAppender = ConsoleAppender\n"
//             "log4cpp.appender.rootAppender.layout = PatternLayout\n"
//             "log4cpp.appender.rootAppender.layout.ConversionPattern = %d [%p] %m%n\n"
//             "log4cpp.appender.programLog = RollingFileAppender\n"
//             "log4cpp.appender.programLog.fileName = ") + log_path + "\n" +
//             std::string("log4cpp.appender.programLog.maxFileSize = 1073741824\n"
//                 "log4cpp.appender.programLog.maxBackupIndex = 1\n"
//                 "log4cpp.appender.programLog.layout = PatternLayout\n"
//                 "log4cpp.appender.programLog.layout.ConversionPattern = %d [%p] %m%n\n");
//         fwrite(log_str.c_str(), log_str.size(), 1, file);
//         fclose(file);
//         db::Db::Instance()->Init("./db");
//     }
// 
//     static void SetUpTestCase() {    
//         common::global_stop = true;
//         std::string config_path_ = "./";
//         std::string conf_path = config_path_ + "/tenon.conf";
//         std::string log_conf_path = config_path_ + "/log4cpp.properties";
//         std::string log_path = config_path_ + "/tenon.log";
//         WriteDefaultLogConf(log_conf_path, log_path);
//         log4cpp::PropertyConfigurator::configure(log_conf_path);
//     }
// 
//     static void TearDownTestCase() {
//     }
// 
//     virtual void SetUp() {
//     }
// 
//     virtual void TearDown() {
//     }
// 
//     std::string GetIdByPrikey(const std::string& private_key) {
//         security::PrivateKey prikey(private_key);
//         security::PublicKey pubkey(prikey);
//         std::string pubkey_str;
//         EXPECT_EQ(pubkey.Serialize(pubkey_str, false), security::kPublicKeyUncompressSize);
//         std::string id = security::Secp256k1::Instance()->ToAddressWithPublicKey(pubkey_str);
//         return id;
//     }
// 
//     std::string GetPubkeyByPrikey(const std::string& private_key, bool compress = true) {
//         security::PrivateKey prikey(private_key);
//         security::PublicKey pubkey(prikey);
//         std::string pubkey_str;
//         EXPECT_EQ(pubkey.Serialize(pubkey_str, compress), security::kPublicKeySize);
//         return pubkey_str;
//     }
// 
//     void CreateElectionBlockMemeber(uint32_t net_id, std::vector<std::string>& pri_vec) {
//         std::map<uint32_t, common::MembersPtr> in_members;
//         std::map<uint32_t, common::MembersPtr> out_members;
//         std::map<uint32_t, elect::NodeIndexMapPtr> in_index_members;
//         std::map<uint32_t, uint32_t> begin_index_map_;
//         auto shard_members_ptr = std::make_shared<Members>();
//         auto shard_members_index_ptr = std::make_shared<
//             std::unordered_map<std::string, uint32_t>>();
//         for (uint32_t i = 0; i < pri_vec.size(); ++i) {
//             auto iter = in_members.find(net_id);
//             if (iter == in_members.end()) {
//                 in_members[net_id] = std::make_shared<elect::Members>();
//                 in_index_members[net_id] = std::make_shared<
//                     std::unordered_map<std::string, uint32_t>>();
//                 begin_index_map_[net_id] = 0;
//             }
// 
//             security::PrivateKey prikey(pri_vec[i]);
//             security::PublicKey pubkey(prikey);
//             std::string pubkey_str;
//             ASSERT_EQ(pubkey.Serialize(pubkey_str, false), security::kPublicKeyUncompressSize);
//             std::string id = security::Secp256k1::Instance()->ToAddressWithPublicKey(pubkey_str);
//             in_members[net_id]->push_back(std::make_shared<common::BftMember>(
//                 net_id, id, pubkey_str, begin_index_map_[net_id], "", i == 0 ? 0 : -1));
//             in_index_members[net_id]->insert(std::make_pair(id, begin_index_map_[net_id]));
//             ++begin_index_map_[net_id];
//             shard_members_ptr->push_back(std::make_shared<common::BftMember>(
//                 net_id,
//                 id,
//                 pubkey_str,
//                 i,
//                 "dht_key",
//                 -1));
//             (*shard_members_index_ptr)[id] = i;
//         }
// 
//         ElectManager::Instance()->members_ptr_[net_id] = shard_members_ptr;
//         ElectManager::Instance()->node_index_map_[net_id] = shard_members_index_ptr;
//     }
// 
//     void SetGloableInfo(const std::string& private_key, uint32_t network_id) {
//         security::PrivateKey prikey(private_key);
//         security::PublicKey pubkey(prikey);
//         std::string pubkey_str;
//         ASSERT_EQ(pubkey.Serialize(pubkey_str, false), security::kPublicKeyUncompressSize);
//         std::string id = security::Secp256k1::Instance()->ToAddressWithPublicKey(pubkey_str);
//         security::Security::Instance()->set_prikey(std::make_shared<security::PrivateKey>(prikey));
//         common::GlobalInfo::Instance()->set_id(id);
//         common::GlobalInfo::Instance()->set_consensus_shard_count(1);
//         common::GlobalInfo::Instance()->set_network_id(network_id);
//     }
// 
//     void CreateElectBlocks(int32_t member_count, uint32_t network_id) {
//         std::map<uint32_t, common::MembersPtr> in_members;
//         std::map<uint32_t, NodeIndexMapPtr> in_index_members;
//         std::map<uint32_t, uint32_t> begin_index_map_;
//         std::vector<std::string> pri_vec;
//         for (int32_t i = 0; i < member_count; ++i) {
//             char from_data[128];
//             snprintf(from_data, sizeof(from_data), "1%04d%s", i, kRootNodeIdEndFix);
//             std::string prikey = common::Encode::HexDecode(from_data);
//             pri_vec.push_back(prikey);
//             auto net_id = network_id;
//             auto iter = in_members.find(net_id);
//             if (iter == in_members.end()) {
//                 in_members[net_id] = std::make_shared<Members>();
//                 in_index_members[net_id] = std::make_shared<
//                     std::unordered_map<std::string, uint32_t>>();
//                 begin_index_map_[net_id] = 0;
//             }
// 
//             in_members[net_id]->push_back(std::make_shared<common::BftMember>(
//                 net_id,
//                 GetIdByPrikey(prikey),
//                 GetPubkeyByPrikey(prikey),
//                 begin_index_map_[net_id],
//                 "",
//                 -1));
//             in_index_members[net_id]->insert(std::make_pair(GetIdByPrikey(prikey), begin_index_map_[net_id]));
//             ++begin_index_map_[net_id];
//         }
// 
//         for (auto iter = in_members.begin(); iter != in_members.end(); ++iter) {
//             auto index_map_iter = in_index_members.find(iter->first);
//             assert(index_map_iter != in_index_members.end());
//             elect_pool_manager_.NetworkMemberChange(iter->first, iter->second);
//         }
// 
//         SetGloableInfo(pri_vec[0], network::kConsensusShardBeginNetworkId);
//         CreateElectionBlockMemeber(network_id, pri_vec);
//     }
// 
//     void UpdateNodeInfoWithBlock(int32_t member_count, uint64_t height) {
//         block::protobuf::Block block_info;
//         block_info.set_height(height);
//         for (int32_t i = 0; i < member_count; ++i) {
//             char from_data[128];
//             snprintf(from_data, sizeof(from_data), "1%04d%s", i, kRootNodeIdEndFix);
//             std::string prikey = common::Encode::HexDecode(from_data);
// 
//             char to_data[128];
//             snprintf(to_data, sizeof(to_data), "1%04d%s", i + member_count + 1, kRootNodeIdEndFix);
//             std::string to_prikey = common::Encode::HexDecode(to_data);
// 
//             auto tx_list = block_info.mutable_tx_list();
//             auto tx_info = tx_list->Add();
//             tx_info->set_from(GetIdByPrikey(prikey));
//             tx_info->set_to(GetIdByPrikey(to_prikey));
//             tx_info->set_from_pubkey(GetPubkeyByPrikey(prikey));
//             tx_info->set_balance(common::Random::RandomUint64() % (common::kTenonMaxAmount / 1000000));
//         }
// 
//         elect_pool_manager_.UpdateNodeInfoWithBlock(block_info);
//     }
// 
//     void AddWaitingPoolNetworkNodes(int32_t member_count, uint32_t network_id) {
//         for (int32_t i = 0; i < member_count; ++i) {
//             NodeDetailPtr new_node = std::make_shared<ElectNodeDetail>();
//             char from_data[128];
//             snprintf(from_data, sizeof(from_data), "%04d%s", i, kWaitingNodeIdEndFix);
//             std::string prikey = common::Encode::HexDecode(from_data);
//             new_node->id = GetIdByPrikey(prikey);
//             new_node->public_key = GetPubkeyByPrikey(prikey);
//             new_node->dht_key = "";
//             new_node->public_ip = "";
//             new_node->public_port = 0;
//             new_node->join_tm = std::chrono::steady_clock::now() - std::chrono::microseconds(kElectAvailableJoinTime + 1000);
//             new_node->choosed_balance = common::Random::RandomUint64() % (common::kTenonMaxAmount / 1000000);
//             elect_pool_manager_.AddWaitingPoolNode(network_id, new_node);
//         }
//     }
// 
//     void UpdateWaitingNodesConsensusCount(int32_t member_count) {
//         common::BloomFilter pick_all(kBloomfilterWaitingSize, kBloomfilterWaitingHashCount);
//         std::vector<NodeDetailPtr> pick_all_vec;
//         elect_pool_manager_.waiting_pool_map_[
//             network::kConsensusShardBeginNetworkId +
//             network::kConsensusWaitingShardOffset]->GetAllValidHeartbeatNodes(
//             0, 0, pick_all, pick_all_vec);
//         for (int32_t i = 0; i < member_count; ++i) {
//             char from_data[128];
//             snprintf(from_data, sizeof(from_data), "1%04d%s", i, kRootNodeIdEndFix);
//             std::string prikey = common::Encode::HexDecode(from_data);
//             elect_pool_manager_.waiting_pool_map_[
//                 network::kConsensusShardBeginNetworkId +
//                 network::kConsensusWaitingShardOffset]->UpdateWaitingNodes(
//                 GetIdByPrikey(prikey), pick_all);
//         }
//     }
// 
//     void GetStatisticInfo(elect::protobuf::StatisticInfo& statistic_info) {
//         common::GlobalInfo::Instance()->set_network_id(3);
//         static const uint32_t n = 1024;
//         common::MembersPtr members = std::make_shared<elect::Members>();
//         std::vector<std::string> pri_vec;
//         for (uint32_t i = 0; i < n; ++i) {
//             char from_data[128];
//             snprintf(from_data, sizeof(from_data), "1%04d%s", i, kRootNodeIdEndFix);
//             std::string prikey = common::Encode::HexDecode(from_data);
//             pri_vec.push_back(prikey);
//         }
// 
//         std::set<int32_t> leader_idx;
//         while(true) {
//             leader_idx.insert(rand() % 1024);
//             if (leader_idx.size() >= 256) {
//                 break;
//             }
//         }
// 
//         std::vector<int32_t> valid_node_idx;
//         for (int32_t i = 0; i < 1024; ++i) {
//             valid_node_idx.push_back(i);
//         }
//     
//         std::random_shuffle(valid_node_idx.begin(), valid_node_idx.end());
//         int32_t pool_idx = 0;
//         std::vector<common::BftMemberPtr> leaders;
//         for (uint32_t i = 0; i < pri_vec.size(); ++i) {
//             security::PrivateKey prikey(pri_vec[i]);
//             security::PublicKey pubkey(prikey);
//             std::string pubkey_str;
//             ASSERT_EQ(pubkey.Serialize(pubkey_str, false), security::kPublicKeyUncompressSize);
//             std::string id = security::Secp256k1::Instance()->ToAddressWithPublicKey(pubkey_str);
//             auto member = std::make_shared<common::BftMember>(
//                 network::kConsensusShardBeginNetworkId, id, pubkey_str, i, "", i == 0 ? 0 : -1);
//             member->public_ip = "127.0.0.1";
//             member->public_port = 123;
//             members->push_back(member);
//             if (leader_idx.find(i) != leader_idx.end()) {
//                 member->pool_index_mod_num = pool_idx++;
//                 leaders.push_back(member);
//             }
//         }
// 
//         elect::ElectManager::Instance()->latest_leader_count_[3] = leaders.size();
//         libff::alt_bn128_G2 cpk;
//         elect::ElectManager::Instance()->height_with_block_.AddNewHeightBlock(10, 3, members, cpk);
//         static const int32_t kValidCount = 1024 * 2 / 3 + 1;
//         static const int32_t kBlockCount = 10;
//         uint64_t block_height = 0;
//         srand(time(NULL));
//         int32_t invalid_leader_count = 0;
//         for (auto iter = leaders.begin(); iter != leaders.end(); ++iter) {
//             int32_t rand_num = rand() % 100;
//             if (rand_num >= 90) {
//                 ++invalid_leader_count;
//             }
// 
//             for (int32_t bidx = 0; bidx < kBlockCount; ++bidx) {
//                 auto block_item = std::make_shared<block::protobuf::Block>();
//                 block_item->set_electblock_height(10);
//                 block_item->set_timeblock_height(9);
//                 block_item->set_network_id(3);
//                 block_item->set_height(++block_height);
//                 block_item->set_leader_index((*iter)->index);
//                 common::Bitmap bitmap(1024);
//                 std::vector<int32_t> random_set_node = valid_node_idx;
//                 std::random_shuffle(random_set_node.begin(), random_set_node.end());
//                 static const uint32_t kRandomCount = 50;
//                 for (uint32_t i = 0; i < kRandomCount; ++i) {
//                     if (bitmap.valid_count() >= kValidCount) {
//                         break;
//                     }
// 
//                     bitmap.Set(random_set_node[i]);
//                 }
// 
//                 if (rand_num >= 90) {
//                     for (uint32_t i = kRandomCount; i < random_set_node.size(); ++i) {
//                         if (bitmap.valid_count() >= kValidCount) {
//                             break;
//                         }
// 
//                         bitmap.Set(random_set_node[i]);
//                     }
//                 }
// 
//                 for (uint32_t i = 0; i < valid_node_idx.size(); ++i) {
//                     if (bitmap.valid_count() >= kValidCount) {
//                         break;
//                     }
// 
//                     if (bitmap.Valid(valid_node_idx[i])) {
//                         continue;
//                     }
// 
//                     bitmap.Set(valid_node_idx[i]);
//                 }
// 
//                 auto datas = bitmap.data();
//                 for (uint32_t i = 0; i < datas.size(); ++i) {
//                     block_item->add_bitmap(datas[i]);
//                 }
// 
//                 int32_t tx_size = rand() % 10 + 1;
//                 int32_t start_pool_idx = (*iter)->pool_index_mod_num;
//                 for (int32_t i = 0; i < tx_size; ++i) {
//                     auto tx = block_item->add_tx_list();
//                     tx->set_type(common::kConsensusTransaction);
//                     bft::DispatchPool::Instance()->tx_pool_.AddTxCount(start_pool_idx);
//                     start_pool_idx = (start_pool_idx + leaders.size()) % 256;
//                 }
// 
//                 block::ShardStatistic::Instance()->AddStatistic(block_item);
//             }
//         }
// 
//         block::ShardStatistic::Instance()->GetStatisticInfo(9, &statistic_info);
//     }
// private:
//     ElectPoolManager elect_pool_manager_;
// };
// 
// TEST_F(TestElectPoolManager, GetAllBloomFilerAndNodes) {
//     elect::ElectManager::Instance()->elect_net_heights_map_[network::kConsensusShardBeginNetworkId] = 10;
//     const uint32_t kMemberCount = 1024;
//     const uint32_t kWaitingCount = 11;
//     CreateElectBlocks(kMemberCount, network::kConsensusShardBeginNetworkId);
//     CreateElectBlocks(kMemberCount, network::kRootCongressNetworkId);
//     for (uint32_t i = 0; i < 20; ++i) {
//         UpdateNodeInfoWithBlock(kMemberCount, i);
//     }
// 
//     AddWaitingPoolNetworkNodes(
//         kWaitingCount,
//         network::kConsensusShardBeginNetworkId + network::kConsensusWaitingShardOffset);
//     auto waiting_pool_ptr = elect_pool_manager_.waiting_pool_map_[
//         network::kConsensusShardBeginNetworkId + network::kConsensusWaitingShardOffset];
//     ASSERT_TRUE(waiting_pool_ptr != nullptr);
//     ASSERT_EQ(waiting_pool_ptr->node_map_.size(), kWaitingCount);
//     auto latest_time_block_tm = common::TimeUtils::TimestampSeconds() - common::kTimeBlockCreatePeriodSeconds;
//     elect_pool_manager_.OnTimeBlock(latest_time_block_tm);
//     UpdateWaitingNodesConsensusCount(kMemberCount);
//     ASSERT_EQ(waiting_pool_ptr->all_nodes_waiting_map_.size(), 1);
//     auto waiting_iter = waiting_pool_ptr->all_nodes_waiting_map_.begin();
//     ASSERT_EQ(waiting_iter->second->nodes_vec.size(), kWaitingCount);
//     common::BloomFilter cons_all(kBloomfilterSize, kBloomfilterHashCount);
//     common::BloomFilter cons_weed_out(kBloomfilterSize, kBloomfilterHashCount);
//     common::BloomFilter pick_all(kBloomfilterWaitingSize, kBloomfilterWaitingHashCount);
//     common::BloomFilter pick_in(kBloomfilterSize, kBloomfilterHashCount);
//     std::vector<NodeDetailPtr> exists_shard_nodes;
//     std::vector<NodeDetailPtr> weed_out_vec;
//     std::vector<NodeDetailPtr> pick_in_vec;
//     int32_t leader_count = 0;
//     elect::protobuf::StatisticInfo statistic_info;
//     GetStatisticInfo(statistic_info);
//     uint32_t shard_netid = network::kConsensusShardBeginNetworkId;
//     std::vector<NodeDetailPtr> elected_nodes;
//     uint64_t etime3 = common::TimeUtils::TimestampMs();
//     block::protobuf::BlockTx tx_info;
//     tx_info.set_network_id(3);
//     auto attr = tx_info.add_attr();
//     attr->set_key(bft::kStatisticAttr);
//     attr->set_value(statistic_info.SerializeAsString());
//     ASSERT_EQ(elect_pool_manager_.GetElectionTxInfo(tx_info), kElectSuccess);
//     auto res_str = tx_info.SerializeAsString();
//     elect::protobuf::ElectBlock ec_block;
//     for (int32_t i = 0; i < tx_info.attr_size(); ++i) {
//         std::cout << i << ": " << tx_info.attr(i).key() << ", " << kElectNodeAttrElectBlock << std::endl;
//         if (tx_info.attr(i).key() == kElectNodeAttrElectBlock) {
//             ASSERT_TRUE(ec_block.ParseFromString(tx_info.attr(i).value()));
//         }
//     }
// 
//     ASSERT_TRUE(ec_block.weedout_ids_size() >= 20);
//     ASSERT_TRUE(ec_block.in_size() >= 1000);
// }
// 
// }  // namespace test
// 
// }  // namespace elect
// 
// }  // namespace zjchain
