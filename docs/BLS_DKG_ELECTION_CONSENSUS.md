# BLS 阈值签名、DKG 与选举机制深度分析

> 基于 src/bls/、src/elect/、src/consensus/hotstuff/ 的完整实现分析

---

## 一、整体架构概览

系统将 **BLS 阈值签名**、**分布式密钥生成（DKG）** 和 **选举机制** 三者紧密耦合，形成一条完整的信任链：

```
选举区块（ElectBlock）
    │  包含新成员列表 + 前一轮 BLS 公钥
    ▼
DKG 协议（BlsDkg）
    │  三阶段：验证向量广播 → 秘密份额交换 → 完成广播
    ▼
ElectItem（elect_info.h）
    │  持有：common_pk（公共公钥）+ local_sk（本地私钥份额）
    ▼
共识签名（Crypto）
    │  PartialSign → ReconstructAndVerifyThresSign
    ▼
QC（Quorum Certificate）
    │  sign_x + sign_y = BLS 阈值签名的 G1 点坐标
    ▼
区块提交
```

---

## 二、密码学基础

### 2.1 曲线与参数

系统使用 **alt_bn128**（BN254）椭圆曲线：

| 参数 | 类型 | 说明 |
|------|------|------|
| `libff::alt_bn128_Fr` | 标量域 | 私钥、秘密份额 |
| `libff::alt_bn128_G1` | G1 群点 | 签名、哈希映射 |
| `libff::alt_bn128_G2` | G2 群点 | 公钥、验证向量 |
| `libff::alt_bn128_GT` | GT 群 | 配对结果，用于验证 |

### 2.2 阈值参数

```cpp
// common/utils.h
inline static uint32_t GetSignerCount(uint32_t n) {
    auto t = n * 2 / 3;
    if ((n * 2) % 3 > 0) { t += 1; }
    return t;
}
// n=10 → t=7, n=100 → t=67
```

即 **t = ⌈2n/3⌉**，满足拜占庭容错要求（f < n/3）。

### 2.3 两种 BLS 模式

| 模式 | 类 | 用途 |
|------|-----|------|
| **阈值 BLS** | `BlsSign` | 共识投票，需 t-of-n 聚合 |
| **聚合 BLS** | `AggBls` | 节点身份证明（PoP），单节点签名 |


---

## 三、BLS 阈值签名详解

### 3.1 签名（BlsSign::Sign）

```cpp
// src/bls/bls_sign.cc
void BlsSign::Sign(
        uint32_t t, uint32_t n,
        const libff::alt_bn128_Fr& secret_key,   // 本地私钥份额 sk_i
        const libff::alt_bn128_G1& g1_hash,       // H(message) ∈ G1
        libff::alt_bn128_G1* sign) {
    libBLS::Bls bls_instance = libBLS::Bls(t, n);
    *sign = bls_instance.Signing(g1_hash, secret_key);
    // 计算：σ_i = sk_i · H(m)
}
```

**数学表达**：每个节点 i 计算部分签名 σ_i = sk_i · H(m)，其中 H: {0,1}* → G1。

### 3.2 哈希映射到 G1

```cpp
// src/consensus/hotstuff/crypto.cc
void Crypto::GetG1Hash(const HashStr& msg_hash, libff::alt_bn128_G1* g1_hash) {
    auto hex_str = common::Encode::HexEncode(msg_hash);
    *g1_hash = libBLS::ThresholdUtils::HashtoG1(hex_str);
}
```

### 3.3 阈值签名重建（Crypto::ReconstructAndVerifyThresSign）

```cpp
// 收集 t 个部分签名后重建
std::vector<libff::alt_bn128_G1> all_signs;
std::vector<size_t> idx_vec;
for (auto& [index, partial_sign] : collected_signs) {
    all_signs.push_back(*partial_sign);
    idx_vec.push_back(index + 1);  // 1-indexed
}

// 计算 Lagrange 系数
std::vector<libff::alt_bn128_Fr> lagrange_coeffs(t);
libBLS::ThresholdUtils::LagrangeCoeffs(idx_vec, t, lagrange_coeffs);

// 重建签名：σ = Σ λ_i · σ_i
libBLS::Bls bls_instance(t, n);
auto reconstructed = bls_instance.SignatureRecover(all_signs, lagrange_coeffs);
```

**数学表达**：
- Lagrange 插值：σ = Σᵢ λᵢ · σᵢ，其中 λᵢ 是 Lagrange 系数
- 验证：e(σ, G2) == e(H(m), PK_common)，使用双线性配对

### 3.4 验证

