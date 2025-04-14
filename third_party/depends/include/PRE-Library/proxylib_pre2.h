// The JHU-MIT Proxy Re-encryption Library (PRL)
//
// proxylib_pre1.h: Data structures for PRE1 proxy re-encryption
// scheme.
//
// ================================================================
// 	
// Copyright (c) 2007, Matthew Green, Giuseppe Ateniese, Kevin Fu,
// Susan Hohenberger.  All rights reserved.
//
// Redistribution and use in source and binary forms, with or
// without modification, are permitted provided that the following
// conditions are met:												
//
// Redistributions of source code must retain the above copyright 
// notice, this list of conditions and the following disclaimer.  
// Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in 
// the documentation and/or other materials provided with the 
// distribution.
//
// Neither the names of the Johns Hopkins University, the Massachusetts
// Institute of Technology nor the names of its contributors may be 
// used to endorse or promote products derived from this software 
// without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
// POSSIBILITY OF SUCH DAMAGE.

#ifndef __PROXYLIB_PRE2_H__
#define __PROXYLIB_PRE2_H__

//
// Data structures
//

// ProxyPK_PRE2: Public Key for PRE2 scheme
//
// Subclass of ProxyPK

class ProxyPK_PRE2: public ProxyPK_PRE1 {
public:
  ProxyPK_PRE2() { this->schemeType = SCHEME_PRE2; }
  ProxyPK_PRE2(ZZn2 &Zp1, ECn &Pp2) { this->schemeType = SCHEME_PRE2;
  this->set(Zp1, Pp2); }

  void set(ZZn2 &Zp1, ECn &Pp2) { this->Zpub1 = Zp1; this->Ppub2 = Pp2; }
};

// ProxySK_PRE2: Secret Key for PRE2 scheme
//
// Subclass of ProxySK

class ProxySK_PRE2: public ProxySK_PRE1 {
 public:
  ProxySK_PRE2() { this->schemeType = SCHEME_PRE2; }
  ProxySK_PRE2(Big &sk1, Big &sk2) { this->schemeType = SCHEME_PRE2;
  this->set(sk1); }
  void set(Big &sk1) { this->a1 = sk1; }

  BOOL operator==(ProxySK_PRE1 &second) {
    return (this->a1 == second.a1);
  }

};

// Proxy Ciphertext_PRE2: Ciphertext class for PRE2 scheme
//
// Subclass of ProxyCiphertext

class ProxyCiphertext_PRE2: public ProxyCiphertext_PRE1 {
 public:
  ProxyCiphertext_PRE2() { this->schemeType = SCHEME_PRE2; }
  ProxyCiphertext_PRE2(CIPHERTEXT_TYPE Type, ECn &C1a, ZZn2 &C1b, ZZn2 &C2) {
    this->schemeType = SCHEME_PRE2;
    this->set(Type, C1a, C1b, C2);
  }

  ProxyCiphertext_PRE2(CIPHERTEXT_TYPE Type, ECn &C1a, ZZn2 &C2) { 
    this->schemeType = SCHEME_PRE2;
    this->set(Type, C1a, C2);
  }

  ProxyCiphertext_PRE2(CIPHERTEXT_TYPE Type, ZZn2 &C1b, ZZn2 &C2) {
    this->schemeType = SCHEME_PRE2;
    this->set(Type, C1b, C2);
  }

  void set(CIPHERTEXT_TYPE Type, ECn &C1a, ZZn2 &C1b, ZZn2 &C2) { this->type = Type; this->c1a = C1a;
						this->c1b = C1b; this->c2 = C2; }
  void set(CIPHERTEXT_TYPE Type, ECn &C1a, ZZn2 &C2) { this->type = Type; this->c1a = C1a;
						this->c2 = C2; }
  void set(CIPHERTEXT_TYPE Type, ZZn2 &C1b, ZZn2 &C2) { this->type = Type; this->c1b = C1b;
						this->c2 = C2; }
};

typedef ECn DelegationKey_PRE2;

// Cryptographic Routines
BOOL PRE2_generate_params(CurveParams &params);
BOOL PRE2_keygen(CurveParams &params, ProxyPK_PRE2 &publicKey, ProxySK_PRE2 &secretKey);
BOOL PRE2_level1_encrypt(CurveParams &params, Big &plaintext, ProxyPK_PRE2 &publicKey, ProxyCiphertext_PRE2 &ciphertext);
BOOL PRE2_level2_encrypt(CurveParams &params, Big &plaintext, ProxyPK_PRE2 &publicKey, ProxyCiphertext_PRE2 &ciphertext);
BOOL PRE2_delegate(CurveParams &params, ProxyPK_PRE2 &delegatee, ProxySK_PRE2 &delegator, DelegationKey_PRE2 &reskey);
BOOL PRE2_reencrypt(CurveParams &params, ProxyCiphertext_PRE2 &origCiphertext, DelegationKey_PRE2 &delegationKey, 
					ProxyCiphertext_PRE2 &newCiphertext);
BOOL PRE2_decrypt(CurveParams &params, ProxyCiphertext_PRE2 &ciphertext, ProxySK_PRE2 &secretKey, Big &plaintext);

int SerializeDelegationKey_PRE2(DelegationKey_PRE2 &delKey, SERIALIZE_MODE mode, char *buffer, int maxBuffer);
BOOL DeserializeDelegationKey_PRE2(DelegationKey_PRE2 &delKey, SERIALIZE_MODE mode, char *buffer, int bufSize);

#endif // __PROXYLIB_PRE2_H__
