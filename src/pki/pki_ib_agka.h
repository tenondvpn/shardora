#pragma once

#include <fmt/base.h>

#include "contract/call_parameters.h"
#include "pki/def.h"
#include "protos/prefix_db.h"

namespace shardora {

namespace pki {

class PkiIbAgka {
 public:
  PublicParam pp;

 private:
  int n_ = 0;  // all paticipants num -1

  Zq k_;  // master secret

  std::map<int, G1> j_map_;  // d_i_j map

  std::vector<KeyPair> pki_keys_;
  std::vector<KeyPair> ib_keys_;

  std::vector<Msg> pki_msgs_;  // pki messages
  std::vector<Msg> ib_msgs_;   // ib messages

 public:
  void Simulate();

  // Stage1: Setup
  void Setup();
  // Stage2: PKI Extrect
  void PkiExtract(const int& n = kPKIn);
  int PkiExtract(
        const shardora::contract::CallParameters& param, 
        const std::string& key, 
        const std::string& value);
  int IbExtract(
        const shardora::contract::CallParameters& param, 
        const std::string& key, 
        const std::string& value);
  // Stage3: IB Extrect
  void IbExtract(const IdList& ids, const int& n = kIBn);
  // Stage4: PKI IB Agreement
  void PkiIbAgreement();
  int EncKeyGen(
      const shardora::contract::CallParameters& param, 
      const std::string& key, 
      const std::string& value);
  int DecKeyGen(
      const shardora::contract::CallParameters& param, 
      const std::string& key, 
      const std::string& value);
  // Stage5: Encode Key Gen
  EncodeKey EncKeyGen();
  // Stage6: Encode Key Gen
  std::map<int, DecodeKey> DecKeyGen();
  int Enc(
      const shardora::contract::CallParameters& param, 
      const std::string& key, 
      const std::string& value);
  int Dec(
      const shardora::contract::CallParameters& param, 
      const std::string& key, 
      const std::string& value);
  // Stage7: Encode
  CipherText Enc(PlainText& plain, EncodeKey& ek);
  // Stage8: Decode
  PlainText Dec(CipherText& cipher, DecodeKey& dk);
  // Stage9: Test
  void Test(EncodeKey& ek, std::map<int, DecodeKey>& dks);

  explicit PkiIbAgka(
    const std::string& secure_param, 
    const std::string& k, 
    const std::string& g);
  ~PkiIbAgka() = default;

  PkiIbAgka(PkiIbAgka const&) = delete;
  PkiIbAgka& operator=(PkiIbAgka const&) = delete;
  PkiIbAgka(PkiIbAgka&&) = delete;
  PkiIbAgka& operator=(PkiIbAgka&&) = delete;

 private:
  void agreement(std::vector<KeyPair>& keys, std::vector<Msg>& msgs);
  void ek_gen_omega(std::vector<Msg>& msgs);
  void ek_gen_Q(std::vector<Msg>& msgs);
};

}

}