```cpp
// src/bls/bls_sign.cc
int BlsSign::Verify(uint32_t t, uint32_t n,
        const libff::alt_bn128_G1& sign,
        const libff::alt_bn128_G1& g1_hash,
        const libff::alt_bn128_G2& pkey,
        std::string* verify_hash) {
    libBLS::Bls bls_instance(t, n);
    libff::alt_bn128_GT res;
    bls_instance.Verification(g1_hash, sign, pkey, &res);
    // 验证：e(sign, G2) == e(g1_hash, pkey)
    *verify_hash = GetVerifyHash(res);  // keccak256(GT 元素的字符串表示)
}
```

`verify_hash` 是 GT 群元素的 keccak256 哈希，作为 QC 的唯一标识符。

### 3.5 聚合 BLS（AggBls）

用于节点身份证明（Proof of Possession）：

```cpp
// src/bls/agg_bls.cc
void AggBls::Sign(const libff::alt_bn128_Fr& sec_key,
                  const std::string& str_hash,
                  libff::alt_bn128_G1* signature) {
    auto hex_str = common::Encode::HexEncode(str_hash);
    *signature = libBLS::Bls::CoreSignAggregated(hex_str, sec_key);
}

// 快速聚合验证（相同消息，多个公钥）
bool AggBls::FastAggregateVerify(
        const std::vector<libff::alt_bn128_G2>& pks,
        const std::string& str_hash,
        const libff::alt_bn128_G1& signature) {
    return libBLS::Bls::FastAggregateVerify(pks, common::Encode::HexEncode(str_hash), signature);
}
```


---

## 四、分布式密钥生成（DKG）协议

### 4.1 DKG 触发时机

每次新选举区块到来时，`BlsDkg::OnNewElectionBlock()` 被调用：

```cpp
// src/bls/bls_dkg.cc
void BlsDkg::OnNewElectionBlock(
        uint64_t elect_height,
        uint64_t prev_elect_height,
        common::MembersPtr& members,
        std::shared_ptr<TimeBlockItem> latest_timeblock_info) {
    if (elect_height <= elect_hegiht_) return;

    // 重置状态
    memset(valid_swaped_keys_, 0, sizeof(valid_swaped_keys_));
    finished_ = false;
    max_finish_count_ = 0;
    member_count_ = members->size();
    min_aggree_member_count_ = common::GetSignerCount(member_count_);
    begin_time_us_ = common::TimeUtils::TimestampUs();

    // 计算各阶段偏移（加入随机抖动避免同步广播）
    swap_offset_ = kDkgPeriodUs * 4 + (vss_random % kDkgPeriodUs);
    finish_offset_ = kDkgPeriodUs * 5 + (vss_random % kDkgPeriodUs);
}
```

### 4.2 时间参数

```
kTimeBlsPeriodSeconds = kTimeBlockCreatePeriodSeconds / 3  (约 200 秒)
kDkgPeriodUs = kTimeBlsPeriodSeconds / 10 * 1,000,000     (约 20 秒)

阶段时间线（相对于 begin_time_us_）：
├── 0                    → 4×kDkgPeriodUs  : 阶段1 验证向量广播
├── 4×kDkgPeriodUs + δ₁ → 8×kDkgPeriodUs  : 阶段2 秘密份额交换
└── 5×kDkgPeriodUs + δ₂ → 10×kDkgPeriodUs : 阶段3 完成广播
```

δ₁, δ₂ 是基于 VSS 随机数的随机偏移，防止所有节点同时广播。

### 4.3 阶段一：验证向量广播（BroadcastVerfify）

**目的**：每个节点公开其多项式的承诺（Feldman VSS）

```
节点 i 生成 t 阶多项式：
    f_i(x) = a_{i,0} + a_{i,1}·x + ... + a_{i,t-1}·x^{t-1}  (系数 ∈ Fr)

广播验证向量（Feldman 承诺）：
    V_i = [a_{i,0}·G2, a_{i,1}·G2, ..., a_{i,t-1}·G2]  (G2 点)
```

```cpp
// src/bls/bls_dkg.cc - CreateContribution()
void BlsDkg::CreateContribution(uint32_t valid_n, uint32_t valid_t) {
    libBLS::Dkg dkg_instance(valid_t, valid_n);
    auto polynomial = dkg_instance.GeneratePolynomial();  // 生成随机多项式

    // 计算验证向量
    auto g2_vec = dkg_instance.VerificationVector(polynomial);
    // g2_vec[k] = polynomial[k] * G2::one()

    // 计算对每个成员的秘密份额
    local_src_secret_key_contribution_ = dkg_instance.SecretKeyContribution(polynomial);
    // local_src_secret_key_contribution_[j] = f_i(j+1)

    // 存储验证向量
    for (uint32_t i = 0; i < valid_t; ++i) {
        g2_vec_[i] = g2_vec[i];
    }
}
```

