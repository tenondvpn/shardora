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
// This Agreement, effective as of March 1, 2007 is between the
// Massachusetts Institute of Technology ("MIT"), a non-profit
// institution of higher education, and you (YOU).
//
// WHEREAS, M.I.T. has developed certain software and technology
// pertaining to M.I.T. Case No. 11977, "Unidirectional Proxy
// Re-Encryption," by Giuseppe Ateniese, Kevin Fu, Matt Green and Susan
// Hohenberger (PROGRAM); and
// 
// WHEREAS, M.I.T. is a joint owner of certain right, title and interest
// to a patent pertaining to the technology associated with M.I.T. Case
// No. 11977 "Unidirectional Proxy Re-Encryption," (PATENTED INVENTION");
// and
// 
// WHEREAS, M.I.T. desires to aid the academic and non-commercial
// research community and raise awareness of the PATENTED INVENTION and
// thereby agrees to grant a limited copyright license to the PROGRAM for
// research and non-commercial purposes only, with M.I.T. retaining all
// ownership rights in the PATENTED INVENTION and the PROGRAM; and
// 
// WHEREAS, M.I.T. agrees to make the downloadable software and
// documentation, if any, available to YOU without charge for
// non-commercial research purposes, subject to the following terms and
// conditions.
// 
// THEREFORE:
// 
// 1.  Grant.
// 
// (a) Subject to the terms of this Agreement, M.I.T. hereby grants YOU a
// royalty-free, non-transferable, non-exclusive license in the United
// States for the Term under the copyright to use, reproduce, display,
// perform and modify the PROGRAM solely for non-commercial research
// and/or academic testing purposes.
// 
// (b) MIT hereby agrees that it will not assert its rights in the
// PATENTED INVENTION against YOU provided that YOU comply with the terms
// of this agreement.
// 
// (c) In order to obtain any further license rights, including the right
// to use the PROGRAM or PATENTED INVENTION for commercial purposes, YOU
// must enter into an appropriate license agreement with M.I.T.
// 
// 2.  Disclaimer.  THE PROGRAM MADE AVAILABLE HEREUNDER IS "AS IS",
// WITHOUT WARRANTY OF ANY KIND EXPRESSED OR IMPLIED, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE, NOR REPRESENTATION THAT THE PROGRAM DOES NOT
// INFRINGE THE INTELLECTUAL PROPERTY RIGHTS OF ANY THIRD PARTY. MIT has
// no obligation to assist in your installation or use of the PROGRAM or
// to provide services or maintenance of any type with respect to the
// PROGRAM.  The entire risk as to the quality and performance of the
// PROGRAM is borne by YOU.  YOU acknowledge that the PROGRAM may contain
// errors or bugs.  YOU must determine whether the PROGRAM sufficiently
// meets your requirements.  This disclaimer of warranty constitutes an
// essential part of this Agreement.
// 
// 3. No Consequential Damages; Indemnification.  IN NO EVENT SHALL MIT
// BE LIABLE TO YOU FOR ANY LOST PROFITS OR OTHER INDIRECT, PUNITIVE,
// INCIDENTAL OR CONSEQUENTIAL DAMAGES RELATING TO THE SUBJECT MATTER OF
// THIS AGREEMENT.
// 
// 4. Copyright.  YOU agree to retain M.I.T.'s copyright notice on all
// copies of the PROGRAM or portions thereof.
// 
// 5. Export Control.  YOU agree to comply with all United States export
// control laws and regulations controlling the export of the PROGRAM,
// including, without limitation, all Export Administration Regulations
// of the United States Department of Commerce.  Among other things,
// these laws and regulations prohibit, or require a license for, the
// export of certain types of software to specified countries.
// 
// 6. Reports, Notices, License Request.  Reports, any notice, or
// commercial license requests required or permitted under this Agreement
// shall be directed to:
// 
// Director
// Massachusetts Institute of Technology Technology
// Licensing Office, Rm NE25-230 Five Cambridge Center, Kendall Square
// Cambridge, MA 02142-1493						
// 
// 7.  General.  This Agreement shall be governed by the laws of the
// Commonwealth of Massachusetts.  The parties acknowledge that this
// Agreement sets forth the entire Agreement and understanding of the
// parties as to the subject matter.

#ifndef __PROXYLIB_PRE1_H__
#define __PROXYLIB_PRE1_H__

//
// Data structures and Classes
//

// ProxyPK_PRE1: Public Key for PRE1 scheme
//
// Subclass of ProxyPK

class ProxyPK_PRE1: public ProxyPK {
public:
  ZZn2 Zpub1;
  ECn Ppub2;

  ProxyPK_PRE1() { schemeType = SCHEME_PRE1; }
  ProxyPK_PRE1(ZZn2 &Zp1, ECn &Pp2) { this->schemeType = SCHEME_PRE1;
  this->Zpub1 = Zp1; this->Ppub2 = Pp2; }
  void set(ZZn2 &Zp1, ECn &Pp2) { this->Zpub1 = Zp1; this->Ppub2 = Pp2; }

