#pragma once

//#include <fmt/base.h>

#include "pki/pki_cl_def.h"
namespace shardora {

namespace pkicl {

class PkiClAgka {
 public:
  PublicParam pp;

 private:
  int n_ = 0;  // all paticipants num -1

  Zq k_;  // master secret

  std::map<int, G1> j_map_;  // d_i_j map

  CertList cert_list; // Certificate List
  IdList id_list; // Certificateless List

  std::vector<KeyPair> keys_; // all keys

  std::vector<Msg> msgs_;   // all messages

 public:
  void Simulate(bool honest = true);

  // Stage1: Setup
  void Setup();
  // Stage2: PKI Extrect
  void PkiExtract(const int& n = kPKIn);
  // Stage3: CL Extrect
  void ClExtract(const int& n = kCLn);
  // Stage4: PKI CL Agreement
  void PkiClAgreement(bool honest = true);
  // Stage5: Encode Key Gen
  EncodeKey EncKeyGen();
  // Stage6: Encode Key Gen
  std::map<int, DecodeKey> DecKeyGen();
  // Stage7: Encode
  CipherText Enc(PlainText& plain, EncodeKey& ek);
  // Stage8: Decode
  PlainText Dec(CipherText& cipher, DecodeKey& dk);
  // Stage9: Check  
  std::vector<int> Check(EncodeKey& ek, std::map<int, DecodeKey>& dks);
  // Stage10: Identifiable Abort 
  void IdentifiableAbort(std::vector<int> abort_list);

  explicit PkiClAgka(const std::string& secure_param);
  ~PkiClAgka() = default;

  PkiClAgka(PkiClAgka const&) = delete;
  PkiClAgka& operator=(PkiClAgka const&) = delete;
  PkiClAgka(PkiClAgka&&) = delete;
  PkiClAgka& operator=(PkiClAgka&&) = delete;

 private:
  void initialize();
  void init_id_list(const int& n = kCLn);
  void init_cert_list(const int& n = kPKIn);
  void agreement(bool honest = true);
  DecodeKey generate_d_for_list(int& index, std::vector<Msg>& msgs);
  void output_message_if_(bool no_abort, PlainText& message);
  void identify(int index);
};

}

}