**广播消息**（`VerifyVecBrdReq`）：
```protobuf
message VerifyVecBrdReq {
    repeated VerifyVecItem verify_vec = 1;  // t 个 G2 点
    optional uint32 change_idx = 2;
}
```

**接收方验证**：
```cpp
// 验证收到的验证向量是否合法
// 对于成员 j 收到成员 i 的验证向量 V_i：
// 验证：f_i(j+1) * G2 == Σ_{k=0}^{t-1} (j+1)^k * V_i[k]
bool valid = dkg_instance.Verification(j, contribution_ij, g2_vec_i);
```

### 4.4 阶段二：秘密份额交换（SwapSecKey）

**目的**：节点 i 将 f_i(j) 加密后发送给节点 j

```cpp
// src/bls/bls_dkg.cc - CreateSwapKey()
void BlsDkg::CreateSwapKey(uint32_t member_idx, std::string* seckey, int32_t* seckey_len) {
    // 获取对成员 member_idx 的秘密份额
    auto contribution = local_src_secret_key_contribution_[member_idx];
    auto seckey_str = libBLS::ThresholdUtils::fieldElementToString(contribution);

    // 使用 ECDH 密钥加密
    auto member = (*members_)[member_idx];
    std::string ecdh_key;
    security_->GetEcdhKey(member->pubkey_str, &ecdh_key);

    // AES 加密
    security_->Encrypt(seckey_str, {ecdh_key.c_str(), ecdh_key.size()}, seckey);
    *seckey_len = seckey->size();
}
```

**广播消息**（`SwapSecKeyReq`）：
```protobuf
message SwapSecKeyReq {
    repeated SwapSecKeyItem keys = 1;  // 对每个成员加密的份额
}
message SwapSecKeyItem {
    optional bytes sec_key = 1;      // ECDH 加密的 f_i(j)
    optional uint32 sec_key_len = 2;
}
```

**接收方处理**：
```cpp
// 节点 j 收到节点 i 的加密份额
// 1. 用自己的私钥解密
security_->Decrypt(encrypted_key, raw_private_key, &decrypted_key);

// 2. 验证解密结果
libff::alt_bn128_Fr seckey(decrypted_key.c_str());
libff::alt_bn128_G2 expected = GetVerifyG2FromDb(peer_index);
bool valid = (seckey * libff::alt_bn128_G2::one() == expected);

// 3. 存入缓存
if (valid) {
    dkg_cache_->SetSwapKey(network_id, local_member_index_, id, peer_index, decrypted_key);
    valid_swapkey_set_.insert(peer_index);
}
```

### 4.5 阶段三：完成广播（FinishBroadcast）

**目的**：聚合所有有效份额，生成本地私钥份额和公共公钥

```cpp
// src/bls/bls_dkg.cc - FinishBroadcast()
void BlsDkg::FinishBroadcast() {
    std::vector<libff::alt_bn128_Fr> valid_seck_keys;
    common::Bitmap valid_bitmap(member_count_);

    for (size_t i = 0; i < member_count_; ++i) {
        std::string seckey_str;
        if (!dkg_cache_->GetSwapKey(network_id, local_member_index_, id, i, &seckey_str)) {
            continue;
        }

        libff::alt_bn128_Fr seckey(seckey_str.c_str());
        valid_seck_keys.push_back(seckey);
        valid_bitmap.Set(i);

        // 累加公共公钥：PK_common = Σ a_{i,0} * G2
        common_public_key_ = common_public_key_ + for_common_pk_g2s_[i];
    }

    // 恢复本地私钥份额
    libBLS::Dkg dkg(min_aggree_member_count_, member_count_);
    local_sec_key_ = dkg.SecretKeyShareCreate(valid_seck_keys);
    // local_sec_key_ = Σ f_j(i+1)  (对所有有效成员 j 求和)

    // 计算本地公钥
    local_publick_key_ = dkg.GetPublicKeyFromSecretKey(local_sec_key_);

    // 广播完成消息
    BroadcastFinish(valid_bitmap);
    finished_ = true;
}
```

