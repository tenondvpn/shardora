#include "contract/contract_reencryption.h"

#include "common/split.h"
#include "common/time_utils.h"
#include "zjcvm/zjc_host.h"

namespace shardora {

namespace contract {

ContractReEncryption::~ContractReEncryption() {}

int ContractReEncryption::call(
        const CallParameters& param,
        uint64_t gas,
        const std::string& origin_address,
        evmc_result* res) {
    auto btime = common::TimeUtils::TimestampMs();
    auto etime = common::TimeUtils::TimestampMs();
    CONTRACT_DEBUG("call use time: %d ms", etime - btime);
    return kContractSuccess;
}

int ContractReEncryption::CreatePrivateAndPublicKeys(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto& e = *pairing_ptr_;
    std::string g_str(common::Encode::HexDecode("92d2c563c4dd82a51ab97ac85b17055e06e222671ab21290c6126be096475699c766bee1fcae94e6baaa9c6694b9a03ca0205d044878c8996fec96bef10df61001"));
    G1 g(e, g_str.c_str(), g_str.size());
    std::string g1_str(common::Encode::HexDecode("7c8ae882453932ed180735e6eef3c983c93e0501dcfe6a1230fbfea4ac95f4c22795fe5a8137549d1a1b7427519b189431e794e365be5910fcd8e1c91bbc67fa00"));
    G1 g1(e, g1_str.c_str(), g1_str.size());
    ZJC_DEBUG("create member g and g1 g: %s, g1: %s",
        common::Encode::HexEncode(g.toString(true)).c_str(),
        common::Encode::HexEncode(g1.toString(true)).c_str());
    //密钥生成，这里生成10个用户。
    //下标为0的用户0作为加密者，其余用户(1~9)为接受者。
    vector<Zr> sk;
    vector<G1> pk;
    auto sk_splits = common::Split<>(value.c_str(), ',');
    int nu = sk_splits.Count();
    for(int i = 0;i<nu;i++){
        auto sk_str = common::Encode::HexDecode(sk_splits[i]);
        Zr x(e, sk_splits[i], sk_splits.SubLen(i));
        sk.push_back(x);
        auto tmp_pk = g^x;
        pk.push_back(tmp_pk);
        ZJC_DEBUG("create member private and public key: %d, sk: %s, pk: %s",
            i, common::Encode::HexEncode(x.toString()).c_str(),
            common::Encode::HexEncode(tmp_pk.toString(true)).c_str());
        auto private_key = std::string("init_prikey_") + std::to_string(i);
        param.zjc_host->SaveKeyValue(param.from, private_key, x.toString());
        auto public_key = std::string("init_pubkey_") + std::to_string(i);
        param.zjc_host->SaveKeyValue(param.from, public_key, tmp_pk.toString(true));
    }

    return kContractSuccess;
}

int ContractReEncryption::CreateReEncryptionKeys(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto& e = *pairing_ptr_;
    G1 g(e,false);
    G1 g1(e,false);

    //密钥生成，这里生成10个用户。
    //下标为0的用户0作为加密者，其余用户(1~9)为接受者。
    int nu = 10;
    vector<Zr> sk;
    vector<G1> pk;
    for(int i = 0;i<nu;i++){
        auto private_key = std::string("init_prikey_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, private_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        Zr x(e, val.c_str(), val.size());
        sk.push_back(x);
        auto public_key = std::string("init_pubkey_") + std::to_string(i);
        if (param.zjc_host->GetKeyValue(param.from, public_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        G1 tmp_pk(e, val.c_str(), val.size());
        pk.push_back(tmp_pk);
    }

    //重加密密钥生成，假设代理总数为np，门限为t。
    //即只有不少于t个代理参与重加密，才能正确解密。
    int np=10,t=4;
    vector<Zr> proxyId,coefficientsF,coefficientsH,fid,hid;
    //proxyId表示每个代理的id
    for(int i = 0;i<np;i++){
        auto tmp_proxy_id = Zr(e,true);
        proxyId.push_back(tmp_proxy_id);
        ZJC_DEBUG("create member proxy id: %d, proxy_id: %s",
            i, common::Encode::HexEncode(tmp_proxy_id.toString()).c_str());
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
        ZJC_DEBUG("create member hid: %d, hid: %s", i, common::Encode::HexEncode(resulth.toString()).c_str());
    }

    //选择随机数X作为对称密钥
    vector<GT> X(nu);
    vector<G1> Hx(nu);
    for (int i = 1; i < nu; i++){
        X[i] = GT(e,false);
        Hx[i] = G1(e,X[i].toString().c_str(),X[i].getElementSize());//GT到G1的哈希
    }

    //计算重加密密钥 rk=(rk1,rk2,rk3)
    //其中rk1=rk[nu][np],rk[i]表示重加密给用户i的所有重加密密钥，rk[i]的每一项在实际场景中应分发给代理
    vector<vector<G1>> rk1;
    vector<G1> rk2;
    vector<GT> rk3;
    for(int i = 1; i < nu;i++){
        Zr r(e,true);
        auto tmp_rk2 = g^r;
        rk2.push_back(tmp_rk2);
        ZJC_DEBUG("create member rk2: %d, rk2: %s", i, common::Encode::HexEncode(tmp_rk2.toString(true)).c_str());
        auto tmp_rk3 = X[i]*e(g1,pk[i]^r);
        rk3.push_back(tmp_rk3);
        ZJC_DEBUG("create member rk3: %d, rk3: %s", i, common::Encode::HexEncode(tmp_rk3.toString()).c_str());
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

        for (int32_t tmp_idx = 0; tmp_idx < tmp.size(); ++tmp_idx) {
            ZJC_DEBUG("create member rk1: %d, %d, rk1: %s",
                i, tmp_idx, common::Encode::HexEncode(tmp[tmp_idx].toString(true)).c_str());
        }

        rk1.push_back(tmp);
    }

    return kContractSuccess;
}

int ContractReEncryption::EncryptUserMessage(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto& e = *pairing_ptr_;
    G1 g(e,false);
    G1 g1(e,false);

    //密钥生成，这里生成10个用户。
    //下标为0的用户0作为加密者，其余用户(1~9)为接受者。
    int nu = 10;
    vector<Zr> sk;
    vector<G1> pk;
    for(int i = 0;i<nu;i++){
        auto private_key = std::string("init_prikey_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, private_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        Zr x(e, val.c_str(), val.size());
        sk.push_back(x);
        auto public_key = std::string("init_pubkey_") + std::to_string(i);
        if (param.zjc_host->GetKeyValue(param.from, public_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        G1 tmp_pk(e, val.c_str(), val.size());
        pk.push_back(tmp_pk);
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
        Hx[i] = G1(e,X[i].toString().c_str(),X[i].getElementSize());//GT到G1的哈希
    }
    //计算重加密密钥 rk=(rk1,rk2,rk3)
    //其中rk1=rk[nu][np],rk[i]表示重加密给用户i的所有重加密密钥，rk[i]的每一项在实际场景中应分发给代理
    vector<vector<G1>> rk1;
    vector<G1> rk2;
    vector<GT> rk3;

    for(int i = 1; i < nu;i++){
        Zr r(e,true);
        rk2.push_back(g^r);
        rk3.push_back(X[i]*e(g1,pk[i]^r));
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
        rk1.push_back(tmp);
    }
    
    //用户0使用自己的公钥pk0加密消息m
    //c=(c1,c2,c3,c4,c5,c6),每项又包含np个，用于分发给np个代理
    vector<G1> c1,c3,c5;
    vector<GT> c2,c4,c6;
    std::string test_data = "hello world!";
    GT m(e,test_data.c_str(), test_data.size());
    m.dump(stdout,"加密消息为");
    Zr r(e,true),z(e,true);
    for(int i = 0;i<np;i++){
        auto tmp_c1 = g^r;
        auto tmp_c2 = g^z;
        auto tmp_c3 = (m*(e(g1,pk[0])^r))^hid[i];
        auto tmp_c4 = e(g1,pk[0])^(z*hid[i]);
        auto tmp_c5 = G1(e,false);
        auto tmp_c6 = GT(e,false);
        c1.push_back(tmp_c1);
        c3.push_back(tmp_c2);
        c2.push_back(tmp_c3);
        c4.push_back(tmp_c4);
        c5.push_back(tmp_c5);
        c6.push_back(tmp_c6);
        ZJC_DEBUG("create member %d c1: %s, c2: %s, c3: %s, c4: %s, c5: %s, c6: %s",
                i, common::Encode::HexEncode(tmp_c1.toString(true)).c_str(),
                common::Encode::HexEncode(tmp_c2.toString(true)).c_str(),
                common::Encode::HexEncode(tmp_c3.toString()).c_str(),
                common::Encode::HexEncode(tmp_c4.toString()).c_str(),
                common::Encode::HexEncode(tmp_c5.toString(true)).c_str(),
                common::Encode::HexEncode(tmp_c6.toString()).c_str());
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
        ZJC_DEBUG("create member lag: %d, lag: %s", i, common::Encode::HexEncode(result.toString()).c_str());
    }

    GT tempc2(c2[0]^lag[0]);
    for(int i = 1;i<t;i++){
        tempc2*=(c2[i]^lag[i]);
    }

    GT result1(tempc2/e(c1[0],g1^sk[0]));
    if(m==result1){
        ZJC_DEBUG("user encryption success: %s", common::Encode::HexEncode(result1.toString()).c_str());
    }else{
        ZJC_DEBUG("user encryption failed: %s, %s", common::Encode::HexEncode(m.toString()).c_str(), common::Encode::HexEncode(result1.toString()).c_str());
    }

    return kContractSuccess;
}

int ContractReEncryption::ReEncryptUserMessage(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto& e = *pairing_ptr_;
    G1 g(e,false);
    G1 g1(e,false);

    //密钥生成，这里生成10个用户。
    //下标为0的用户0作为加密者，其余用户(1~9)为接受者。
    int nu = 10;
    vector<Zr> sk;
    vector<G1> pk;
    for(int i = 0;i<nu;i++){
        auto private_key = std::string("init_prikey_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, private_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        Zr x(e, val.c_str(), val.size());
        sk.push_back(x);
        auto public_key = std::string("init_pubkey_") + std::to_string(i);
        if (param.zjc_host->GetKeyValue(param.from, public_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        G1 tmp_pk(e, val.c_str(), val.size());
        pk.push_back(tmp_pk);
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
        Hx[i] = G1(e,X[i].toString().c_str(),X[i].getElementSize());//GT到G1的哈希
    }
    //计算重加密密钥 rk=(rk1,rk2,rk3)
    //其中rk1=rk[nu][np],rk[i]表示重加密给用户i的所有重加密密钥，rk[i]的每一项在实际场景中应分发给代理
    vector<vector<G1>> rk1;
    vector<G1> rk2;
    vector<GT> rk3;

    for(int i = 1; i < nu;i++){
        Zr r(e,true);
        rk2.push_back(g^r);
        rk3.push_back(X[i]*e(g1,pk[i]^r));
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
        rk1.push_back(tmp);
    }
    
    //用户0使用自己的公钥pk0加密消息m
    //c=(c1,c2,c3,c4,c5,c6),每项又包含np个，用于分发给np个代理
    vector<G1> c1,c3,c5;
    vector<GT> c2,c4,c6;
    std::string test_data = "hello world!";
    GT m(e,test_data.c_str(), test_data.size());
    m.dump(stdout,"加密消息为");
    Zr r(e,true),z(e,true);
    for(int i = 0;i<np;i++){
        c1.push_back(g^r);
        c3.push_back(g^z);
        c2.push_back((m*(e(g1,pk[0])^r))^hid[i]);
        c4.push_back(e(g1,pk[0])^(z*hid[i]));
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
    cout<<"是否成功解密？"<<endl;
    if(m==result1){
        cout<<"Success!"<<endl;
    }else{
        cout<<"Fail"<<endl;
    }

    //重加密（随即t个代理执行，这里为了方便就取前t个）
    //注意到，不论是初始密文还是重加密密文，都可进行重加密操作。
    //reenc-c=(rc1,rc2,rc3,rc4,rc5,rc6)
    //其中每一项 rc的第一个下标表示接收者编号，第二个下标表示分发给的代理编号
    //rc[nu][t],rc[i]表示用户i的重加密密文，rc[i][k]表示代理k发送给用户i的密文
    vector<vector<G1>> rc1,rc3,rc5;
    vector<vector<GT>> rc2,rc4,rc6;
    rc1.push_back(c1);
    rc2.push_back(c2);
    rc3.push_back(c3);
    rc4.push_back(c4);
    rc5.push_back(c5);
    rc6.push_back(c6);
    //有nu-1个接受者，则需重加密nu-1次
    for(int i = 1;i<nu;i++){
        //在实际应用中，这里的两个随机数需要使用分布式随机数（密钥）协商算法。
        //例如每个代理都向其他代理发送w1i和w2i，每个代理接收后都做累加得到w1和w2。
        Zr w1(e,true),w2(e,true);
        vector<G1> tmp1,tmp3,tmp5;
        vector<GT> tmp2,tmp4,tmp6;
        for(int j= 0;j<t;j++){
            tmp1.push_back(rc1[i-1][j]*(rc3[i-1][j]^w1));
            tmp3.push_back(rc3[i-1][j]^w2);
            tmp2.push_back(rc2[i-1][j]*(rc4[i-1][j]^w1)*e(tmp1.back(),rk1[i-1][j]));
            tmp4.push_back((rc4[i-1][j]*e(rc3[i-1][j],rk1[i-1][j]))^w2);
        }
        tmp5.push_back(rk2[i-1]);
        tmp6.push_back(rk3[i-1]);
        rc1.push_back(tmp1);
        rc2.push_back(tmp2);
        rc3.push_back(tmp3);
        rc4.push_back(tmp4);
        rc5.push_back(tmp5);
        rc6.push_back(tmp6);
        for (int32_t tmp_idx = 0; tmp_idx < tmp1.size(); ++tmp_idx) {
            ZJC_DEBUG("create member reenc data: %d, %d, tmp1: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp1[tmp_idx].toString(true)).c_str());
        }

        for (int32_t tmp_idx = 0; tmp_idx < tmp2.size(); ++tmp_idx) {
            ZJC_DEBUG("create member reenc data: %d, %d, tmp2: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp2[tmp_idx].toString()).c_str());
        }

        for (int32_t tmp_idx = 0; tmp_idx < tmp3.size(); ++tmp_idx) {
            ZJC_DEBUG("create member reenc data: %d, %d, tmp3: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp3[tmp_idx].toString(true)).c_str());
        }

        for (int32_t tmp_idx = 0; tmp_idx < tmp4.size(); ++tmp_idx) {
            ZJC_DEBUG("create member reenc data: %d, %d, tmp4: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp4[tmp_idx].toString()).c_str());
        }

        for (int32_t tmp_idx = 0; tmp_idx < tmp5.size(); ++tmp_idx) {
            ZJC_DEBUG("create member reenc data: %d, %d, tmp5: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp5[tmp_idx].toString(true)).c_str());
        }

        for (int32_t tmp_idx = 0; tmp_idx < tmp6.size(); ++tmp_idx) {
            ZJC_DEBUG("create member reenc data: %d, %d, tmp6: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp6[tmp_idx].toString()).c_str());
        }
    }

    return kContractSuccess;
}


int ContractReEncryption::Decryption(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto& e = *pairing_ptr_;
    G1 g(e,false);
    G1 g1(e,false);

    //密钥生成，这里生成10个用户。
    //下标为0的用户0作为加密者，其余用户(1~9)为接受者。
    int nu = 10;
    vector<Zr> sk;
    vector<G1> pk;
    for(int i = 0;i<nu;i++){
        auto private_key = std::string("init_prikey_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, private_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        Zr x(e, val.c_str(), val.size());
        sk.push_back(x);
        auto public_key = std::string("init_pubkey_") + std::to_string(i);
        if (param.zjc_host->GetKeyValue(param.from, public_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        G1 tmp_pk(e, val.c_str(), val.size());
        pk.push_back(tmp_pk);
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
        Hx[i] = G1(e,X[i].toString().c_str(),X[i].getElementSize());//GT到G1的哈希
    }
    //计算重加密密钥 rk=(rk1,rk2,rk3)
    //其中rk1=rk[nu][np],rk[i]表示重加密给用户i的所有重加密密钥，rk[i]的每一项在实际场景中应分发给代理
    vector<vector<G1>> rk1;
    vector<G1> rk2;
    vector<GT> rk3;

    for(int i = 1; i < nu;i++){
        Zr r(e,true);
        rk2.push_back(g^r);
        rk3.push_back(X[i]*e(g1,pk[i]^r));
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
        rk1.push_back(tmp);
    }
    
    //用户0使用自己的公钥pk0加密消息m
    //c=(c1,c2,c3,c4,c5,c6),每项又包含np个，用于分发给np个代理
    vector<G1> c1,c3,c5;
    vector<GT> c2,c4,c6;
    std::string test_data = "hello world!";
    GT m(e,test_data.c_str(), test_data.size());
    m.dump(stdout,"加密消息为");
    Zr r(e,true),z(e,true);
    for(int i = 0;i<np;i++){
        c1.push_back(g^r);
        c3.push_back(g^z);
        c2.push_back((m*(e(g1,pk[0])^r))^hid[i]);
        c4.push_back(e(g1,pk[0])^(z*hid[i]));
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
    cout<<"是否成功解密？"<<endl;
    if(m==result1){
        cout<<"Success!"<<endl;
    }else{
        cout<<"Fail"<<endl;
    }

    //重加密（随即t个代理执行，这里为了方便就取前t个）
    //注意到，不论是初始密文还是重加密密文，都可进行重加密操作。
    //reenc-c=(rc1,rc2,rc3,rc4,rc5,rc6)
    //其中每一项 rc的第一个下标表示接收者编号，第二个下标表示分发给的代理编号
    //rc[nu][t],rc[i]表示用户i的重加密密文，rc[i][k]表示代理k发送给用户i的密文
    vector<vector<G1>> rc1,rc3,rc5;
    vector<vector<GT>> rc2,rc4,rc6;
    rc1.push_back(c1);
    rc2.push_back(c2);
    rc3.push_back(c3);
    rc4.push_back(c4);
    rc5.push_back(c5);
    rc6.push_back(c6);
    //有nu-1个接受者，则需重加密nu-1次
    for(int i = 1;i<nu;i++){
        //在实际应用中，这里的两个随机数需要使用分布式随机数（密钥）协商算法。
        //例如每个代理都向其他代理发送w1i和w2i，每个代理接收后都做累加得到w1和w2。
        Zr w1(e,true),w2(e,true);
        vector<G1> tmp1,tmp3,tmp5;
        vector<GT> tmp2,tmp4,tmp6;
        for(int j= 0;j<t;j++){
            tmp1.push_back(rc1[i-1][j]*(rc3[i-1][j]^w1));
            tmp3.push_back(rc3[i-1][j]^w2);
            tmp2.push_back(rc2[i-1][j]*(rc4[i-1][j]^w1)*e(tmp1.back(),rk1[i-1][j]));
            tmp4.push_back((rc4[i-1][j]*e(rc3[i-1][j],rk1[i-1][j]))^w2);
        }
        tmp5.push_back(rk2[i-1]);
        tmp6.push_back(rk3[i-1]);
        rc1.push_back(tmp1);
        rc2.push_back(tmp2);
        rc3.push_back(tmp3);
        rc4.push_back(tmp4);
        rc5.push_back(tmp5);
        rc6.push_back(tmp6);
    }

    // 重加密密文的解密如下(为了方便，选前t个碎片解密)
    for(int i = 1;i<nu;i++){
        GT Xi = rc6[i][0]/e(g1^sk[i],rc5[i][0]);
        GT tempc2(rc2[i][0]^lag[0]);
        for(int j=1;j<t;j++){
            tempc2*=(rc2[i][j]^lag[j]);
        }
        GT result2=tempc2/e(rc1[i][0],G1(e,Xi.toString().c_str(),Xi.getElementSize()));
        if(m==result2){
            ZJC_DEBUG("user %d success.", i);
        }else{
            ZJC_DEBUG("user %d failed.", i);
        }
    }
}

}  // namespace contract

}  // namespace shardora
