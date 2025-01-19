#pragma once

#include <functional>
#include <map>

#include "pbc/pbc.h"
#include "pbc/pbcxx.h"

namespace shardora {

namespace pkicl {

using G1 = pbc::G1Element;  // G1
using G2 = pbc::GTElement;  // G2
using Zq = pbc::ZrElement;

using BilinearPairing = pbc::Pairing;
using IdList = std::vector<std::string>;
using CertList = std::vector<std::string>;

using PlainText = std::string;
using ByteStream = std::string;

using HASH1 = std::function<G1(const std::string&)>;
using HASH2 = std::function<G1(const std::string&)>;
using HASH3 = std::function<G1(const int&)>;
using HASH4 = std::function<ByteStream(G2&)>;

static constexpr int kZeta = 384;  // default plaintext length
static constexpr int kPKIn = 5;  // default PKI participants num
static constexpr int kCLn = 3;   // default CL participants num

struct CipherText {
  G1 c1;
  G1 c2;
  ByteStream c3;
};

struct PublicParam {
  BilinearPairing e;  // e(G1, G1) -> G2
  G1 g;               // G1's generator g
  G1 g1;              // g1 = g^k
  HASH1 H1;           // {0,1}* -> G1
  HASH2 H2;           // {0,1}* -> G1
  HASH3 H3;           // {0,1}* -> G1
  HASH4 H4;           // {0,1}* -> G2

  explicit PublicParam(const std::string& secure_param)
      : e(secure_param), g(e), g1(e) {}
};

struct KeyPair {
  int i = 0;

  G1 pk;  // public key
  G1 s;  // secret key

  KeyPair(int i, G1&& pk, G1&& s)
      : i(i), pk(std::move(pk)), s(std::move(s)) {};
};

struct Msg {
  int i = 0;  // from

  G1 r;
  std::map<int, G1> d_map;  // j -> d

  Msg(int i, G1&& r, std::map<int, G1>&& d_map)
      : i(i), r(std::move(r)), d_map(std::move(d_map)) {};
};

struct EncodeKey {
  G1 omega;  // w
  G2 A;      // A
};

struct DecodeKey {
  int i = 0;

  G1 d;  // d_i

  DecodeKey(int i, G1&& d) : i(i), d(std::move(d)) {};
};

}

}