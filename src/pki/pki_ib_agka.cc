#include "pki/pki_ib_agka.h"

#include <cstdint>

#include <chrono>
#include <ratio>
#include <vector>

#include "common/encode.h"

#include "fmt/base.h"
#include "fmt/format.h"
#include "pki/utils.h"

PkiIbAgka::PkiIbAgka(const std::string& secure_param)
    : pp(secure_param), k_(pp.e) {}

// Simulate the protocol
void PkiIbAgka::Simulate() {
  IdList id_list = {"Alice", "Bob", "Cindy", "David", "Eden", "Frank"};

  PlainText txt = "Hello World";

  // fmt::println("\n[ Stage1: 🛠️ Setup ]\n");
  Setup();

  // fmt::println("\n[ Stage2: 🚚 PKI Extract ]\n");
  PkiExtract(kPKIn);

  // fmt::println("\n[ Stage3: 🚛 IB Extract ]\n");
  IbExtract(id_list);

  // fmt::println("\n[ Stage4: 🧾 PKI IB Agreement ]\n");
  PkiIbAgreement();

  // fmt::println("\n[ Stage5: 🔑 Encode Key Gen ]\n");
  auto ek = EncKeyGen();

  // fmt::println("\n[ Stage6: 🪄 Decode Key Gen ]\n");
  auto dks = DecKeyGen();

  // fmt::println("\n[ Stage7: 💌 Encode Message ]\n");
  auto cipher = Enc(txt, ek);

  // fmt::println("\n[ Stage8: 📨 Decode Cipher ]\n");
  auto dec_txt = Dec(cipher, dks.at(0));
  std::cout << "src: " << txt << ", dec: " << dec_txt << std::endl;

  // fmt::println("\n[ Stage9: 🧪 Final Test ]\n");
  Test(ek, dks);
}

// Stage1: Setup
void PkiIbAgka::Setup() {
  // setup biliner pair e
  if (!pp.e.is_symmetric()) {
    // fmt::println("Bilinear Pair e Not Symmetric");
    exit(1);
  }

  // fmt::println("🍬 Master Secret:");
  // randomly select master secret k_
  k_.set_random();
  {
      std::cout << "k_:" << std::endl;
      auto hex_bytes = shardora::common::Encode::HexEncode(k_.to_bytes());
      std::cout << hex_bytes << std::endl;
      k_.from_bytes(shardora::common::Encode::HexDecode(hex_bytes));
      std::cout << shardora::common::Encode::HexEncode(k_.to_bytes()) << std::endl;
  }
  // fmt::println("\t- k = {}", byte2string(k_.to_bytes()));

  // fmt::println("\n🍼 Public Parameter:");
  // get generator g
  pp.g.set_random();
  {
      std::cout << "pp.g:" << std::endl;
      auto hex_bytes = shardora::common::Encode::HexEncode(pp.g.to_bytes());
      std::cout << hex_bytes << std::endl;
      pp.g.from_bytes(shardora::common::Encode::HexDecode(hex_bytes));
      std::cout << shardora::common::Encode::HexEncode(pp.g.to_bytes()) << std::endl;
  }
  // fmt::println("\t- g = {}", byte2string(pp.g.to_bytes()));

  // compute g1 = g^k
  pp.g1 = pp.g.pow_zn(k_);
  // fmt::println("\t- g1 = {}", byte2string(pp.g1.to_bytes()));

  // initialize H1, H2, H3
  pp.H1 = [this](const std::string& ID) -> G1 {
    G1 result(pp.e);
    result.from_hash(ID);
    return result;
  };

  pp.H2 = [this](const int& index) -> G1 {
    G1 result(pp.e);
    result.from_hash(fmt::to_string(index));
    return result;
  };

  pp.H3 = [this](G2& input) -> ByteStream {
    return byte2string(input.to_bytes());
  };
}