**广播消息**（`FinishBroadcast`）：
```protobuf
message FinishBroadcast {
    repeated uint64 bitmap = 1;       // 有效参与成员的位图
    optional BlsPublicKey pubkey = 2;         // 本地公钥 (G2)
    optional BlsPublicKey common_pubkey = 3;  // 公共公钥 (G2)
    optional uint32 network_id = 4;
    optional bytes bls_sign_x = 5;    // 对完成消息的签名
    optional bytes bls_sign_y = 6;
}
```

### 4.6 DKG 数学正确性

```
设 n 个节点，每个节点 i 有多项式 f_i(x)

节点 j 的本地私钥份额：
    sk_j = Σᵢ f_i(j+1)  (对所有有效节点 i 求和)

公共公钥：
    PK = Σᵢ f_i(0) * G2 = Σᵢ a_{i,0} * G2

验证关系：
    sk_j * G2 = Σᵢ f_i(j+1) * G2
              = Σᵢ Σₖ a_{i,k} * (j+1)^k * G2
              = Σᵢ Σₖ (j+1)^k * V_i[k]

阈值签名重建（Lagrange 插值）：
    σ = Σⱼ λⱼ * σⱼ = Σⱼ λⱼ * sk_j * H(m)
      = (Σⱼ λⱼ * sk_j) * H(m)
      = f(0) * H(m)  (Lagrange 插值恢复 f(0) = Σᵢ a_{i,0})
      = sk_master * H(m)

验证：e(σ, G2) = e(sk_master * H(m), G2) = e(H(m), sk_master * G2) = e(H(m), PK)
```


---

## 五、选举机制

### 5.1 选举区块结构（elect.proto）

```protobuf
message ElectBlock {
    repeated member in = 1;           // 新加入的成员列表
    optional PrevMembers prev_members = 2;  // 上一轮成员的 BLS 公钥
    optional uint32 shard_network_id = 3;
    optional uint64 elect_height = 4;
    optional uint64 all_gas_amount = 5;
    optional uint64 gas_for_root = 6;
}

message member {
    optional bytes pubkey = 1;           // ECDSA 公钥
    optional int32 pool_idx_mod_num = 2; // 池分配索引
    optional uint64 mining_amount = 3;   // 挖矿收益
    optional uint64 fts_value = 4;       // FTS 权重值
    optional uint64 consensus_gap = 5;   // 共识间隔
}

message PrevMembers {
    repeated PrevMemberInfo bls_pubkey = 1;  // 上一轮每个成员的 BLS G2 公钥
    optional uint64 prev_elect_height = 2;
    optional BlsPublicKey common_pubkey = 3; // 上一轮公共公钥
}
```

### 5.2 节点加入选举（JoinElectTxItem）

节点通过提交 `kJoinElect` 类型交易申请加入选举：

```cpp
// src/consensus/zbft/join_elect_tx_item.cc
int JoinElectTxItem::HandleTx(...) {
    bls::protobuf::JoinElectInfo join_info;
    join_info.ParseFromString(tx_info->value());

    // 验证 BLS 验证向量大小必须等于 t
    auto n = common::GlobalInfo::Instance()->each_shard_max_members();
    auto t = common::GetSignerCount(n);
    if (join_info.g2_req().verify_vec_size() != static_cast<int>(t)) {
        block_tx.set_status(consensus::kConsensusJoinElectThreashTInvalid);
        break;
    }

    // 验证分片 ID 合法性
    if (join_info.shard_id() != common::GlobalInfo::Instance()->network_id()) {
        block_tx.set_status(consensus::kConsensusError);
        break;
    }

    // 扣除 gas 费用
    gas_used = consensus::kJoinElectGas;
    // 存储 JoinElectInfo 到链上
}
```

**JoinElectInfo 结构**（bls.proto）：
```protobuf
message JoinElectInfo {
    optional uint32 shard_id = 1;
    optional uint32 member_idx = 2;
    optional uint32 change_idx = 3;
    optional VerifyVecBrdReq g2_req = 4;  // t 个 G2 验证向量点
    optional bytes addr = 5;
    optional uint64 stoke = 6;
    optional bytes public_key = 7;
    optional uint64 stake_amount = 8;
    optional uint64 total_staked = 10;
    optional StakeOperation stake_op = 11;  // NONE/STAKE/REDEEM
}
```

### 5.3 选举执行（ElectTxItem）

选举交易由根分片共识执行，使用 FTS（Fitness-based Selection）算法：

