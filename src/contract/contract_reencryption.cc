#include "contract/contract_reencryption.h"

#include "common/split.h"
#include "common/string_utils.h"
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
    //密钥生成，这里生成10个用户。
    //下标为0的用户0作为加密者，其余用户(1~9)为接受者。
    auto lines = common::Split<>(value.c_str(), ';');
    if (lines.Count() != 2) {
        ZJC_WARN("failed!");
        return kContractError;
    }

    std::string id(lines[0]);
    vector<Zr> sk;
    vector<G1> pk;
    auto sk_splits = common::Split<>(lines[1], ',');
    int nu = sk_splits.Count();
    for(int i = 0;i<nu;i++){
        auto sk_str = common::Encode::HexDecode(sk_splits[i]);
        Zr x(e, sk_splits[i], sk_splits.SubLen(i));
        sk.push_back(x);
        auto tmp_pk = g^x;
        pk.push_back(tmp_pk);
        auto private_key = id + "_" + std::string("init_prikey_") + std::to_string(i);
        auto public_key = id + "_" + std::string("init_pubkey_") + std::to_string(i);
        ZJC_WARN("create member private and public key: %s, %s sk: %s, pk: %s",
            private_key.c_str(), public_key.c_str(), common::Encode::HexEncode(x.toString()).c_str(),
            common::Encode::HexEncode(tmp_pk.toString(true)).c_str());
        param.zjc_host->SaveKeyValue(param.from, private_key, x.toString());
        param.zjc_host->SaveKeyValue(param.from, public_key, tmp_pk.toString(true));
    }

    std::string reenc_value = id+";468a50340bc016c5161df8a40dd7890a84132750,204be12c7d5a77f4cecca96aeb1aadd0dc11e829,4d84890349d26fa23fb94ee32d16a4a522025072,4855a79cbcfc3d54cd99d504872beee01c8f9096,543cf9eec69613a4a01c28ebe64e50fbe234b57f,026047a338ee52e99f611bd02e9e5b12ecb83b74,35c95955d7bca26ccea47b3bf1aacc5936b53c5a,5e1694b4226bd0e1b75b71a249d6126c87d14a9e,48d9915add7e5bf58e5ad8adef850fb899c3d6ea,166ab66ee71d78d357c2c69697267c30fb820c65\n578997ffbf36d620eabeed6c6462090bf4850063,1ca9d6839beec8a8ae44aa717b217ea49929333d,6f20963912e7b78aba3a27850a53e2d2898d156d\n77f1868888fea3dc4cf479b9fed840c50e7b80e6,1c4bdf20371da32c30499e0e46438dcd0f829c21,3029b3818313522b65700c0bfaffe8741acfd1de\n4544042ac99c72c0740f78945b3842d90e362da3ce950cd40daa73da7651ab2908318301af471419fbd37334b8514a7031f70761bbf5b3755c4221cf7147ec1153066cef10869dff1ab7f6d39043dbc8287f06f6588e9418b3f253e501ae3881daf1a54363a37c5f4a3a9ba8b4a5cdfeea0993aef11fb0187ebb57a18eec290a,3b5987fc4a0a333f36376062f4b917560901e80e765b908a982bb33d276eb8fdacbfeffb63baefb81559c032d47e0afc1c4d70c4785cef138027f296c4585b17500b95dcb5a1e67458dbefb741f1ecd9fb100e50bd6ece1fe23714238d468a92eafaaa1c7ac6d2f6e722480b5729eff536c6e5d2262c5be3cc4f2c469fa8e2ea,8b94d276adea01a630b195216a210bdda9e4a07929d3a3c3d5660bc3a9726f75d2ba89bf64089b505562ac0c3a6f81e28936ddd235eaf185bd63c6e7ccd459187094a3f5e2c2a2b36b01ba8f6a072194cb276f3700440c972aa6b194c150a2a5e0c72f1df1689a01f60b42c270e2bfbbf6ced7edfd677816aadb4d8a5da6e72a,849ff0d2ffabf448864463b78d7de061e0142cbe12131b7f944c6df368c8d3223067e68f1f555b995c84320466b43f4dbef933ef201b433cd97972f371113f3a4adedaaec087673ab079bf217e3ee54bf7fa03435addb2262a99f04ef203b99108bcee8555189b543fb353998345e8f20b9f1a25f92d0ef501338ed28dbb27f0,3850f3d6b6e8305c7bf536e024843d8f9170660e514bae7342152aa14e07eb3b75f462d6226c34e00398e48bfd9265a07684a9ddb5bba7c8010a029f25784b992a0559ec761f0d72728ad37df2c067cff8c0b1f33516cd1b5f2a01def52abd782f114e545725dc8a79be4453552c7b99b8c71298661d6a2e17e0e7858f2c4d51,1e8b6f9ecc6c317d51923ff1a730365237115c99cb573f19241e7ae3938a817df95d45ccd7fcbea2d87c97b2a1be265e43a5a270913d1c14dfa19866ccd55bc04889db7f30cb04f210c73520ee72175b7c26cf9324a55755e5f4dc6eab57b9d0c8a9ee538adf84880dc0f5d181fb15f352cade261cf4a6c0376e4144399275c6,7a669e8d8a9cbb185626b0af4eb467cb86ee1e5393165544715e7486198394319e1b8790335ff5949d17a4054974e84e703c85b7bbfc1e577e2feeaaeaf16acb83ba19a36403aab7f796788fa31546a72e5b7b4d419b49a1fa63757791390d3327debb38e7b6fcc80df28c67c119368b57e989bb1bc7ea667b642f1f71b7181b,a008e91fc6085a0386b0076edc3796a2a0c431e5f946556ae63c9a173a47fe08eae37b9fce61c05138c4a04de7fe024afdacc556f9d0fbb5020c03ae8b5173de4b4933b9ce0c611bd0ce6eb6b68947e12b5c7a4e93febc1b880c6e9e917aeebd9da5e3f9251f83150d2b89654f8631ed0e7f53ccc67dbfc2cb495747391ca550,4749b5b7cfb6c8e9f7418615a53d94b582cc873a8e586ac06837885f22d44bdca39ff8109b19372481b7c58cc7dd91958abd20b26eaffb38006c8c1874714e2615403ad7b65ebd01c98be53382f77c25c69a8fd2e0ca490271a77ee2118bd541223a73b45b9ea26109fb5274360aa276437271c1a9a78e5b026ed63aec01146d\n4eb24680d02ab2ba45ef78e8b425f7c94eb662d0,3545ad3b3fe0c2f4c08a900eae3d03c87969ba6c,336293b77152b9e4d718481c6153c2d4369561b5,578e3eb7048153b05577276a0b1c1059140be7c9,604e8223208efb81daa9778237fa75fc22344a1c,138eb8b0d8c6073a080179935b8b9b92d00c589b,0f2ea3cf55743836fbbf9a5756a524be3adb1c78,247922d501351bfea6ab14bab95cf0cf912ef84e,78cddbd9d4299c953cfa839feac915e0bcdf7bce";
    CreateReEncryptionKeys(param, key, reenc_value);
    return kContractSuccess;
}