// Stage2: PKI Extract
void PkiIbAgka::PkiExtract(const int& n) {
  pki_keys_.reserve(n);

  for (auto i = 0; i < n; i++) {
    // fmt::println("🍻 Generate PKI Participant Key Pair {}:", n_);
    // sk from G1 randomly
    G1 sk(pp.e);
    sk.set_random();
    {
        std::cout << i << " sk:" << std::endl;
        auto hex_bytes = shardora::common::Encode::HexEncode(sk.to_bytes());
        std::cout << hex_bytes << std::endl;
        sk.from_bytes(shardora::common::Encode::HexDecode(hex_bytes));
        std::cout << shardora::common::Encode::HexEncode(sk.to_bytes()) << std::endl;
    }
    // fmt::println("\t- sk = {}", byte2string(sk.to_bytes()));
    // pk = e(sk,g);
    G2 pk(pp.e);
    pp.e.apply(pk, sk, pp.g);
    // fmt::println("\t- pk = {}\n", byte2string(pk.to_bytes()));
    // generate key and add to list
    pki_keys_.emplace_back(n_++, std::move(pk), std::move(sk));
  }
}

void PkiIbAgka::IbExtract(const IdList& ids, const int& n) {
  ib_keys_.reserve(n);

  for (auto i = 0; i < n; i++) {
    // fmt::println("🥂 Generate IB Participant {} Key Pair {}:", ids[i], n_);
    // id_i = H1(ID)
    G1 id(pp.e);
    id = pp.H1(ids[i]);
    // sk = H1(ID)^k
    G1 sk = id.pow_zn(k_);
    // fmt::println("\t- sk = {}", byte2string(sk.to_bytes()));
    // pk = e(sk,g1)
    G2 pk(pp.e);
    pp.e.apply(pk, id, pp.g1);
    // fmt::println("\t- pk = {}\n", byte2string(pk.to_bytes()));
    // generate key and add to list
    ib_keys_.emplace_back(n_++, std::move(pk), std::move(sk));
  }
}

void PkiIbAgka::PkiIbAgreement() {
  // generate j -> H2(j)
  for (const auto& key : pki_keys_) {
    G1 f_j = pp.H2(key.i);
    j_map_.insert(std::make_pair(key.i, std::move(f_j)));
  }
  for (const auto& key : ib_keys_) {
    G1 f_j = pp.H2(key.i);
    j_map_.insert(std::make_pair(key.i, std::move(f_j)));
  }
  // PKI Agreement
  agreement(pki_keys_, pki_msgs_);
  // IB Agreement
  agreement(ib_keys_, ib_msgs_);
}

EncodeKey PkiIbAgka::EncKeyGen() {
  fmt::println("🍔 Generate Group Encode Key (w,Q):");
  G1 omega(pp.e);
  for (auto& msg : pki_msgs_) {
    omega *= msg.r;
  }
  for (auto& msg : ib_msgs_) {
    omega *= msg.r;
  }
  G2 Q(pp.e);  // NOLINT
  for (auto& key : pki_keys_) {
    Q *= key.pk;
  }
  for (auto& key : ib_keys_) {
    Q *= key.pk;
  }
  fmt::println("\t- w = {}\n", byte2string(omega.to_bytes()));
  fmt::println("\t- Q = {}\n", byte2string(Q.to_bytes()));

  return EncodeKey{.omega = std::move(omega), .Q = std::move(Q)};
}

std::map<int, DecodeKey> PkiIbAgka::DecKeyGen() {
  std::map<int, DecodeKey> dk_map;
  // PKI decode key gen
  for (auto& src : pki_msgs_) {
    // fmt::println("🍟 Generate PKI Participant {} Decode Key:", src.i);
    G1 di(pp.e);
    for (auto const& msg_j : pki_msgs_) {
      auto d_j_i = msg_j.d_map.at(src.i);
      di *= d_j_i;
    }
    for (auto const& msg_j : ib_msgs_) {
      auto d_j_i = msg_j.d_map.at(src.i);
      di *= d_j_i;
    }
    // fmt::println("\t- d{} = {}\n", src.i, byte2string(di.to_bytes()));
    dk_map.try_emplace(src.i, src.i, std::move(di));
  }
  // IB decode key gen
  for (auto& src : ib_msgs_) {
    // fmt::println("\n🍌 Generate IB Participant {} Decode Key ", src.i);
    G1 di(pp.e);
    for (auto const& msg_j : pki_msgs_) {
      auto d_j_i = msg_j.d_map.at(src.i);
      di *= d_j_i;
    }
    for (auto const& msg_j : ib_msgs_) {
      auto d_j_i = msg_j.d_map.at(src.i);
      di *= d_j_i;
    }
    // fmt::println("\t- d{} = {}\n", src.i, byte2string(di.to_bytes()));
    dk_map.try_emplace(src.i, src.i, std::move(di));
  }
  return dk_map;
}