```cpp
// src/consensus/zbft/elect_tx_item.cc
int ElectTxItem::HandleTx(...) {
    // 解析选举统计信息
    elect_statistic_.ParseFromString(tx_info->value());
    elect_block_ = elect_statistic_.mutable_elect_block();

    // 使用 VSS 随机数初始化 FTS 树
    g2_ = std::make_shared<std::mt19937_64>(vss_mgr_->EpochRandom());

    // 基于 stoke（权益）和 fts_value 进行加权随机选择
    // 面积惩罚系数 kAreaPenaltyCoefficient 用于地理分布优化
}
```

### 5.4 选举区块处理（ElectManager::OnNewElectBlock）

```cpp
// src/elect/elect_manager.cc
common::MembersPtr ElectManager::OnNewElectBlock(
        uint64_t height,
        const std::shared_ptr<elect::protobuf::ElectBlock>& elect_block_ptr,
        const std::shared_ptr<elect::protobuf::ElectBlock>& prev_elect_block_ptr) {

    // 1. 处理上一轮成员（携带 BLS 公钥）
    bool cons_elect_valid = ProcessPrevElectMembers(
        height, elect_block, &elected, *prev_elect_block_ptr);

    // 2. 处理新成员
    ProcessNewElectBlock(height, elect_block, &elected);

    // 3. 决定本节点是否当选
    if (!cons_elect_valid && !elected) {
        if (common::GlobalInfo::Instance()->network_id() == elect_block.shard_network_id()) {
            elected = true;
        }
    }

    // 4. 加入对应分片网络
    ElectedToConsensusShard(elect_block, elected);

    return members_ptr_[elect_block.shard_network_id()].load();
}
```

### 5.5 分片分配

```cpp
void ElectManager::ElectedToConsensusShard(
        protobuf::ElectBlock& elect_block, bool cons_elected) {
    auto local_netid = elect_block.shard_network_id();

    if (!cons_elected) {
        // 未当选 → 加入等待分片（waiting shard）
        Join(local_netid + network::kConsensusWaitingShardOffset);
        common::GlobalInfo::Instance()->set_network_id(
            local_netid + network::kConsensusWaitingShardOffset);
    } else {
        // 当选 → 加入共识分片
        Join(elect_block.shard_network_id());
        common::GlobalInfo::Instance()->set_network_id(elect_block.shard_network_id());
    }
}
```

### 5.6 BLS 公钥在选举区块中的传递

```
选举高度 H 的 ElectBlock：
    prev_members.bls_pubkey[i] = 高度 H-1 的成员 i 的 BLS G2 公钥
    prev_members.common_pubkey = 高度 H-1 的公共公钥

这些公钥来自 DKG 完成后的 FinishBroadcast 消息，
由 BlsManager::OnNewElectBlock() 收集并写入下一个选举区块。
```


---

## 六、ElectItem：连接选举与共识

### 6.1 ElectItem 数据结构

```cpp
// src/consensus/hotstuff/elect_info.h
class ElectItem {
    common::MembersPtr members_;        // 所有当选成员（含 BLS 公钥）
    common::MembersPtr valid_leaders_;  // 有效 BLS 公钥的成员子集
    common::BftMemberPtr local_member_; // 本节点的成员信息
    uint64_t elect_height_;             // 选举高度（密钥版本号）
    libff::alt_bn128_G2 common_pk_;     // 公共 BLS 公钥（用于验证）
    libff::alt_bn128_Fr local_sk_;      // 本地私钥份额（用于签名）
    uint32_t bls_t_, bls_n_;            // 阈值 t 和总数 n
    bool bls_valid_;                    // 本节点 BLS 密钥是否有效
};
```

### 6.2 ElectItem 的创建

```cpp
// 构造时从成员列表中提取有效 BLS 公钥
ElectItem(const std::shared_ptr<security::Security>& security,
          uint32_t sharding_id, uint64_t elect_height,
          const common::MembersPtr& members,
          const libff::alt_bn128_G2& common_pk,
          const libff::alt_bn128_Fr& sk) {

    valid_leaders_ = std::make_shared<common::Members>();
    for (uint32_t i = 0; i < members->size(); i++) {
        // 只有 BLS 公钥非零的成员才是有效 leader
        if ((*members)[i]->bls_publick_key != libff::alt_bn128_G2::zero()) {
            valid_leaders_->push_back((*members)[i]);
        }
        // 找到本节点
        if ((*members)[i]->id == security_ptr_->GetAddress()) {
            local_member_ = (*members)[i];
            if (local_member_->bls_publick_key != libff::alt_bn128_G2::zero()) {
                bls_valid_ = true;
            }
        }
    }

    common_pk_ = common_pk;
    local_sk_ = sk;
    bls_t_ = common::GetSignerCount(members->size());
    bls_n_ = members->size();
}
```

