#include "pki/pki_cl_agka.h"

#include <chrono>
#include <ratio>
#include <vector>

#include "fmt/base.h"
#include "fmt/format.h"
#include "pki/pki_cl_utils.h"

namespace shardora {

namespace pkicl {

PkiClAgka::PkiClAgka(const std::string& secure_param)
    : pp(secure_param), k_(pp.e) {}

// PkiClAgka::~PkiClAgka() {}

// Simulate the protocol
void PkiClAgka::Simulate(bool honest) {
  // clear and initialize
  initialize();

  // plaintext 
  PlainText plain = "This is a sample message for testing the encryption scheme.";

  fmt::println("\n[ Stage1: 🛠️ Setup ]\n");
  auto start = std::chrono::steady_clock::now();
  Setup();
  auto end = std::chrono::steady_clock::now();
  // 计算时间差
  // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // std::cout << "Stage1 Setup duration:" << duration.count() << std::endl;
  // std::cout << duration.count() << std::endl;

  fmt::println("\n[ Stage2: 🚚 PKI Extract ]\n");
  // start = std::chrono::steady_clock::now();
  PkiExtract();
  // end = std::chrono::steady_clock::now();
  // duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // std::cout << "Stage2 PKI Extract duration:" << duration.count() << std::endl;
  // std::cout << duration.count() << std::endl;

  fmt::println("\n[ Stage3: 🚛 CL Extract ]\n");
  // start = std::chrono::steady_clock::now();
  ClExtract();
  // end = std::chrono::steady_clock::now();
  // duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // std::cout << "Stage3 CL Extract duration:" << duration.count() << std::endl;
  // std::cout << duration.count() << std::endl;

  fmt::println("\n[ Stage4: 🧾 PKI CL Agreement ]\n");
  // start = std::chrono::steady_clock::now();
  PkiClAgreement(honest);
  // end = std::chrono::steady_clock::now();
  // duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // std::cout << "Stage4 PKI Agreement duration:" << duration.count() << std::endl;
  // std::cout << duration.count() << std::endl;

  fmt::println("\n[ Stage5: 🔑 Encode Key Gen ]\n");
  // start = std::chrono::steady_clock::now();
  auto ek = EncKeyGen();
  // end = std::chrono::steady_clock::now();
  // duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // std::cout << "Stage5 Encode Key Gen duration:" << duration.count() << std::endl;
  // std::cout << duration.count() << std::endl;

  fmt::println("\n[ Stage6: 🪄 Decode Key Gen ]\n");
  // start = std::chrono::steady_clock::now();
  auto dks = DecKeyGen();
  // end = std::chrono::steady_clock::now();
  // duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // std::cout << "Stage6 Decode Key Gen duration:" << duration.count() << std::endl;
  // std::cout << duration.count() << std::endl;

  fmt::println("\n[ Stage7: 💌 Encode Message ]\n");
  // start = std::chrono::steady_clock::now();
  auto cipher = Enc(plain, ek);
  // end = std::chrono::steady_clock::now();
  // duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // std::cout << "Stage7 Encode Message duration:" << duration.count() << std::endl;
  // std::cout << duration.count() << std::endl;

  fmt::println("\n[ Stage8: 📨 Decode Cipher ]\n");
  // start = std::chrono::steady_clock::now();
  PlainText message  = Dec(cipher, dks.at(0));
  // end = std::chrono::steady_clock::now();
  // duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // std::cout << "Stage8 Decode Cipher duration:" << duration.count() << std::endl;
  // std::cout << duration.count() << std::endl;

  fmt::println("\n[ Stage9: 🧪 Check if decrypted succussfully ]\n");
  auto abort_list = Check(ek, dks);

  fmt::println("\n[ Stage10: 🧪 Identifiable Abort ]\n");
  // start = std::chrono::steady_clock::now();
  IdentifiableAbort(abort_list);
  end = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  std::cout << "Stage10 Identifiable Abort duration:" << duration.count() << std::endl;
  std::cout << duration.count() << std::endl;

  // If no abort, output decrypted message.
  output_message_if_(abort_list.empty(), message);
}

// Stage1: Setup
void PkiClAgka::Setup() {
  // setup biliner pair e
  if (!pp.e.is_symmetric()) {
    fmt::println("Bilinear Pair e Not Symmetric");
    exit(1);
  }

  fmt::println("🍬 Master Secret:");
  // randomly select master secret k_
  k_.set_random();
  fmt::println("\t- k = {}", byte2string(k_.to_bytes()));

  fmt::println("\n🍼 Public Parameter:");
  // get generator g
  pp.g.set_random();
  fmt::println("\t- g = {}", byte2string(pp.g.to_bytes()));

  // compute g1 = g^k
  pp.g1 = pp.g.pow_zn(k_);
  fmt::println("\t- g1 = {}", byte2string(pp.g1.to_bytes()));

  // initialize H1, H2, H3, H4
  pp.H1 = [this](const std::string& cert) -> G1 {
    G1 result(pp.e);
    result.from_hash(cert);
    return result;
  };
  
  pp.H2 = [this](const std::string& ID) -> G1 {
    G1 result(pp.e);
    result.from_hash(ID);
    return result;
  };

  pp.H3 = [this](const int& index) -> G1 {
    G1 result(pp.e);
    result.from_hash(std::to_string(index));
    return result;
  };

  pp.H4 = [this](G2& input) -> ByteStream {
    return byte2string(input.to_bytes());
  };
}

// Stage2: PKI Extract
void PkiClAgka::PkiExtract(const int& n) {
  keys_.reserve(n);

  for (auto i = 0; i < n; i++) {
    fmt::println("🍻 Generate PKI Participant Key Pair {}:", n_);
    // sk from G1 randomly
   
    Zq sk(pp.e);
    sk.set_random();
    fmt::println("\t- sk = {}", byte2string(sk.to_bytes()));
    // pk = g^sk;
    G1 pk(pp.e);
    pk = pp.g.pow_zn(sk);

    // s = H1(cert)^sk 
    G1 cert(pp.e);
    cert = pp.H1(cert_list[i]);
    G1 s(pp.e);
    s = cert.pow_zn(sk);
    
    fmt::println("\t- pk = {}\n", byte2string(pk.to_bytes()));
    // generate key and add to list
    keys_.emplace_back(n_++, std::move(pk), std::move(s));
  }
}

void PkiClAgka::ClExtract(const int& n) {
  keys_.reserve(n);

  for (auto i = 0; i < n; i++) {
    // fmt::println("🥂 Generate CL Participant {} Key Pair {}:", ids[i], n_);
    // id = H2(ID)
    G1 id(pp.e);
    id = pp.H2(id_list[i]);
    // partial private key psk = H1(ID)^k
    G1 psk = id.pow_zn(k_);
    
    // generate s, fpk and spk
    Zq sk(pp.e);
    sk.set_random();
    // fpk = g^sk;
    G1 fpk(pp.e);
    fpk = pp.g.pow_zn(sk);
    // s = psk^fsk = ((H2(ID)^k)^sk)
    G1 s(pp.e);
    s = psk.pow_zn(sk);
    // spk = g1^sk;
    G1 spk(pp.e);
    spk = pp.g1.pow_zn(sk);
 
    // generate key and add to list
    keys_.emplace_back(n_++, std::move(spk), std::move(s));
  }    
}

void PkiClAgka::PkiClAgreement(bool honest) {
  // generate j -> H3(j)
  for (const auto& key : keys_) {
    G1 f_j = pp.H3(key.i);
    j_map_.insert(std::make_pair(key.i, std::move(f_j)));
  }
  // agreement
  agreement(honest);
}

EncodeKey PkiClAgka::EncKeyGen() {
  fmt::println("🍔 Generate Group Encode Key (w,A):");
  G1 omega(pp.e);
  for (auto& msg : msgs_) {
    omega *= msg.r;
  }
  
  G2 A(pp.e);  // NOLINT
  for (auto& key : keys_) {
    if (key.i < kPKIn){
      A *= pp.e(key.pk, pp.H1(cert_list[key.i]));
    } else {  
      A *= pp.e(key.pk, pp.H2(id_list[key.i - kPKIn]));
    } 
  }
  //fmt::println("\t- w = {}\n", byte2string(omega.to_bytes()));
  //fmt::println("\t- A = {}\n", byte2string(A.to_bytes()));

  return EncodeKey{.omega = std::move(omega), .A = std::move(A)};
}

std::map<int, DecodeKey> PkiClAgka::DecKeyGen() {
  std::map<int, DecodeKey> dk_map;
  // PKI decode key gen
  for (auto& src : msgs_) {
    fmt::println("🍟 Generate PKI Participant {} Decode Key:", src.i);
    
    // fmt::println("\t- d{} = {}\n", src.i, byte2string(di.to_bytes()));
    dk_map.try_emplace(src.i, generate_d_for_list(src.i, msgs_));
  }
  return dk_map;
}

std::vector<int> PkiClAgka::Check(EncodeKey& ek, std::map<int, DecodeKey>& dks) {
  std::vector<int> abort_list;
  abort_list.reserve(n_);

  for (auto& iter : dks) {
    G2 pair1 = pp.e(iter.second.d, pp.g);

    G1& fi = j_map_.at(iter.second.i);
    G2 pair2 = pp.e(fi, ek.omega) * ek.A;

    if (pair1 == pair2) {
      fmt::println("✅ Participant {} Decode Cipher\n", iter.first);
      // std::cout << "True" << std::endl;
    } else {
      abort_list.emplace_back(iter.second.i);
      fmt::println("❌ Participant {} Failed Decode Cipher", iter.first);
      // std::cout << "False" << std::endl;
    }
  }

  return std::move(abort_list);
}

void PkiClAgka::IdentifiableAbort(std::vector<int> abort_list) {
  if (abort_list.empty()) {
    std::cout << "All participants decrypted succussfully!" << std::endl;
    return;
  }

  for (auto i : abort_list) {
    identify(i);
  }
}

// Stage7 : Encode CipherText
CipherText PkiClAgka::Enc(PlainText& plain, EncodeKey& ek) {
  //  randomly select e from Zq
  Zq e(pp.e);
  e.set_random();
  // calc c1 = g^e
  G1 c1(pp.e);
  c1 = pp.g.pow_zn(e);
  // calc c1 = w^e
  G1 c2(pp.e);
  c2 = ek.omega.pow_zn(e);
  // calc c3 = m + H4(Ae)
  ByteStream c3;
  G2 tmp1 = ek.A.pow_zn(e);
  ByteStream tmp2 = pp.H4(tmp1);
  c3 = xor_strings(plain, tmp2);
  return CipherText{
      .c1 = std::move(c1), .c2 = std::move(c2), .c3 = std::move(c3)};
}

PlainText PkiClAgka::Dec(CipherText& cipher, DecodeKey& dk) {
  // auto start = std::chrono::steady_clock::now();
  G2 pair1 = pp.e(dk.d, cipher.c1);
  G2 pair2 = pp.e(j_map_.at(dk.i).invert(), cipher.c2);
  G2 pair = pair1 * pair2;
  std::string plain = xor_strings(cipher.c3, pp.H4(pair));
  // auto end = std::chrono::steady_clock::now();
  // 计算时间差
  // auto duration =
  //     std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  // std::cout << "duration:" << duration.count() << std::endl;
  // std::cout << "plaintext:" << plain << std::endl;
  
  return plain;
}

void PkiClAgka::initialize() {
  n_ = 0;
  j_map_.clear();
  keys_.clear();
  msgs_.clear();
  
  init_cert_list();
  init_id_list();
}

void PkiClAgka::init_id_list(const int& n) {
  id_list.reserve(n);
  id_list.resize(n);
  
  for (auto i = 0; i < n; ++i) {  
        id_list[i] = "Alice_" + std::to_string(i + 1); // 生成不同的字符串  
  }
}

void PkiClAgka::init_cert_list(const int& n) {
  cert_list.reserve(n);
  cert_list.resize(n);
  
  for (auto i = 0; i < n; ++i) {  
        cert_list[i] = "Cert_" + std::to_string(i + 1); // 生成不同的字符串  
  }
}

void PkiClAgka::agreement(bool honest) {
  msgs_.reserve(keys_.size());

  for (auto& key : keys_) {
    fmt::println("🔖 Generate PKI CL Agreement Message {}", key.i);
    // eta from Zq randomly
    Zq eta(pp.e);
    eta.set_random();
    // r = g^eta
    G1 r = pp.g.pow_zn(eta);
    // d_i_j = sk_i * f_j^eta_i
    std::map<int, G1> d_i_j;
    for (auto& [j, f_j] : j_map_) {
      G1 d(pp.e);
      d = key.s * f_j.pow_zn(eta);
      d_i_j.emplace(j, std::move(d));
    }
    // generate messages
    msgs_.emplace_back(key.i, std::move(r), std::move(d_i_j));
  }

  // Randomly choose a malicious participant to test Identifiable Abort. 
  // This malicious participant only changes d_i_j. 
  if (honest == false) {
    for (auto& d_j : msgs_[(kPKIn + kCLn) / 2].d_map) {
      // participant sent blank d_i_j.
      d_j.second *= d_j.second;
    }

    std::cout << "Participant " << (kPKIn + kCLn) / 2 << " is set to be malicious." << std::endl;
  }
}

DecodeKey PkiClAgka::generate_d_for_list(int& index, std::vector<Msg>& msgs) {
  G1 di(pp.e);
  for (auto const& msg_j : msgs) {
    di *= msg_j.d_map.at(index);
  }

  return DecodeKey{.i = index, .d = std::move(di)};
}

void PkiClAgka::output_message_if_(bool no_abort, PlainText& message) {
  if(no_abort){
    std::cout << "Decrypted Message: " << message << std::endl;
  }
}

void PkiClAgka::identify(int index) {
  G1 d = msgs_[index].d_map.at(index);
  G1 f = j_map_.at(index);
  G1 w = msgs_[index].r;
  G2 A(pp.e);
  if (index < kPKIn) {
    A *= pp.e(keys_[index].pk, pp.H1(cert_list[index]));
  } else {
    A *= pp.e(keys_[index].pk, pp.H2(id_list[index - kPKIn]));
  }

  if (pp.e(d, pp.g) != pp.e(f, w) * A) {
    std::cout << "----------------------------------" << std::endl;
    std::cout << "Participant " << index << " is malicious, abort!" << std::endl;
  } else {
    std::cout << "----------------------------------" << std::endl;
    std::cout << "Participant " << index << " is honest and finds that: " << std::endl;

    for (int i = 0; i < n_; ++i) {
      if (i == index) {
        continue;
      }

      G1 d_temp = d;  
      d_temp *= msgs_[i].d_map.at(index);
      
      G1 w_temp = w;
      w_temp *= msgs_[i].r;
      
      G2 A_temp = A;
      if (i < kPKIn) {
        A_temp *= pp.e(keys_[i].pk, pp.H1(cert_list[i]));
      } else {
        A_temp *= pp.e(keys_[i].pk, pp.H2(id_list[i - kPKIn]));
      }
      if (pp.e(d_temp, pp.g) != (pp.e(f, w_temp) * A_temp)) {
        std::cout << "Participant " << i << " sent wrong messages, abort!" << std::endl;
      } else {
        d = d_temp;
        w = w_temp;
        A = A_temp;
      }
    }
  }
}

}

}