void PkiIbAgka::Test(EncodeKey& ek, std::map<int, DecodeKey>& dks) {
  bool all_pass = true;
  uint32_t failed_num = 0;

  for (auto& iter : dks) {
    G2 pair1 = pp.e(iter.second.d, pp.g);

    G1& fi = j_map_.at(iter.second.i);
    G2 pair2 = pp.e(fi, ek.omega) * ek.Q;

    if (pair1 == pair2) {
      // fmt::println("✅ Participant {} Decode Cipher\n", iter.first);
    } else {
      all_pass = false;
      failed_num++;
      // fmt::println("❌ Participant {} Failed Decode Cipher", iter.first);
    }
  }

  // if (all_pass) {
  // fmt::println("🥇 All Participant Decode Cipher Succussfully !!!");
  //} else {
  // fmt::println("🥀 {} Participant failed Decode Cipher", failed_num);
  //}
}

// Stage7 : Encode CipherText
CipherText PkiIbAgka::Enc(PlainText& plain, EncodeKey& ek) {
  //  randomly select e from Zq
  Zq e(pp.e);
  e.set_random();
  {
      std::cout << " enc e:" << std::endl;
      auto hex_bytes = shardora::common::Encode::HexEncode(e.to_bytes());
      std::cout << hex_bytes << std::endl;
      e.from_bytes(shardora::common::Encode::HexDecode(hex_bytes));
      std::cout << shardora::common::Encode::HexEncode(e.to_bytes()) << std::endl;
  }
  // calc c1 = g^e
  G1 c1(pp.e);
  c1 = pp.g.pow_zn(e);
  // calc c1 = w^e
  G1 c2(pp.e);
  c2 = ek.omega.pow_zn(e);
  // calc c3 = m + H3(Qe)
  ByteStream c3;
  G2 tmp1 = ek.Q.pow_zn(e);
  ByteStream tmp2 = pp.H3(tmp1);
  c3 = xor_strings(plain, tmp2);
  return CipherText{
      .c1 = std::move(c1), .c2 = std::move(c2), .c3 = std::move(c3)};
}

PlainText PkiIbAgka::Dec(CipherText& cipher, DecodeKey& dk) {
  // fmt::println("📘 Input Cipher Text:");
  // fmt::println("\t- c1: {}", byte2string(cipher.c1.to_bytes()));
  // fmt::println("\t- c2: {}", byte2string(cipher.c2.to_bytes()));
  // fmt::println("\t- c3: {}", cipher.c3.c_str());
  auto start = std::chrono::steady_clock::now();
  G2 pair1 = pp.e(dk.d, cipher.c1);
  G2 pair2 = pp.e(j_map_.at(dk.i).invert(), cipher.c2);
  G2 pair = pair1 * pair2;
  std::string plain = xor_strings(cipher.c3, pp.H3(pair));
  auto end = std::chrono::steady_clock::now();
  // 计算时间差
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  fmt::println("Dec Time: {}", duration.count());
  // fmt::println("\n📖 Decoed Plain Text:", plain);
  // fmt::println("\t- {}", plain);
  return plain;
}

void PkiIbAgka::agreement(std::vector<KeyPair>& keys, std::vector<Msg>& msgs) {
  msgs.reserve(keys.size());

  int i = 0;
  for (auto& key : keys) {
    // fmt::println("🔖 Generate PKI IB Agreement Message {}", key.i);
    // eta from Zq randomly
    Zq eta(pp.e);
    eta.set_random();
    {
        std::cout << i++ << " agreement eta:" << std::endl;
        auto hex_bytes = shardora::common::Encode::HexEncode(eta.to_bytes());
        std::cout << hex_bytes << std::endl;
        eta.from_bytes(shardora::common::Encode::HexDecode(hex_bytes));
        std::cout << shardora::common::Encode::HexEncode(eta.to_bytes()) << std::endl;
    }
    // r = g^eta
    G1 r = pp.g.pow_zn(eta);
    // d_i_j = sk_i * f_j^eta_i
    std::map<int, G1> d_i_j;
    for (auto& [j, f_j] : j_map_) {
      G1 d(pp.e);
      d = key.sk * f_j.pow_zn(eta);
      d_i_j.emplace(j, std::move(d));
    }
    // generate message
    msgs.emplace_back(key.i, std::move(r), std::move(d_i_j));
  }
}