### 6.3 ElectInfo：多 epoch 管理

```cpp
// src/consensus/hotstuff/elect_info.h
class ElectInfo {
    // 按 (sharding_id, elect_height) 索引的 ElectItem 映射
    std::map<uint64_t, std::shared_ptr<ElectItem>> elect_items_[kConsensusShardEndNetworkId];

    // 获取最新的 ElectItem
    std::shared_ptr<ElectItem> GetElectItemWithShardingId(uint32_t sharding_id) const;

    // 获取指定高度的 ElectItem（用于历史验证）
    std::shared_ptr<ElectItem> GetElectItem(uint32_t sharding_id, uint64_t elect_height) const;
};
```

**为什么需要多 epoch**：QC 中携带 `elect_height`，验证时需要找到对应高度的 `common_pk`，因此需要保留历史 ElectItem。

---

## 七、共识中的 BLS 集成

### 7.1 Crypto 类：共识与 BLS 的桥梁

```cpp
// src/consensus/hotstuff/crypto.h
class Crypto {
    uint32_t pool_idx_;
    std::shared_ptr<ElectInfo> elect_info_;
    std::shared_ptr<bls::IBlsManager> bls_mgr_;

    // 部分签名（副本节点投票时调用）
    Status PartialSign(uint32_t sharding_id, uint64_t elect_height,
                       const HashStr& msg_hash,
                       std::string* sign_x, std::string* sign_y);

    // 重建并验证阈值签名（leader 收集足够投票后调用）
    Status ReconstructAndVerifyThresSign(
        const transport::MessagePtr& msg_ptr,
        uint64_t elect_height, View view,
        const HashStr& msg_hash,
        uint32_t member_idx,
        const std::string& partial_sign_x,
        const std::string& partial_sign_y,
        std::shared_ptr<libff::alt_bn128_G1>& reconstructed_sign);
};
```

### 7.2 投票消息中的 BLS 签名

```
副本节点投票流程：
1. 收到 Propose 消息
2. 验证通过后，调用 Crypto::PartialSign()
3. 生成部分签名 (sign_x, sign_y)
4. 发送 VoteMsg 给 leader

VoteMsg 结构：
    view_block_hash: 被投票的区块哈希
    view: 视图号
    elect_height: 选举高度（密钥版本）
    replica_idx: 副本节点索引
    leader_idx: leader 索引
    sign_x, sign_y: BLS 部分签名的 G1 点坐标
    tm_height: 时间块高度
```

### 7.3 QC 生成流程

```cpp
// src/consensus/hotstuff/hotstuff.cc - HandleVoteMsgImpl()
Status Hotstuff::HandleVoteMsgImpl(const transport::MessagePtr& msg_ptr) {
    auto& vote_msg = msg_ptr->header.hotstuff().vote_msg();

    // 构建 QC 基本信息
    QC qc_item;
    qc_item.set_view(vote_msg.view());
    qc_item.set_view_block_hash(vote_msg.view_block_hash());
    qc_item.set_elect_height(vote_msg.elect_height());
    qc_item.set_leader_idx(vote_msg.leader_idx());

    // 计算 QC 哈希（用于 BLS 签名的消息）
    auto qc_hash = GetQCMsgHash(qc_item);

    // 收集部分签名，尝试重建阈值签名
    std::shared_ptr<libff::alt_bn128_G1> reconstructed_sign;
    Status ret = crypto()->ReconstructAndVerifyThresSign(
        msg_ptr, elect_height, vote_msg.view(),
        qc_hash, replica_idx,
        vote_msg.sign_x(), vote_msg.sign_y(),
        reconstructed_sign);

    if (ret == Status::kSuccess) {
        // 将重建的签名写入 QC
        qc_item.set_sign_x(libBLS::ThresholdUtils::fieldElementToString(reconstructed_sign->X));
        qc_item.set_sign_y(libBLS::ThresholdUtils::fieldElementToString(reconstructed_sign->Y));

        // 更新 HighQC，触发提交
        view_block_chain()->UpdateHighViewBlock(qc_item);
        UpdateLatestQcItemPtr(qc_item_ptr);
    }
}
```

### 7.4 QC 验证