int ContractReEncryption::CreateReEncryptionKeys(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto& e = *pairing_ptr_;
    std::string g_str(common::Encode::HexDecode("92d2c563c4dd82a51ab97ac85b17055e06e222671ab21290c6126be096475699c766bee1fcae94e6baaa9c6694b9a03ca0205d044878c8996fec96bef10df61001"));
    G1 g(e, g_str.c_str(), g_str.size());
    std::string g1_str(common::Encode::HexDecode("7c8ae882453932ed180735e6eef3c983c93e0501dcfe6a1230fbfea4ac95f4c22795fe5a8137549d1a1b7427519b189431e794e365be5910fcd8e1c91bbc67fa00"));
    G1 g1(e, g1_str.c_str(), g1_str.size());

    auto lines = common::Split<>(value.c_str(), ';');
    if (lines.Count() != 2) {
        ZJC_WARN("failed!");
        return kContractError;
    }

    std::string id(lines[0]);
    //密钥生成，这里生成10个用户。
    //下标为0的用户0作为加密者，其余用户(1~9)为接受者。
    int nu = 10;
    vector<Zr> sk;
    vector<G1> pk;
    for(int i = 0;i<nu;i++){
        auto private_key = id + "_" + std::string("init_prikey_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, private_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        Zr x(e, val.c_str(), val.size());
        sk.push_back(x);
        auto public_key = id + "_" + std::string("init_pubkey_") + std::to_string(i);
        if (param.zjc_host->GetKeyValue(param.from, public_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        G1 tmp_pk(e, (const unsigned char*)val.c_str(), val.size(), true, 0);
        pk.push_back(tmp_pk);
    }

    //重加密密钥生成，假设代理总数为np，门限为t。
    //即只有不少于t个代理参与重加密，才能正确解密。
    auto line_split = common::Split<>(lines[1], '\n');
    if (line_split.Count() < 5) {
        ZJC_WARN("failed!");
        return kContractError;
    }

    auto sk_splits = common::Split<>(line_split[0], ',');
    auto f_splits = common::Split<>(line_split[1], ',');
    auto h_splits = common::Split<>(line_split[2], ',');
    int np = sk_splits.Count();
    int t=f_splits.Count();
    vector<Zr> proxyId,coefficientsF,coefficientsH,fid,hid;
    //proxyId表示每个代理的id
    for(int i = 0;i<np;i++){
        auto tmp_pid_str = common::Encode::HexDecode(sk_splits[i]);
        auto tmp_proxy_id = Zr(e, tmp_pid_str.c_str(), tmp_pid_str.size());
        proxyId.push_back(tmp_proxy_id);
        ZJC_WARN("create member proxy id: %d, proxy_id: %s",
            i, common::Encode::HexEncode(tmp_proxy_id.toString()).c_str());
        auto key = id + "_" + std::string("create_renc_key_proxyid_") + std::to_string(i);
        param.zjc_host->SaveKeyValue(param.from, key, tmp_proxy_id.toString());
    }

    //选择两个t-1阶多项式
    coefficientsF.push_back(sk[0].inverse(true));
    coefficientsH.push_back(Zr(e,(long)1));
    for (int i = 0; i < t-1; i++){
        auto tmp_f_str = common::Encode::HexDecode(f_splits[i]);
        auto tmp_h_str = common::Encode::HexDecode(h_splits[i]);
        auto tmp_f = Zr(e, tmp_f_str.c_str(), tmp_f_str.size());
        auto tmp_h = Zr(e, tmp_h_str.c_str(), tmp_h_str.size());
        coefficientsF.push_back(tmp_f);
        coefficientsH.push_back(tmp_h);
        ZJC_WARN("create member private and public key: %d, f: %s, h: %s",
            i, common::Encode::HexEncode(tmp_f.toString()).c_str(),
            common::Encode::HexEncode(tmp_h.toString()).c_str());
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
        ZJC_WARN("create member hid: %d, hid: %s", i, common::Encode::HexEncode(resulth.toString()).c_str());
        auto key = id + "_" + std::string("create_renc_key_hid_") + std::to_string(i);
        param.zjc_host->SaveKeyValue(param.from, key, resulth.toString());
    }

    //选择随机数X作为对称密钥
    vector<GT> X(nu);
    vector<G1> Hx(nu);
    auto x_splits = common::Split<>(line_split[3], ',');
    for (int i = 1; i < nu; i++){
        auto x_str = common::Encode::HexDecode(x_splits[i - 1]);
        X[i] = GT(e, x_str.c_str(), x_str.size());
        Hx[i] = G1(e,X[i].toString().c_str(),X[i].getElementSize());//GT到G1的哈希
        ZJC_WARN("create member hid: %d, Xi: %s", i, common::Encode::HexEncode(X[i].toString()).c_str());
    }

    //计算重加密密钥 rk=(rk1,rk2,rk3)
    //其中rk1=rk[nu][np],rk[i]表示重加密给用户i的所有重加密密钥，rk[i]的每一项在实际场景中应分发给代理
    vector<vector<G1>> rk1;
    vector<G1> rk2;
    vector<GT> rk3;
    auto rk_splits = common::Split<>(line_split[4], ',');
    for(int i = 1; i < nu;i++){
        auto rk_str = common::Encode::HexDecode(rk_splits[i - 1]);
        Zr r(e, rk_str.c_str(), rk_str.size());
        ZJC_WARN("create member hid: %d, RKi: %s", i, common::Encode::HexEncode(r.toString()).c_str());
        auto tmp_rk2 = g^r;
        rk2.push_back(tmp_rk2);
        ZJC_WARN("create member rk2: %d, rk2: %s", i, common::Encode::HexEncode(tmp_rk2.toString(true)).c_str());
        auto rk2_key = id + "_" + std::string("create_renc_key_rk2_") + std::to_string(i);
        param.zjc_host->SaveKeyValue(param.from, rk2_key, tmp_rk2.toString(true));

        auto tmp_rk3 = X[i]*e(g1,pk[i]^r);
        rk3.push_back(tmp_rk3);
        ZJC_WARN("create member rk3: %d, rk3: %s", i, common::Encode::HexEncode(tmp_rk3.toString()).c_str());
        auto rk3_key = id + "_" + std::string("create_renc_key_rk3_") + std::to_string(i);
        param.zjc_host->SaveKeyValue(param.from, rk3_key, tmp_rk3.toString());
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

        for (uint32_t tmp_idx = 0; tmp_idx < tmp.size(); ++tmp_idx) {
            ZJC_WARN("create member rk1: %d, %d, rk1: %s",
            i, tmp_idx, common::Encode::HexEncode(tmp[tmp_idx].toString(true)).c_str());
            auto key = id + "_" + std::string("create_renc_key_rk1_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp[tmp_idx].toString(true));
        }

        rk1.push_back(tmp);
    }

    return kContractSuccess;
}

int ContractReEncryption::EncryptUserMessage(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) try {
    auto& e = *pairing_ptr_;
    std::string g_str(common::Encode::HexDecode("92d2c563c4dd82a51ab97ac85b17055e06e222671ab21290c6126be096475699c766bee1fcae94e6baaa9c6694b9a03ca0205d044878c8996fec96bef10df61001"));
    G1 g(e, g_str.c_str(), g_str.size());
    std::string g1_str(common::Encode::HexDecode("7c8ae882453932ed180735e6eef3c983c93e0501dcfe6a1230fbfea4ac95f4c22795fe5a8137549d1a1b7427519b189431e794e365be5910fcd8e1c91bbc67fa00"));
    G1 g1(e, g1_str.c_str(), g1_str.size());

    auto lines = common::Split<>(value.c_str(), ';');
    if (lines.Count() != 2) {
        ZJC_WARN("failed!");
        return kContractError;
    }

    std::string id(lines[0]);

    //密钥生成，这里生成10个用户。
    //下标为0的用户0作为加密者，其余用户(1~9)为接受者。
    int nu = 10;
    vector<Zr> sk;
    vector<G1> pk;
    for(int i = 0;i<nu;i++){
        auto private_key = id + "_" + std::string("init_prikey_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, private_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        Zr x(e, val.c_str(), val.size());
        sk.push_back(x);
        auto public_key = id + "_" + std::string("init_pubkey_") + std::to_string(i);
        if (param.zjc_host->GetKeyValue(param.from, public_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        G1 tmp_pk(e, (const unsigned char*)val.c_str(), val.size(), true, 0);
        pk.push_back(tmp_pk);

        ZJC_WARN("init member private and public key: %d, sk: %s, pk: %s",
            i, common::Encode::HexEncode(x.toString()).c_str(),
            common::Encode::HexEncode(tmp_pk.toString(true)).c_str());
    }

    int np=10,t=4;
    vector<Zr> proxyId, hid;
    for(int i = 0;i<np;i++){
        auto key = id + "_" + std::string("create_renc_key_proxyid_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        proxyId.push_back(Zr(e,val.c_str(), val.size()));
    }

    for(int i = 0;i<np;i++){
        auto key = id + "_" + std::string("create_renc_key_hid_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }
        hid.push_back(Zr(e, val.c_str(), val.size()));
    }

    vector<vector<G1>> rk1;
    vector<G1> rk2;
    vector<GT> rk3;
    for(int i = 1; i < nu;i++){
        Zr r(e,true);
        auto rk2_key = id + "_" + std::string("create_renc_key_rk2_") + std::to_string(i);
        std::string rk2_val;
        if (param.zjc_host->GetKeyValue(param.from, rk2_key, &rk2_val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", rk2_key.c_str());
            return kContractError;
        }

        rk2.push_back(G1(e, rk2_val.c_str(), rk2_val.size()));
        auto rk3_key = id + "_" + std::string("create_renc_key_rk3_") + std::to_string(i);
        std::string rk3_val;
        if (param.zjc_host->GetKeyValue(param.from, rk3_key, &rk3_val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", rk3_key.c_str());
            return kContractError;
        }

        rk3.push_back(GT(e, rk3_val.c_str(), rk3_val.size()));
        vector<G1> tmp;
        for(int j= 0; j<np; j++) {
            auto key = id + "_" + std::string("create_renc_key_rk1_") + std::to_string(i) + "_" + std::to_string(j);
            std::string rk1_val;
            if (param.zjc_host->GetKeyValue(param.from, key, &rk1_val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }

            tmp.push_back(G1(e, rk1_val.c_str(), rk1_val.size()));
        }

        rk1.push_back(tmp);
    }
    
    //用户0使用自己的公钥pk0加密消息m
    //c=(c1,c2,c3,c4,c5,c6),每项又包含np个，用于分发给np个代理
    vector<G1> c1,c3,c5;
    vector<GT> c2,c4,c6;
    std::string test_data = lines[1];
    GT m(e, test_data.c_str(), test_data.size());
    ZJC_WARN("enc m data src: %s, tom: %s",
        test_data.c_str(), 
        common::Encode::HexEncode(m.toString()).c_str());
    ZJC_WARN("get m data: %s, %s, %s", 
        test_data.c_str(), 
        common::Encode::HexEncode(m.toString()).c_str(), 
        (const char*)m.getElement()->data);
    std::string r_str = common::Encode::HexDecode("7edf5dac8f63ebaed688f823053e817e51c185d6");
    std::string z_str = common::Encode::HexDecode("1254472274e6a59840bb23e709072735d7267340");
    Zr r(e, r_str.c_str(), r_str.size());
    Zr z(e, z_str.c_str(), z_str.size());
    std::vector<std::string> c5_vec{
        common::Encode::HexDecode("16934ce2fb381892590c313d7a9d75e1aca16d889706c9278db00c697828a27a6981409518ce175c2309f3cbba1d07b3a5e58d556230d842e72ab88b6f5ed97d00"),
        common::Encode::HexDecode("6fbbdd170409a40783a06062accd62ad602f6a8abda55c89100d4f0c124009c2b538fbd3a62ba0e21956ccf3d23a35cb48c29d6b109bae0069d48fac4dc2dc0501"),
        common::Encode::HexDecode("6c20d5cbbe4a4d72d80e727bfaaa6f83ad9e46c69d5396c35efe8eb23ab3c6e491992eb3e7eba490d88f24f071415b4d00cdfc5ac8d2044d65e7441ae959b4ad00"),
        common::Encode::HexDecode("59361ab2ea18e2bc42398cca5a7db3c1a2cfa13c14dc3c756c44ec047a6a2624c3213cada3f1b6e5761175094d3441b48dc9c09b711c9d6da7e6389426b078c700"),
        common::Encode::HexDecode("469bcb60011c4d533297ad40f947a89d0a0d3c322d97b5693aa8568521335898f88dbfca643d0bed07944c0513a52f93726092c31f46fb668a768736b3e1d67500"),
        common::Encode::HexDecode("5db8787b0bc74e44947ca93b143b8392166dca98dac567572c775cbe670c43c40258a6be2f4a6bde238cd106fe81a2aaf0b640ef0adee148ca7a0ef6901440f600"),
        common::Encode::HexDecode("60d652211f7ea105d9a78399b3f7633b0e92d8618616de6d3ae9ae2d8ef103c3b0e2c6f5d5ffa0e8f3389e37068f00c17def358cb7fe892ba0ea3e577539ee2301"),
        common::Encode::HexDecode("3d1d9f8555a37f4318147c68201c7d9ff7d8ba385e3742d97fd01b3e4f956e9685ff88f177d6599793c771a18c3017712bd2b2f2a3a5fb0479f1bb18d951f84400"),
        common::Encode::HexDecode("6162e3ded7ac71edcf7311a7b60cbe84bce7bd80af1c1b8631ad86bfc75c325c8784c68d895b583b8244371b4bc155fbe8e7e739d1d76e88c207143d213358ac01"),
        common::Encode::HexDecode("0004aed5ee12588cf789a947bedf0a26807bd8663bac5eb34929c71decf89cabdd34ec2e0c2ed46421174ca8ad2bd7acd1340054cc3d261226ca99ee1a13cde800")
    };

    std::vector<std::string> c6_vec{
        common::Encode::HexDecode("3f61b3ad7719b11df4df758f7787f93862f7f7df9fa75decaccbea4bdad8e784346e95ceb9f0e693d84589035bff738577e352203a90dec7e84c9cf7898af7442c0b759623a9cbc3988e92827782756d70fec1bb62905c658ef417d696012ac995f78749a915593380098834a855410ec5ae5d8fcf9e810f03655acf0617e741"),
        common::Encode::HexDecode("7bdb26bc3a311ddb1796daa21e8b41e4443999f99918dae428e792f4281dcaf9c6c821919438a78d3a9095b5f77a49e9d7513762e0cbeb7451bea22b4e6e7fb0928b1824fd6ce1d42fa27d9660a4ccb12342cd7b86be44146a463526c64e6620cd1d4298d294da259aafc9a31f5c1aedcb01ec7aea59d34ecea403841e5d3fe6"),
        common::Encode::HexDecode("6aa5b8882b182ad41a164a6004c0a977bc2eb98dbf0f23809bf2f5d571c73f6ae3997af193596f1efb96a2306802f96098742bc720fb21ed8a78d7599a2c10c533b85fb75965ecafaf120a757482a2ecc0ad2bcc6b73765e0913c11c8fd0a4536600968ddd31e61f17d5322602e766670bd4d78f5756b0ccd755bce2768ec713"),
        common::Encode::HexDecode("5664eaa9d5f0c4b2261217a9f9de6cca34ce1a06c784583b7d5b107a753b230e7d414499ff091513762cb5b80d493afdf35d304a7c8d02ad285a7cd1f86e4cec6d88723626f03fd5999d9eb15d16c8b4d29cf1fe074c97fc0162ca292fc81d88cd5a33aefec8ec4eee6290ab278aec289d7e2898a2cb67c316f121cedca4283e"),
        common::Encode::HexDecode("90535bcd82d07d30b27e40db9886d3f8d8e899749ef9584d979fbd581089f97c439887780ff4c007b67f146f48b8c8e76a9cd1385b34c90ea14aba96b53307676424def3da81ba854ed8b024769525e72663a652ba76c24cbdf1fe98c37656aa6b382adcf615e1525cc0ad1a6a01e2112567c2c406ed78e92dbae3698d4ed19c"),
        common::Encode::HexDecode("01921c688cae6c7b3998a68a4445db37131d8d24a77b2246a560a63b9e5e0b5635167e6e08ba369c4f12552c012855c45a937ca0648e0b4b8a52aba0724570b32110933cb783e510c28a5cf7255ffe110545bd29b47ca6e2223fc2b35ff30628ba2ff37a46450be2de17741de40434fb72b534343a8372f50cd3aa44a0bf808e"),
        common::Encode::HexDecode("48a4b3dbe42ab27c9648ad5dc6841c6337345441607ef1cf53378540dd6597c224b4eb74aa8d9341b72d08cbcd84e1d2987dd982fa4f609e4caa45221cfc0f1d8f52acebc4e47696a163b77f8ce8834739dcecaa7f507d30ef7ec8c8a01775bd4190c47c36dad61993a59de5eeab824965aceca1cc8fa6b82cad01c2fdc5b76d"),
        common::Encode::HexDecode("09eb0288527d8bbd506fa0b348218c689187b98dacca8c991236d4141827156ecd0fdf7ee29c947e64645aa59c226649295bdfcf774263e12a73386c00d24d581f7dc042619791ba5a9c001767d7addf72e8629c4aa086339cf1334c1999807229b8477764792c9c0f77cad8cb154c700caa84162efa8bfc1d5d6fa39d604bee"),
        common::Encode::HexDecode("1a4237a61b43aec10f5e31bc3a18144ffe8d360edc83d2750e1945b3c697498eca2452a2c4b632d4078af12baf6d0ff94ba7e1584932ba38f67ff709a7a692d961b9d6b1d5559d990437b61c553cc968eea5339fd9dde8768e86bea347ec86b2144a2ae2eeb58b97b4060b8389233f38474f58fb0e6e5ee3f9fc55b005b281c3"),
        common::Encode::HexDecode("7a4862984defcf29a65b2e4aa09603be99995f04ce6c9daf0a3c8d645fce348daa25d3d315cde539f23305207ea249b2edd0db1dc7f0bd301f7ab7e44d80b8b58df2a0f9f311960681eceded415682dd7d809e8120083d6308e9469414a45e5962571f9e1b8fc33a0274a38bf649fe2d42801cac32f14ba256b25ae2ca49a9ba")
    };

    ZJC_WARN("re encryption create key r: %s, z: %s", common::Encode::HexEncode(r.toString()).c_str(), common::Encode::HexEncode(z.toString()).c_str());
    for (int i = 0; i < np; i++) {
        auto tmp_c1 = g^r;
        auto tmp_c2 = (m*(e(g1,pk[0])^r))^hid[i];
        auto tmp_c3 = g^z;
        auto tmp_c4 = e(g1,pk[0])^(z*hid[i]);
        auto tmp_c5 = G1(e, c5_vec[i].c_str(), c5_vec[i].size());
        auto tmp_c6 = GT(e, c6_vec[i].c_str(), c6_vec[i].size());
        ZJC_WARN("re encryption create key i: %d, c5: %s, c6: %s", i, common::Encode::HexEncode(tmp_c5.toString(true)).c_str(), common::Encode::HexEncode(tmp_c6.toString()).c_str());
        c1.push_back(tmp_c1);
        c2.push_back(tmp_c2);
        c3.push_back(tmp_c3);
        c4.push_back(tmp_c4);
        c5.push_back(tmp_c5);
        c6.push_back(tmp_c6);
        ZJC_WARN("c-6 create member %d c1: %s, c2: %s, c3: %s, c4: %s, c5: %s, c6: %s",
                i, common::Encode::HexEncode(tmp_c1.toString(true)).c_str(),
                common::Encode::HexEncode(tmp_c2.toString()).c_str(),
                common::Encode::HexEncode(tmp_c3.toString(true)).c_str(),
                common::Encode::HexEncode(tmp_c4.toString()).c_str(),
                common::Encode::HexEncode(tmp_c5.toString(true)).c_str(),
                common::Encode::HexEncode(tmp_c6.toString()).c_str());
        {
            auto key = id + "_" + std::string("create_enc_user_msg_c1_") + std::to_string(i);
            param.zjc_host->SaveKeyValue(param.from, key, tmp_c1.toString(true));
            ZJC_WARN("save now create g1 i: %d, from:%s, key: %s, val: %s",
                i, 
                common::Encode::HexEncode(param.from).c_str(), 
                key.c_str(), 
                common::Encode::HexEncode(tmp_c1.toString(true)).c_str());
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c2_") + std::to_string(i);
            param.zjc_host->SaveKeyValue(param.from, key, tmp_c2.toString());
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c3_") + std::to_string(i);
            param.zjc_host->SaveKeyValue(param.from, key, tmp_c3.toString(true));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c4_") + std::to_string(i);
            param.zjc_host->SaveKeyValue(param.from, key, tmp_c4.toString());
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c5_") + std::to_string(i);
            param.zjc_host->SaveKeyValue(param.from, key, tmp_c5.toString(true));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c6_") + std::to_string(i);
            param.zjc_host->SaveKeyValue(param.from, key, tmp_c6.toString());
        }
    }

    //初始密文的解密如下(为了方便，选前t个碎片解密)
    vector<Zr> lag;
    for (int i = 0; i < t; i++) {
        Zr result(e, (long)1);
        // 拉格朗日差值
        for (int j = 0; j < t; j++) {
            if (proxyId[j] == proxyId[i]) {
                continue;
            }

            result *= (proxyId[j] / (proxyId[j] - proxyId[i]));
        }

        lag.push_back(result);
        ZJC_WARN("create member lag: %d, lag: %s", i, common::Encode::HexEncode(result.toString()).c_str());
        auto key = id + "_" + std::string("create_enc_user_msg_lag_") + std::to_string(i);
        param.zjc_host->SaveKeyValue(param.from, key, result.toString());
    }

    GT tempc2(c2[0] ^ lag[0]);
    for (int i = 1; i < t; i++) {
        tempc2 *= (c2[i] ^ lag[i]);
    }

    GT result1(tempc2 / e(c1[0], g1 ^ sk[0]));
    if (m == result1) {
        ZJC_WARN("user encryption success: %s", common::Encode::HexEncode(result1.toString()).c_str());
    } else {
        ZJC_WARN("user encryption failed: %s, %s", common::Encode::HexEncode(m.toString()).c_str(), common::Encode::HexEncode(result1.toString()).c_str());
    }

    return kContractSuccess;
} catch(std::exception& e) {
    ZJC_WARN("user encryption data catch error: %s", e.what());
    return kContractError;
}

int ContractReEncryption::ReEncryptUserMessage(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto lines = common::Split<>(value.c_str(), ';');
    if (lines.Count() != 2) {
        return kContractError;
    }

    std::string id(lines[0]);

    auto& e = *pairing_ptr_;
    int nu = 10;
    int np=10,t=4;
    vector<G1> c1,c3,c5;
    vector<GT> c2,c4,c6;
    for (int i = 0; i < np; i++) {
        {
            auto key = id + "_" + std::string("create_enc_user_msg_c1_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c1.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c2_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c2.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c3_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c3.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c4_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c4.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c5_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c5.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c6_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c6.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }
    }
    //初始密文的解密如下(为了方便，选前t个碎片解密)
    vector<vector<G1>> rk1;
    vector<G1> rk2;
    vector<GT> rk3;
    for(int i = 1; i < nu;i++){
        Zr r(e,true);
        auto rk2_key = id + "_" + std::string("create_renc_key_rk2_") + std::to_string(i);
        std::string rk2_val;
        if (param.zjc_host->GetKeyValue(param.from, rk2_key, &rk2_val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", rk2_key.c_str());
            return kContractError;
        }

        rk2.push_back(G1(e, (const unsigned char*)rk2_val.c_str(), rk2_val.size(), true, 0));
        auto rk3_key = id + "_" + std::string("create_renc_key_rk3_") + std::to_string(i);
        std::string rk3_val;
        if (param.zjc_host->GetKeyValue(param.from, rk3_key, &rk3_val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", rk3_key.c_str());
            return kContractError;
        }

        rk3.push_back(GT(e, (const unsigned char*)rk3_val.c_str(), rk3_val.size(), 0));
        vector<G1> tmp;
        for(int j= 0; j<np; j++) {
            auto key = id + "_" + std::string("create_renc_key_rk1_") + std::to_string(i) + "_" + std::to_string(j);
            std::string rk1_val;
            if (param.zjc_host->GetKeyValue(param.from, key, &rk1_val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }

            tmp.push_back(G1(e, (const unsigned char*)rk1_val.c_str(), rk1_val.size(), true, 0));
        }
        
        rk1.push_back(tmp);
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
    std::vector<std::string> w1_vec{
        common::Encode::HexDecode("3651a4e5df2ccf01bb0008470e79f92e4c5e1219"),
        common::Encode::HexDecode("00dfecbab5d98af34f15be229bd52ebf97875208"),
        common::Encode::HexDecode("21e5f65d090bc5f32c835065c46fdf5cee048c8e"),
        common::Encode::HexDecode("00c86d7360197a0612e382bccb221e713d109481"),
        common::Encode::HexDecode("1d9aba1b1d19c3a53c1797d027ad77c7f17baee9"),
        common::Encode::HexDecode("64fdc762c07514950719fdef69b541c1eeef02de"),
        common::Encode::HexDecode("07e450d8711937038584d5c57a6ffc38813c1c4f"),
        common::Encode::HexDecode("7b7abbb83682ad432966d5f65f14b029163cf94f"),
        common::Encode::HexDecode("199155e313a39f146ad2c243ec67c9f9cf194099")
    };

    std::vector<std::string> w2_vec{
        common::Encode::HexDecode("19c9bb04c356e61bde2f0f5a4c68d81892cfe095"),
        common::Encode::HexDecode("52a9607972e60997400b657df89af985e24d2958"),
        common::Encode::HexDecode("3f282e3d475f1e6827db14c1fd1f3e8de86e8cb5"),
        common::Encode::HexDecode("20cb8c80f0ee6d575e4801c2a47d022024144eb9"),
        common::Encode::HexDecode("72a51b01deb6ba7bbfd23a21fc24e4e6f4f0a13f"),
        common::Encode::HexDecode("0a515ba36acb63cb962d7a88d5f96c8fd2f3a3f7"),
        common::Encode::HexDecode("4b0e9455da7b02ca61802012dea1a085272a2d2e"),
        common::Encode::HexDecode("66f4f3ec8d91492c6579718478fc1c5721982456"),
        common::Encode::HexDecode("6c5fe12238ad420ff91da801360ae1e6e07cde11")
    };

    //有nu-1个接受者，则需重加密nu-1次
    for(int i = 1;i<nu;i++){
        //在实际应用中，这里的两个随机数需要使用分布式随机数（密钥）协商算法。
        //例如每个代理都向其他代理发送w1i和w2i，每个代理接收后都做累加得到w1和w2。
        Zr w1(e, w1_vec[i - 1].c_str(), w1_vec[i - 1].size());
        Zr w2(e, w2_vec[i - 1].c_str(), w2_vec[i - 1].size());
        ZJC_WARN("encrypt data i: %d, w1: %s, w2: %s",
            i, 
            common::Encode::HexEncode(w1.toString()).c_str(), 
            common::Encode::HexEncode(w2.toString()).c_str());
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
        for (uint32_t tmp_idx = 0; tmp_idx < tmp1.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp1: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp1[tmp_idx].toString(true)).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc1_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp1[tmp_idx].toString(true));
        }

        for (uint32_t tmp_idx = 0; tmp_idx < tmp2.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp2: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp2[tmp_idx].toString()).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc2_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp2[tmp_idx].toString());
        }

        for (uint32_t tmp_idx = 0; tmp_idx < tmp3.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp3: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp3[tmp_idx].toString(true)).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc3_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp3[tmp_idx].toString(true));
        }

        for (uint32_t tmp_idx = 0; tmp_idx < tmp4.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp4: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp4[tmp_idx].toString()).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc4_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp4[tmp_idx].toString());
        }

        for (uint32_t tmp_idx = 0; tmp_idx < tmp5.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp5: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp5[tmp_idx].toString(true)).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc5_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp5[tmp_idx].toString(true));
        }

        for (uint32_t tmp_idx = 0; tmp_idx < tmp6.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp6: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp6[tmp_idx].toString()).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc6_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp6[tmp_idx].toString());
        }
    }

    return kContractSuccess;
}

int ContractReEncryption::ReEncryptUserMessageWithMember(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value) {
    auto lines = common::Split<>(value.c_str(), ';');
    if (lines.Count() != 2) {
        ZJC_WARN("failed!");
        return kContractError;
    }

    std::string id(lines[0]);

    int32_t member_idx = -1;
    if (!common::StringUtil::ToInt32(value, &member_idx)) {
        ZJC_WARN("member index failed!");
        return kContractError;
    }

    auto& e = *pairing_ptr_;
    int nu = 10;
    int np=10,t=4;
    if (member_idx < 1 || member_idx >= nu) {
        ZJC_WARN("failed!");
        return kContractError;
    }

    vector<G1> c1,c3,c5;
    vector<GT> c2,c4,c6;
    for (int i = 0; i < np; i++) {
        {
            auto key = id + "_" + std::string("create_enc_user_msg_c1_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c1.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c2_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c2.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c3_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c3.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c4_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c4.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c5_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c5.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c6_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c6.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }
    }
    //初始密文的解密如下(为了方便，选前t个碎片解密)
    vector<vector<G1>> rk1;
    vector<G1> rk2;
    vector<GT> rk3;
    for(int i = 1; i < nu;i++){
        if (i != member_idx) {
            continue;
        }

        auto rk2_key = id + "_" + std::string("create_renc_key_rk2_") + std::to_string(i);
        std::string rk2_val;
        if (param.zjc_host->GetKeyValue(param.from, rk2_key, &rk2_val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", rk2_key.c_str());
            return kContractError;
        }

        rk2.push_back(G1(e, (const unsigned char*)rk2_val.c_str(), rk2_val.size(), true, 0));
        auto rk3_key = id + "_" + std::string("create_renc_key_rk3_") + std::to_string(i);
        std::string rk3_val;
        if (param.zjc_host->GetKeyValue(param.from, rk3_key, &rk3_val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", rk3_key.c_str());
            return kContractError;
        }

        rk3.push_back(GT(e, (const unsigned char*)rk3_val.c_str(), rk3_val.size(), 0));
        vector<G1> tmp;
        for(int j= 0; j<np; j++) {
            auto key = id + "_" + std::string("create_renc_key_rk1_") + std::to_string(i) + "_" + std::to_string(j);
            std::string rk1_val;
            if (param.zjc_host->GetKeyValue(param.from, key, &rk1_val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }

            tmp.push_back(G1(e, (const unsigned char*)rk1_val.c_str(), rk1_val.size(), true, 0));
        }
        
        rk1.push_back(tmp);
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
    std::vector<std::string> w1_vec{
        common::Encode::HexDecode("3651a4e5df2ccf01bb0008470e79f92e4c5e1219"),
        common::Encode::HexDecode("00dfecbab5d98af34f15be229bd52ebf97875208"),
        common::Encode::HexDecode("21e5f65d090bc5f32c835065c46fdf5cee048c8e"),
        common::Encode::HexDecode("00c86d7360197a0612e382bccb221e713d109481"),
        common::Encode::HexDecode("1d9aba1b1d19c3a53c1797d027ad77c7f17baee9"),
        common::Encode::HexDecode("64fdc762c07514950719fdef69b541c1eeef02de"),
        common::Encode::HexDecode("07e450d8711937038584d5c57a6ffc38813c1c4f"),
        common::Encode::HexDecode("7b7abbb83682ad432966d5f65f14b029163cf94f"),
        common::Encode::HexDecode("199155e313a39f146ad2c243ec67c9f9cf194099")
    };

    std::vector<std::string> w2_vec{
        common::Encode::HexDecode("19c9bb04c356e61bde2f0f5a4c68d81892cfe095"),
        common::Encode::HexDecode("52a9607972e60997400b657df89af985e24d2958"),
        common::Encode::HexDecode("3f282e3d475f1e6827db14c1fd1f3e8de86e8cb5"),
        common::Encode::HexDecode("20cb8c80f0ee6d575e4801c2a47d022024144eb9"),
        common::Encode::HexDecode("72a51b01deb6ba7bbfd23a21fc24e4e6f4f0a13f"),
        common::Encode::HexDecode("0a515ba36acb63cb962d7a88d5f96c8fd2f3a3f7"),
        common::Encode::HexDecode("4b0e9455da7b02ca61802012dea1a085272a2d2e"),
        common::Encode::HexDecode("66f4f3ec8d91492c6579718478fc1c5721982456"),
        common::Encode::HexDecode("6c5fe12238ad420ff91da801360ae1e6e07cde11")
    };

    //有nu-1个接受者，则需重加密nu-1次
    for(int i = 1;i<nu;i++) {
        if (i > member_idx) {
            break;
        }
        
        //在实际应用中，这里的两个随机数需要使用分布式随机数（密钥）协商算法。
        //例如每个代理都向其他代理发送w1i和w2i，每个代理接收后都做累加得到w1和w2。
        Zr w1(e, w1_vec[i - 1].c_str(), w1_vec[i - 1].size());
        Zr w2(e, w2_vec[i - 1].c_str(), w2_vec[i - 1].size());
        ZJC_WARN("encrypt data i: %d, w1: %s, w2: %s",
            i, 
            common::Encode::HexEncode(w1.toString()).c_str(), 
            common::Encode::HexEncode(w2.toString()).c_str());
        vector<G1> tmp1,tmp3,tmp5;
        vector<GT> tmp2,tmp4,tmp6;
        for(int j= 0;j<t;j++){
            tmp1.push_back(rc1[i-1][j]*(rc3[i-1][j]^w1));
            tmp3.push_back(rc3[i-1][j]^w2);
            tmp2.push_back(rc2[i-1][j]*(rc4[i-1][j]^w1)*e(tmp1.back(),rk1[0][j]));
            tmp4.push_back((rc4[i-1][j]*e(rc3[i-1][j],rk1[0][j]))^w2);
        }
        tmp5.push_back(rk2[0]);
        tmp6.push_back(rk3[0]);
        rc1.push_back(tmp1);
        rc2.push_back(tmp2);
        rc3.push_back(tmp3);
        rc4.push_back(tmp4);
        rc5.push_back(tmp5);
        rc6.push_back(tmp6);
        for (uint32_t tmp_idx = 0; tmp_idx < tmp1.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp1: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp1[tmp_idx].toString(true)).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc1_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp1[tmp_idx].toString(true));
        }

        for (uint32_t tmp_idx = 0; tmp_idx < tmp2.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp2: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp2[tmp_idx].toString()).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc2_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp2[tmp_idx].toString());
        }

        for (uint32_t tmp_idx = 0; tmp_idx < tmp3.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp3: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp3[tmp_idx].toString(true)).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc3_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp3[tmp_idx].toString(true));
        }

        for (uint32_t tmp_idx = 0; tmp_idx < tmp4.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp4: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp4[tmp_idx].toString()).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc4_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp4[tmp_idx].toString());
        }

        for (uint32_t tmp_idx = 0; tmp_idx < tmp5.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp5: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp5[tmp_idx].toString(true)).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc5_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp5[tmp_idx].toString(true));
        }

        for (uint32_t tmp_idx = 0; tmp_idx < tmp6.size(); ++tmp_idx) {
            ZJC_WARN("create member reenc data: %d, %d, tmp6: %s", 
                i, tmp_idx, 
                common::Encode::HexEncode(tmp6[tmp_idx].toString()).c_str());
            auto key = id + "_" + std::string("create_reenc_user_msg_rc6_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            param.zjc_host->SaveKeyValue(param.from, key, tmp6[tmp_idx].toString());
        }
    }

    return kContractSuccess;
}

int ContractReEncryption::Decryption(
        const CallParameters& param, 
        const std::string& key, 
        const std::string& value,
        std::string* res) try {
    ZJC_WARN("called 0!");
    auto lines = common::Split<>(value.c_str(), ';');
    if (lines.Count() != 2) {
        ZJC_WARN("failed: %s, count: %d", value.c_str(), lines.Count());
        return kContractError;
    }

    ZJC_WARN("called 1!");
    std::string id(lines[0]);
    auto& e = *pairing_ptr_;
    //密钥生成，这里生成10个用户。
    //下标为0的用户0作为加密者，其余用户(1~9)为接受者。
    int nu = 10;
    int np = 10;
    int t = 4;
    vector<Zr> sk;
    vector<G1> pk;
    for(int i = 0;i<nu;i++){
        auto private_key = id + "_" + std::string("init_prikey_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, private_key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        Zr x(e, (const unsigned char*)val.c_str(), val.size(), 0);
        sk.push_back(x);
    }

    ZJC_WARN("called 2!");
    std::string g1_str(common::Encode::HexDecode("7c8ae882453932ed180735e6eef3c983c93e0501dcfe6a1230fbfea4ac95f4c22795fe5a8137549d1a1b7427519b189431e794e365be5910fcd8e1c91bbc67fa00"));
    G1 g1(e, g1_str.c_str(), g1_str.size());

    vector<vector<G1>> rc1,rc3,rc5;
    vector<vector<GT>> rc2,rc4,rc6;
    vector<G1> c1,c3,c5;
    vector<GT> c2,c4,c6;
    for(int i = 0;i<np;i++){
        {
            auto key = id + "_" + std::string("create_enc_user_msg_c1_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }

            ZJC_WARN("now create g1 i: %d, from:%s, key: %s, val: %s",
                i, 
                common::Encode::HexEncode(param.from).c_str(), 
                key.c_str(), 
                common::Encode::HexEncode(val).c_str());
            c1.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c2_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c2.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c3_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c3.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c4_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c4.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c5_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c5.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
        }

        {
            auto key = id + "_" + std::string("create_enc_user_msg_c6_") + std::to_string(i);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }
            c6.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }
    }

    ZJC_WARN("called 3!");
    rc1.push_back(c1);
    rc2.push_back(c2);
    rc3.push_back(c3);
    rc4.push_back(c4);
    rc5.push_back(c5);
    rc6.push_back(c6);

    //有nu-1个接受者，则需重加密nu-1次
    for (int i = 1; i < nu; i++) {
        ZJC_WARN("called 3 0!");
        vector<G1> tmp1;
        for (int32_t tmp_idx = 0; tmp_idx < t; ++tmp_idx) {
            ZJC_WARN("called 3 0 0!");
            auto key = id + "_" + std::string("create_reenc_user_msg_rc1_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            std::string val;
            ZJC_WARN("called 3 0 1!");
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }

            ZJC_WARN("called 3 0 2!");
            tmp1.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
            ZJC_WARN("called 3 0 3!");
        }

        ZJC_WARN("called 3 1!");
        vector<GT> tmp2;
        for (int32_t tmp_idx = 0; tmp_idx < t; ++tmp_idx) {
            auto key = id + "_" + std::string("create_reenc_user_msg_rc2_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }

            tmp2.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }

        ZJC_WARN("called 3 2!");
        vector<G1> tmp3;
        for (int32_t tmp_idx = 0; tmp_idx < t; ++tmp_idx) {
            auto key = id + "_" + std::string("create_reenc_user_msg_rc3_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }

            tmp3.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
        }

        ZJC_WARN("called 3 3!");
        vector<GT> tmp4;
        for (int32_t tmp_idx = 0; tmp_idx < t; ++tmp_idx) {
            auto key = id + "_" + std::string("create_reenc_user_msg_rc4_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }

            tmp4.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }

        ZJC_WARN("called 3 4!");
        vector<G1> tmp5;
        for (int32_t tmp_idx = 0; tmp_idx < 1; ++tmp_idx) {
            auto key = id + "_" + std::string("create_reenc_user_msg_rc5_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }

            tmp5.push_back(G1(e, (const unsigned char*)val.c_str(), val.size(), true, 0));
        }

        ZJC_WARN("called 3 5!");
        vector<GT> tmp6;
        for (int32_t tmp_idx = 0; tmp_idx < 1; ++tmp_idx) {
            auto key = id + "_" + std::string("create_reenc_user_msg_rc6_") + std::to_string(i) + "_" + std::to_string(tmp_idx);
            std::string val;
            if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
                CONTRACT_ERROR("get key value failed: %s", key.c_str());
                return kContractError;
            }

            tmp6.push_back(GT(e, (const unsigned char*)val.c_str(), val.size(), 0));
        }

        ZJC_WARN("called 3 6!");
        rc1.push_back(tmp1);
        rc2.push_back(tmp2);
        rc3.push_back(tmp3);
        rc4.push_back(tmp4);
        rc5.push_back(tmp5);
        rc6.push_back(tmp6);
        ZJC_WARN("called 3 7!");
    }

    ZJC_WARN("called 4");
    vector<Zr> lag;
    for (int i = 0; i < t; i++) {
        auto key = id + "_" + std::string("create_enc_user_msg_lag_") + std::to_string(i);
        std::string val;
        if (param.zjc_host->GetKeyValue(param.from, key, &val) != 0) {
            CONTRACT_ERROR("get key value failed: %s", key.c_str());
            return kContractError;
        }

        lag.push_back(Zr(e, (const unsigned char*)val.c_str(), val.size(), 0));
    }

    // std::string test_data = "e49b72939ac609cf50e1773ea4af277b29f048981ec6438a45ac38b8c7f489ff";
    // GT m(e, test_data.c_str(), test_data.size());
    // ZJC_WARN("dec m data src: %s, tom: %s",
    //     test_data.c_str(), 
    //     common::Encode::HexEncode(m.toString()).c_str());
    // 重加密密文的解密如下(为了方便，选前t个碎片解密)
    for(int i = 1; i<nu; i++){
        GT Xi = rc6[i][0] / e(g1 ^ sk[i], rc5[i][0]);
        GT tempc2(rc2[i][0] ^ lag[0]);
        for (int j = 1; j < t; j++) {
            tempc2 *= (rc2[i][j] ^ lag[j]);
        }

        GT result2 = tempc2 / e(rc1[i][0], G1(e, Xi.toString().c_str(), Xi.getElementSize()));
        ZJC_WARN("get m data user %d success, res2 data: %s", 
            i, 
            common::Encode::HexEncode(result2.toString()).c_str());
        // if (m == result2) {
            if (res != nullptr) {
                *res = result2.toString();
                return kContractSuccess;
            }
        // } else {
        //     ZJC_WARN("user %d failed.", i);
        // }
    }

    ZJC_WARN("called 6");
    return kContractSuccess;
}  catch (std::exception& e) {
    CONTRACT_ERROR("get exception failed: %s", e.what());
    return kContractError;
}

}  // namespace contract

}  // namespace shardora
