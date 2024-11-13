#include "contract/contract_reencryption.h"

#include "common/time_utils.h"

namespace shardora {

namespace contract {

ContractReEncryption::ContractReEncryption(const std::string& create_address, const std::string& pairing_param)
        : ContractInterface(create_address), pairing_(pairing_param) {}

ContractReEncryption::~ContractReEncryption() {}

int ContractReEncryption::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    auto btime = common::TimeUtils::TimestampMs();
    SetupParams();
    KeyGen();
    GenTk();
    Encrypt();
    Transform();
    Decrypt();
    auto etime = common::TimeUtils::TimestampMs();
    CONTRACT_DEBUG("call use time: %d ms", etime - btime);
    return kContractSuccess;
}

void ContractReEncryption::SaveData() {
    auto btime = common::TimeUtils::TimestampMs();
    SetupParams();
    KeyGen();
    GenTk();
    Encrypt();
//     Transform();
//     Decrypt();
    FILE* fd = fopen("./all_params", "w");
    fputs((std::string("o:") + common::Encode::HexEncode(o_.toString()) + "\n").c_str(), fd);
    fputs((std::string("c:") + common::Encode::HexEncode(C_.toString()) + "\n").c_str(), fd);
    fputs((std::string("c1:") + common::Encode::HexEncode(C1_.toString(true)) + "\n").c_str(), fd);
    fputs((std::string("l1:") + common::Encode::HexEncode(L1_.toString(true)) + "\n").c_str(), fd);
    fputs((std::string("r1:") + common::Encode::HexEncode(R1_.toString(true)) + "\n").c_str(), fd);
    fputs((std::string("n:") + std::to_string(count_) + "\n").c_str(), fd);
    for (int32_t i = 0; i < count_; i++) {
        fputs((std::string("c_") + std::to_string(i) + ":" + common::Encode::HexEncode(clist_[i].toString(true)) + "\n").c_str(), fd);
        fputs((std::string("d_") + std::to_string(i) + ":" + common::Encode::HexEncode(dlist_[i].toString(true)) + "\n").c_str(), fd);
        fputs((std::string("r_") + std::to_string(i) + ":" + common::Encode::HexEncode(Rx1_[i].toString(true)) + "\n").c_str(), fd);
        db_->Put("abe_c_" + std::to_string(i), clist_[i].toString(true));
        db_->Put("abe_d_" + std::to_string(i), dlist_[i].toString(true));
        db_->Put("abe_r_" + std::to_string(i), Rx1_[i].toString(true));
    }

    fclose(fd);
    db_->Put("abe_o", o_.toString());
    db_->Put("abe_c", C_.toString());
    db_->Put("abe_c1", C1_.toString(true));
    db_->Put("abe_l1", L1_.toString(true));
    db_->Put("abe_r1", R1_.toString(true));
    auto etime = common::TimeUtils::TimestampMs();
    CONTRACT_DEBUG("call use time: %d ms", etime - btime);
}

void ContractReEncryption::DecryptData() {
    std::string o_str;
    if (db_->Get("abe_o", &o_str).ok()) {
        CONTRACT_ERROR("get data o failed!");
        return;
    }

    o_ = Zr(pairing_, (void*)o_str.c_str(), o_str.size());
    std::string c_str;
    if (db_->Get("abe_c", &c_str).ok()) {
        CONTRACT_ERROR("get data c failed!");
        return;
    }

    C_ = GT(pairing_, (void*)c_str.c_str(), c_str.size());
    std::string c1_str;
    if (db_->Get("abe_c1", &c1_str).ok()) {
        CONTRACT_ERROR("get data c1 failed!");
        return;
    }

    C1_ = G1(pairing_, (void*)c1_str.c_str(), c1_str.size());
    std::string l1_str;
    if (db_->Get("abe_l1", &l1_str).ok()) {
        CONTRACT_ERROR("get data l1 failed!");
        return;
    }

    L1_ = G1(pairing_, (void*)l1_str.c_str(), l1_str.size());
    std::string r1_str;
    if (db_->Get("abe_r1", &r1_str).ok()) {
        CONTRACT_ERROR("get data l1 failed!");
        return;
    }

    R1_ = G1(pairing_, (void*)r1_str.c_str(), r1_str.size());
    count_ = 20;
    clist_.clear();
    dlist_.clear();
    Rx1_.clear();
    for (int32_t i = 0; i < count_; i++) {
        std::string c_str;
        if (db_->Get("abe_c_" + std::to_string(i), &c_str).ok()) {
            CONTRACT_ERROR("get data c failed!");
            return;
        }

        clist_.push_back(G1(pairing_, (void*)c_str.c_str(), c_str.size()));
        std::string d_str;
        if (db_->Get("abe_d_" + std::to_string(i), &d_str).ok()) {
            CONTRACT_ERROR("get data d failed!");
            return;
        }

        dlist_.push_back(G1(pairing_, (void*)d_str.c_str(), d_str.size()));
        std::string r_str;
        if (db_->Get("abe_r_" + std::to_string(i), &r_str).ok()) {
            CONTRACT_ERROR("get data d failed!");
            return;
        }

        Rx1_.push_back(G1(pairing_, (void*)r_str.c_str(), r_str.size()));
    }

    Transform();
    Decrypt();
}

int ContractReEncryption::SetupParams() {
    g_ = G1(pairing_, false);
    alpha_ = Zr(pairing_, true);
    a_ = Zr(pairing_, true);
    g1_ = GPP<G1>(pairing_, g_) ^ a_;
    auto tmp_gt = pairing_.apply(g_, g_);
    gt_ = GPP<GT>(pairing_, tmp_gt) ^ alpha_;
    for (int32_t i = 0; i < count_; ++i) {
        auto rand_zr = G1(pairing_, true);
        init_zrs_.push_back(rand_zr);
    }
    return kContractSuccess;
}

int ContractReEncryption::KeyGen() {
    msk_ = GPP<G1>(pairing_, g_) ^ alpha_;
    s_ = Zr(pairing_, true);
    auto powg = GPP<G1>(pairing_, g_) ^ (a_ * s_);
    R_ = msk_ * powg;
    L_ = GPP<G1>(pairing_, g_) ^ s_;
    for (int32_t i = 0; i < count_; ++i) {
        auto pow_zr = GPP<G1>(pairing_, init_zrs_[i]) ^ s_;
        Rx_.push_back(pow_zr);
    }

    return kContractSuccess;
}

int ContractReEncryption::GenTk() {
    o_ = Zr(pairing_, true);
    R1_ = GPP<G1>(pairing_, R_) ^ o_.inverse();
    L1_ = GPP<G1>(pairing_, L_) ^ o_.inverse();
    for (int32_t i = 0; i < count_; ++i) {
        auto pow_zr = GPP<G1>(pairing_, Rx_[i]) ^ o_.inverse();
        Rx1_.push_back(pow_zr);
    }

    return kContractSuccess;
}

int ContractReEncryption::Encrypt() {
    k_ = GT(pairing_, true);
    r_ = Zr(pairing_, true);
    for (int32_t i = 0; i < count_; ++i) {
        tlist_.push_back(Zr(pairing_, true));
        llist_.push_back(Zr(pairing_, true));
    }

    auto tmp_gt = pairing_.apply(g_, g_);
    auto tmp_gt2 = GPP<GT>(pairing_, tmp_gt) ^ alpha_ ^ r_;
    C_ = k_ * tmp_gt2;
    C1_ = GPP<G1>(pairing_, g_) ^ r_;
    for (int32_t i = 0; i < count_; ++i) {
        auto rand_zr = Zr(pairing_, true);
        auto tmp_pow = GPP<Zr>(pairing_, rand_zr) ^ tlist_[i].inverse();
        auto mul = a_ * llist_[i] * tmp_pow;
        auto c_pow = GPP<G1>(pairing_, g_) ^ mul;
        clist_.push_back(c_pow);
        auto d_pow = GPP<G1>(pairing_, g_) ^ tlist_[i];
        dlist_.push_back(d_pow);
    }

    return kContractSuccess;
}

int ContractReEncryption::Decrypt() {
    auto tmp_gt = GPP<GT>(pairing_, t_) ^ o_;
    decrypt_k_ = C_ * tmp_gt.inverse();
    return kContractSuccess;
}

int ContractReEncryption::TransformAll() {
    GT total;
    for (int32_t i = 0; i < count_; ++i) {
        auto exp_w = Zr(pairing_, true);
        auto tmp_pow1 = GPP<G1>(pairing_, clist_[i]) ^ exp_w;
        auto tmp_pow2 = GPP<G1>(pairing_, dlist_[i]) ^ exp_w;
        auto tmp_gt1 = pairing_.apply(tmp_pow1, L1_);
        auto tmp_gt = pairing_.apply(tmp_pow2, Rx1_[i]);
        auto ti = tmp_gt1 * tmp_gt;
        trans_list_.push_back(tmp_gt1 * tmp_gt);
        if (i == 0) {
            total = tmp_gt;
        } else {
            total *= tmp_gt;
        }
    }

    t_ = pairing_.apply(C1_, R1_) / total;
    return kContractSuccess;
}

int ContractReEncryption::Transform() {
//     GT total;
//     for (int32_t i = 0; i < 1; ++i) {
    int32_t i = 0;
    int32_t idx = 0;
    auto btime = common::TimeUtils::TimestampUs();
    auto exp_w = Zr(pairing_, true);
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
    btime = common::TimeUtils::TimestampUs();
    auto tmp_pow1 = GPP<G1>(pairing_, clist_[i]) ^ exp_w;
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
    btime = common::TimeUtils::TimestampUs();
    auto tmp_pow2 = GPP<G1>(pairing_, dlist_[i]) ^ exp_w;
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
    btime = common::TimeUtils::TimestampUs();
    auto tmp_gt1 = pairing_.apply(tmp_pow1, L1_);
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
    btime = common::TimeUtils::TimestampUs();
    auto tmp_gt = pairing_.apply(tmp_pow2, Rx1_[i]);
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
    btime = common::TimeUtils::TimestampUs();
    auto ti = tmp_gt1 * tmp_gt;
    used_times_[idx++] += common::TimeUtils::TimestampUs() - btime;
//         trans_list_.push_back(tmp_gt1 * tmp_gt);
//         if (i == 0) {
//             total = tmp_gt;
//         } else {
//             total *= tmp_gt;
//         }
//     }
// 
//     t_ = pairing_.apply(C1_, R1_) / total;
    return kContractSuccess;
}

int ContractReEncryption::TestProxyReEncryption() {
    std::string param = ("type a\n"
        "q 8780710799663312522437781984754049815806883199414208211028653399266475630880222957078625179422662221423155858769582317459277713367317481324925129998224791\n"
        "h 12016012264891146079388821366740534204802954401251311822919615131047207289359704531102844802183906537786776\n"
        "r 730750818665451621361119245571504901405976559617\n"
        "exp2 159\n"
        "exp1 107\n"
        "sign1 1\n"
        "sign0 1\n");
    Pairing e(param.c_str(), param.size());
    G1 g(e,false);
    G1 g1(e,false);

    //密钥生成，这里生成10个用户。
    //下标为0的用户0作为加密者，其余用户(1~9)为接受者。
    int nu = 10;
    vector<Zr> sk;
    vector<G1> pk;
    for(int i = 0;i<nu;i++){
        Zr x(e,true);
        pk.push_back(g^x);
        sk.push_back(x);
    }

    //重加密密钥生成，假设代理总数为np，门限为t。
    //即只有不少于t个代理参与重加密，才能正确解密。
    int np=10,t=4;
    vector<Zr> proxyId,coefficientsF,coefficientsH,fid,hid;
    //proxyId表示每个代理的id
    for(int i = 0;i<np;i++){
        proxyId.push_back(Zr(e,true));
    }
    //选择两个t-1阶多项式
    coefficientsF.push_back(sk[0].inverse(true));
    coefficientsH.push_back(Zr(e,(long)1));
    for (int i = 0; i < t-1; i++){
        coefficientsF.push_back(Zr(e,true));
        coefficientsH.push_back(Zr(e,true));
    }
    //计算每个f(Id)和h(Id)
    for(int i = 0;i<np;i++){
        Zr resultf(e),resulth(e); //resultf = 0
        Zr temp(e,(long int)1);
        for (int j = 0; j < t; j++){
            resultf+=coefficientsF[j]*temp;
            resulth+=coefficientsH[j]*temp;
            temp*=proxyId[i];
        }
        fid.push_back(resultf);
        hid.push_back(resulth);
    }
    //选择随机数X作为对称密钥
    vector<GT> X(nu);
    vector<G1> Hx(nu);
    for (int i = 1; i < nu; i++){
        X[i] = GT(e,false);
        Hx[i] = G1(e,X[1].getElement(),X[1].getElementSize());
    }
    //计算重加密密钥 rk=(rk1,rk2,rk3)
    //其中rk1=rk[nu][np],rk[i]表示重加密给用户i的所有重加密密钥，rk[i]的每一项在实际场景中应分发给代理
    vector<vector<G1>> rk1(nu);
    vector<G1> rk2(nu);
    vector<GT> rk3(nu);

    for(int i = 1; i < nu;i++){
        Zr r(e,true);
        rk2[i]=(g^r);
        rk3[i]=(X[i]*e(g1,pk[i]^r));
        vector<G1> tmp;
        if(i==1){
            for(int j= 0;j<np;j++){
                tmp.push_back((Hx[i]^hid[j])*(g1^fid[j]));
            }
        }
        else{
            for(int j= 0;j<np;j++){
                tmp.push_back((Hx[i]/Hx[i-1])^hid[j]);
            }
        }
        rk1[i]=(tmp);
    }
    
    //用户0使用自己的公钥pk0加密消息m
    //c=(c1,c2,c3,c4,c5,c6),每项又包含np个，用于分发给np个代理
    vector<G1> c1,c3,c5;
    vector<GT> c2,c4,c6;
    GT m(e,false);
    m.dump(stdout,"加密消息为");
    Zr r(e,true),z(e,true);
    for(int i = 0;i<np;i++){
        c1.push_back(g^r);
        c2.push_back((m*(e(g1,pk[0])^r))^hid[i]);
        c3.push_back(g^z);
        c4.push_back(e(g1,pk[0])^(r*hid[i]));
        c5.push_back(G1(e,false));
        c6.push_back(GT(e,false));
    }


    //初始密文的解密如下(为了方便，选前t个碎片解密)
    vector<Zr> lag;
    for(int i = 0;i<t;i++){
        Zr result(e,(long)1);
        //拉格朗日差值
        for(int j=0;j<t;j++){
            if(proxyId[j]==proxyId[i]){
                continue;
            }
            result*=(proxyId[j]/(proxyId[j]-proxyId[i]));
        }
        lag.push_back(result);
    }

    GT tempc2(c2[0]^lag[0]);
    for(int i = 1;i<t;i++){
        tempc2*=(c2[i]^lag[i]);
    }
    GT result1(tempc2/e(c1[0],g1^sk[0]));
    result1.dump(stdout,"初始密文解密结果为");
    if(m==result1){
        ZJC_DEBUG("init encryption data success: %d", 0);
    }else{
        ZJC_DEBUG("init encryption data failed: %d", 0);
    }

    //重加密（随即t个代理执行，这里为了方便就取前t个）
    //reenc-c=(rc1,rc2,rc3,rc4,rc5,rc6)
    //其中每一项 rc的第一个下标表示接收者编号，第二个下标表示分发给的代理编号
    //注意到，不论是初始密文还是重加密密文，都可进行重加密操作。
    vector<vector<G1>> rc1,rc3,rc5;
    vector<vector<GT>> rc2,rc4,rc6;
    //有nu-1个接受者，则需重加密nu-1次
    for(int i = 1;i<nu;i++){
        //在实际应用中，这里的两个随机数需要使用分布式随机数（密钥）协商算法。
        //例如每个代理都向其他代理发送w1i和w2i，每个代理接收后都做累加得到w1和w2。
        Zr w1(e,true),w2(e,true);
        vector<G1> tmp1,tmp3,tmp5;
        vector<GT> tmp2,tmp4,tmp6;
        for(int j= 0;j<t;j++){
            tmp1.push_back(c1[j]*(c3[j]^w1));
            tmp3.push_back(c3[j]^w2);
            tmp2.push_back(c2[j]*(c4[j]^w1)*e(tmp1[j],rk1[i][j]));
            tmp4.push_back(c4[j]*e(c3[j],rk1[i][j]));
            tmp5.push_back(rk2[i]);
            tmp6.push_back(rk3[i]);
        }
        rc1.push_back(tmp1);
        rc2.push_back(tmp2);
        rc3.push_back(tmp3);
        rc4.push_back(tmp4);
        rc5.push_back(tmp5);
        rc6.push_back(tmp6);
    }
    

    //重加密密文的解密如下(为了方便，选前t个碎片解密)
    for(int i = 1;i<nu;i++){
        GT Xi = rc6[i-1][0]/e(g1^sk[i],rc5[i-1][0]);
        GT tempc2(rc2[i-1][0]^lag[0]);
        for(int j=0;j<t;j++){
            tempc2*=(rc2[i-1][j]^lag[j]);
        }

        GT result2=tempc2/e(rc1[i-1][0],G1(e,Xi.getElement(),Xi.getElementSize()));
        if(m==result1){
            ZJC_DEBUG("t member decrypt success: %d", i);
        }else{
            ZJC_DEBUG("t member decrypt failed: %d", i);
        }
    }
}


}  // namespace contract

}  // namespace shardora