```cpp
// src/consensus/hotstuff/crypto.cc - VerifyQC()
Status Crypto::VerifyQC(uint32_t sharding_id, const QC& qc) {
    auto elect_item = GetElectItem(sharding_id, qc.elect_height());

    // 重建 G1 签名点
    libff::alt_bn128_G1 sign;
    sign.X = libff::alt_bn128_Fq(qc.sign_x().c_str());
    sign.Y = libff::alt_bn128_Fq(qc.sign_y().c_str());

    // 计算 QC 哈希
    auto qc_hash = GetQCMsgHash(qc);
    libff::alt_bn128_G1 g1_hash;
    GetG1Hash(qc_hash, &g1_hash);

    // 用公共公钥验证
    std::string verify_hash;
    auto ret = bls_mgr_->Verify(
        elect_item->t(), elect_item->n(),
        elect_item->common_pk(),  // 使用 elect_height 对应的公共公钥
        sign, g1_hash, &verify_hash);

    return (ret == bls::kBlsSuccess) ? Status::kSuccess : Status::kError;
}
```


---

## 八、完整生命周期时序图

```
时间轴 ──────────────────────────────────────────────────────────────────►

选举高度 H-1                    选举高度 H                    选举高度 H+1
    │                               │                               │
    ▼                               ▼                               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         选举阶段（根分片执行）                            │
│                                                                         │
│  节点提交 JoinElect 交易                                                 │
│  ├── 携带 JoinElectInfo.g2_req（t 个 G2 验证向量点）                     │
│  ├── 携带 stake_amount（质押金额）                                       │
│  └── 验证：verify_vec_size == t                                         │
│                                                                         │
│  ElectTxItem 执行选举                                                    │
│  ├── FTS 加权随机选择 n 个节点                                           │
│  ├── 生成 ElectBlock（含 prev_members BLS 公钥）                        │
│  └── 写入区块链                                                          │
└─────────────────────────────────────────────────────────────────────────┘
    │
    ▼ ElectBlock 被提交
┌─────────────────────────────────────────────────────────────────────────┐
│                         DKG 阶段（当选节点执行）                          │
│                                                                         │
│  BlsDkg::OnNewElectionBlock() 触发                                      │
│                                                                         │
│  [0, 4×kDkgPeriodUs]  阶段1：验证向量广播                               │
│  ├── 每个节点生成随机多项式 f_i(x)                                       │
│  ├── 广播验证向量 V_i = [a_{i,0}·G2, ..., a_{i,t-1}·G2]               │
│  └── 接收并验证其他节点的验证向量                                         │
│                                                                         │
│  [4×kDkgPeriodUs, 8×kDkgPeriodUs]  阶段2：秘密份额交换                  │
│  ├── 节点 i 计算 f_i(j) 并用节点 j 的 ECDH 公钥加密                    │
│  ├── 发送加密份额给所有其他节点                                           │
│  └── 接收并解密验证份额                                                  │
│                                                                         │
│  [5×kDkgPeriodUs, 10×kDkgPeriodUs]  阶段3：完成广播                    │
│  ├── 聚合有效份额：sk_j = Σᵢ f_i(j+1)                                  │
│  ├── 计算公共公钥：PK = Σᵢ a_{i,0}·G2                                  │
│  ├── 广播 FinishBroadcast（含 bitmap + 公钥）                           │
│  └── 存储加密私钥到 DB                                                  │
└─────────────────────────────────────────────────────────────────────────┘
    │
    ▼ DKG 完成
┌─────────────────────────────────────────────────────────────────────────┐
│                         共识阶段（使用新密钥）                            │
│                                                                         │
│  ElectItem 创建：                                                        │
│  ├── elect_height = H                                                   │
│  ├── common_pk = DKG 生成的公共公钥                                     │
│  ├── local_sk = DKG 生成的本地私钥份额                                  │
│  └── members = 当选成员列表（含各自 BLS G2 公钥）                       │
│                                                                         │
│  每轮共识（Propose → Vote → QC）：                                      │
│                                                                         │
│  Leader 广播 Propose                                                    │
│      │                                                                  │
│      ▼                                                                  │
│  副本验证 Propose                                                        │
│  └── Crypto::PartialSign(elect_height, msg_hash)                       │
│      └── sign_i = local_sk · H(qc_hash)                               │
│      └── 发送 VoteMsg(sign_x, sign_y, elect_height, replica_idx)      │
│                                                                         │
│  Leader 收集 ≥ t 个 VoteMsg                                            │
│  └── Crypto::ReconstructAndVerifyThresSign()                           │
│      ├── 收集 t 个部分签名                                              │
│      ├── Lagrange 插值重建：σ = Σ λᵢ · σᵢ                             │
│      ├── 验证：e(σ, G2) == e(H(qc_hash), common_pk)                   │
│      └── 生成 QC(sign_x, sign_y, elect_height)                        │
│                                                                         │
│  QC 写入区块，触发提交                                                   │
└─────────────────────────────────────────────────────────────────────────┘
    │
    ▼ 下一个选举高度 H+1
（循环）
```