  virtual int getSerializedSize(SERIALIZE_MODE mode); 
  virtual int serialize(SERIALIZE_MODE mode,
			char *buffer, int maxBuffer);
  virtual BOOL deserialize(SERIALIZE_MODE mode,
			   char *buffer, int maxBuffer);

  BOOL operator==(ProxyPK_PRE1 &second) {
    return ((this->Zpub1 == second.Zpub1) && 
	    (this->Ppub2 == second.Ppub2));
  }
};

// ProxySK_PRE1: Secret Key for PRE1 scheme
//
// Subclass of ProxySK

class ProxySK_PRE1: public ProxySK {
 public:
  Big a1;
  Big a2;

  ProxySK_PRE1() { this->schemeType = SCHEME_PRE1; }
  ProxySK_PRE1(Big &sk1, Big &sk2) { this->schemeType = SCHEME_PRE1; 
  this->set(sk1, sk2); }
  void set(Big &sk1, Big &sk2) { this->a1 = sk1; this->a2 = sk2; }

  virtual int getSerializedSize(SERIALIZE_MODE mode); 
  virtual int serialize(SERIALIZE_MODE mode,
			char *buffer, int maxBuffer);
  virtual BOOL deserialize(SERIALIZE_MODE mode,
			   char *buffer, int maxBuffer);

  BOOL operator==(ProxySK_PRE1 &second) {
    return ((this->a1 == second.a1) && 
	    (this->a2 == second.a2));
  }
};

// Proxy Ciphertext_PRE1: Ciphertext class for PRE1 scheme
//
// Subclass of ProxyCiphertext

class ProxyCiphertext_PRE1: public ProxyCiphertext {
 public:
  ECn c1a;
  ZZn2 c1b;
  ZZn2 c2;

  ProxyCiphertext_PRE1() { this->schemeType = SCHEME_PRE1; }
  ProxyCiphertext_PRE1(CIPHERTEXT_TYPE Type, ECn &C1a, ZZn2 &C1b, ZZn2 &C2) {
    this->schemeType = SCHEME_PRE1;
    this->set(Type, C1a, C1b, C2);
  }

  ProxyCiphertext_PRE1(CIPHERTEXT_TYPE Type, ECn &C1a, ZZn2 &C2) { 
    this->set(Type, C1a, C2);
  }

  ProxyCiphertext_PRE1(CIPHERTEXT_TYPE Type, ZZn2 &C1b, ZZn2 &C2) {
    this->set(Type, C1b, C2);
  }

  void set(CIPHERTEXT_TYPE Type, ECn &C1a, ZZn2 &C1b, ZZn2 &C2) { this->type = Type; this->c1a = C1a;
						this->c1b = C1b; this->c2 = C2; }
  void set(CIPHERTEXT_TYPE Type, ECn &C1a, ZZn2 &C2) { this->type = Type; this->c1a = C1a;
						this->c2 = C2; }
  void set(CIPHERTEXT_TYPE Type, ZZn2 &C1b, ZZn2 &C2) { this->type = Type; this->c1b = C1b;
						this->c2 = C2; }

  virtual int getSerializedSize(SERIALIZE_MODE mode); 
  virtual int serialize(SERIALIZE_MODE mode,
			char *buffer, int maxBuffer);
  virtual BOOL deserialize(SERIALIZE_MODE mode,
  			   char *buffer, int maxBuffer);

  BOOL operator==(ProxyCiphertext_PRE1 &second) {
    return ((this->type == second.type) &&
	    (this->c1a == second.c1a) &&
  	    (this->c1b == second.c1b) &&
	    (this->c2 == second.c2));
  }
};

typedef ECn DelegationKey_PRE1;

// Cryptographic Routines
BOOL PRE1_generate_params(CurveParams &params);
BOOL PRE1_keygen(CurveParams &params, ProxyPK_PRE1 &publicKey, ProxySK_PRE1 &secretKey);
BOOL PRE1_level1_encrypt(CurveParams &params, Big &plaintext, ProxyPK_PRE1 &publicKey, ProxyCiphertext_PRE1 &ciphertext);
BOOL PRE1_level2_encrypt(CurveParams &params, Big &plaintext, ProxyPK_PRE1 &publicKey, ProxyCiphertext_PRE1 &ciphertext);
BOOL PRE1_delegate(CurveParams &params, ProxyPK_PRE1 &delegatee, ProxySK_PRE1 &delegator, DelegationKey_PRE1 &reskey);
BOOL PRE1_reencrypt(CurveParams &params, ProxyCiphertext_PRE1 &origCiphertext, DelegationKey_PRE1 &delegationKey, 
					ProxyCiphertext_PRE1 &newCiphertext);
BOOL PRE1_decrypt(CurveParams &params, ProxyCiphertext_PRE1 &ciphertext, ProxySK_PRE1 &secretKey, Big &plaintext);

int SerializeDelegationKey_PRE1(DelegationKey_PRE1 &delKey, SERIALIZE_MODE mode, char *buffer, int maxBuffer);
BOOL DeserializeDelegationKey_PRE1(DelegationKey_PRE1 &delKey, SERIALIZE_MODE mode, char *buffer, int bufSize);

#endif // __PROXYLIB_PRE1_H__