---

## 九、关键安全属性

### 9.1 密钥安全性

| 属性 | 机制 |
|------|------|
| 私钥份额不泄露 | 份额用 ECDH 加密传输，只有目标节点能解密 |
| 多项式系数保密 | 只广播 G2 承诺，不广播系数本身 |
| 本地私钥加密存储 | `DumpLocalPrivateKey()` 用 ECDH 密钥加密后存 DB |
| 阈值保护 | 任意 t-1 个节点无法重建私钥或伪造签名 |

### 9.2 消息认证

所有 BLS 消息（验证向量广播、秘密份额交换、完成广播）都用节点的 ECDSA 私钥签名：

```cpp
// src/bls/bls_dkg.cc - IsSignValid()
bool BlsDkg::IsSignValid(const transport::MessagePtr msg_ptr, std::string* msg_hash) {
    // 验证消息的 ECDSA 签名
    auto ret = security_->Verify(
        *msg_hash,
        msg_ptr->header.sign(),
        sender_pubkey);
    return ret == security::kSecuritySuccess;
}
```

### 9.3 有效性检查

```cpp
// 验证 QC 有效性
bool IsQcTcValid(const QC& qc) {
    return !qc.sign_x().empty() &&
           !qc.sign_y().empty() &&
           !qc.view_block_hash().empty() &&
           qc.view() > 0;
}
```

### 9.4 位图验证

DKG 完成时，要求有效参与节点比例超过阈值：

```cpp
// src/bls/bls_utils.h
static const float kBlsMaxExchangeMembersRatio = 0.8f;  // 80%

// 至少 80% 的节点成功交换密钥才认为 DKG 有效
if (valid_count < member_count_ * kBlsMaxExchangeMembersRatio) {
    // DKG 失败，等待重试
}
```

---

## 十、elect_height 的核心作用

`elect_height` 是贯穿整个系统的**密钥版本号**：

```
elect_height 的用途：

1. DKG 触发标识
   BlsDkg::OnNewElectionBlock(elect_height, ...)
   → 每个 elect_height 对应一套独立的 DKG 密钥

2. QC 中的密钥版本
   QC.elect_height → 指定验证该 QC 所用的 common_pk

3. 部分签名时的密钥选择
   Crypto::PartialSign(sharding_id, elect_height, ...)
   → 使用 elect_height 对应的 local_sk

4. 签名重建时的公钥选择
   Crypto::ReconstructAndVerifyThresSign(elect_height, ...)
   → 使用 elect_height 对应的 common_pk 验证

5. ElectItem 索引
   ElectInfo::GetElectItem(sharding_id, elect_height)
   → 查找对应的 ElectItem（含 common_pk + local_sk）

6. Leader 选举中的视图跳跃
   GetLeader(): if (high_view_block->qc().elect_height() < latest_elect_height_)
       out_view = high_view_block->qc().view() + latest_elect_height_ + k + 1
   → 新选举轮次时视图号跳跃，避免与旧轮次冲突
```

---

## 十一、数据流总结

```
节点启动
    │
    ├── AggBls::Init()          生成/加载聚合 BLS 密钥对（用于 PoP）
    ├── BlsManager 初始化        注册 kBlsMessage 消息处理器
    └── 等待选举区块
    │
    ▼ 收到选举区块（elect_height = H）
    │
    ├── ElectManager::OnNewElectBlock()
    │   ├── 解析 ElectBlock（新成员 + 上一轮 BLS 公钥）
    │   ├── 更新 members_ptr_[shard_id]
    │   └── 决定本节点是否当选
    │
    ├── BlsManager::OnNewElectBlock()
    │   └── 创建新的 BlsDkg 实例，开始 DKG
    │
    └── DKG 三阶段（约 200 秒）
        ├── 阶段1：广播验证向量
        ├── 阶段2：交换加密秘密份额
        └── 阶段3：恢复密钥，广播完成
    │
    ▼ DKG 完成
    │
    ├── ElectInfo::AddElectItem(H, members, common_pk, local_sk)
    │   └── 创建 ElectItem，供 Crypto 使用
    │
    └── 共识开始使用新密钥
        ├── 投票：Crypto::PartialSign(H, qc_hash) → (sign_x, sign_y)
        ├── 聚合：Crypto::ReconstructAndVerifyThresSign() → QC
        └── 验证：Crypto::VerifyQC(H, qc) → 用 common_pk 验证
```

