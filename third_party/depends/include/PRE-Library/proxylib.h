// The JHU-MIT Proxy Re-encryption Library (PRL)
//
// proxylib.h: Main include file for implementation of pairing-based
// proxy re-encryption scheme.
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

#ifndef __PROXYLIB_H__
#define __PROXYLIB_H__

#include "miracl/ecn.h"
#include "miracl/ebrick.h"
#include "miracl/zzn2.h"

using namespace std;
//
// Macros
//

#define PRINT_DEBUG_STRING(x) printDebugString(x)
#define ASCII_SEPARATOR "#"

//
// Constants
//

#define DEVRANDOM "/dev/urandom" // Not ideal, but faster than /dev/random

//
// Data structures and Classes
//

// Public parameters (shared among all users in a system)
class CurveParams {
 public:
  int bits;
  Big p, q, qsquared;
  ECn P;  
  ZZn2 Z;
  ZZn2 Zprecomp;
  ZZn2 cube;

  virtual int getSerializedSize(SERIALIZE_MODE mode); 
  virtual int serialize(SERIALIZE_MODE mode,
            char *buffer, int maxBuffer);
  virtual BOOL deserialize(SERIALIZE_MODE mode,
               char *buffer, int maxBuffer);
  virtual int maxPlaintextSize() {
    Big temp;
    this->Z.get(temp);
                      
    //return ::bits(temp);
    // Jet: Sep 06, 2016
    return bits = toint(temp);
  }

  BOOL operator==(CurveParams &second) {
    return ((this->bits == second.bits) && 
        (this->p == second.p) &&
        (this->q == second.q) &&
        (this->qsquared == second.qsquared) &&
        (this->P == second.P) &&
        (this->Z == second.Z) &&
        (this->cube == second.cube));
  }

};

// ProxyPK: Public Key class
//
// This is a top-level class; specific proxy re-encryption schemes
// implement subclasses of this structure.

class ProxyPK {
public:
  SCHEME_TYPE schemeType;

  virtual int getSerializedSize(SERIALIZE_MODE mode) { return 0; } 
  virtual int serialize(SERIALIZE_MODE mode,
                 char *buffer, int maxBuffer) {
    return 0;
  }
  virtual BOOL deserialize(SERIALIZE_MODE mode,
             char *buffer, int maxBuffer) {
    return FALSE;
  }
};

// ProxySK: Secret Key class
//
// This is a top-level class; specific proxy re-encryption schemes
// implement subclasses of this structure.

class ProxySK {
 public:
  SCHEME_TYPE schemeType;

  virtual int getSerializedSize(SERIALIZE_MODE mode) { return 0; } 
  virtual int serialize(SERIALIZE_MODE mode,
            char *buffer, int maxBuffer) {
    return 0;
  }
  virtual BOOL deserialize(SERIALIZE_MODE mode,
             char *buffer, int maxBuffer) {
    return FALSE;
  }
};

// ProxyCiphertext: Ciphertext class
//
// This is a top-level class; specific re-encryption schemes
// implement subclasses of this structure.

class ProxyCiphertext {
 public:
  CIPHERTEXT_TYPE type;
  SCHEME_TYPE schemeType;

  virtual int getSerializedSize(SERIALIZE_MODE mode) { return 0; } 
  virtual int serialize(SERIALIZE_MODE mode,
            char *buffer, int maxBuffer) {
    return 0;
  }
  virtual BOOL deserialize(SERIALIZE_MODE mode,
               char *buffer, int maxBuffer) {
    return FALSE;
  }
};

// Main C++ interface

BOOL initLibrary(BOOL selfseed = TRUE, char *seedbuf = NULL,
         int bufSize = 0);

// Utility Routines

BOOL ReadParamsFile(char *filename, CurveParams &params);
BOOL ImportPublicKey(char *buffer, int bufferSize, ProxyPK &pubkey);
BOOL ImportSecretKey(char *buffer, int bufferSize, ProxySK &secret);
BOOL fast_tate_pairing(ECn& P,ZZn2& Qx,ZZn& Qy,Big& q,ZZn2& res);
BOOL ecap(ECn& P,ECn& Q,Big& order,ZZn2& cube,ZZn2& res);
ECn map_to_point(char *ID);
void strip(char *name);

// Core Routines
BOOL proxy_level1_encrypt(CurveParams &params, Big &plaintext, ProxyPK &publicKey, ZZn2 &res1, ZZn2 &res2);
BOOL proxy_level2_encrypt(CurveParams &params, ZZn2 zPlaintext, ProxyPK &publicKey, ECn &res1, ZZn2 &res2);
BOOL proxy_delegate(CurveParams &params, ProxyPK &delegate, ProxySK &delegator, ECn &reskey);
BOOL proxy_reencrypt(CurveParams &params, ECn &c1, ECn &delegation, ZZn2 &res1);
BOOL proxy_decrypt_level1(CurveParams &params, ZZn2 &c1, ZZn2 &c2, ProxySK &secretKey, Big &plaintext);
BOOL proxy_decrypt_level2(CurveParams &params, ECn &c1, ZZn2 &c2, ProxySK &secretKey, Big &plaintext);
BOOL proxy_decrypt_reencrypted(CurveParams &params, ZZn2 &c1, ZZn2 &c2, ProxySK &secretKey, Big &plaintext);

// Utility routines
BOOL encodePlaintextAsBig(CurveParams &params,
             char *message, int messageLen, Big &msg);
BOOL decodePlaintextFromBig(CurveParams &params,
              char *message, int maxMessage, 
              int *messageLen, Big &msg);

/* Given a char array of bytes and its length, convert to a Big */
ECn charToECn (char *c, int *totLen);
ZZn2 charToZZn2 (char *c, int *totLen);

Big charToBig (char *c, int *totLen);
int BigTochar (Big &x, char *c, int s);

/* Given a big, encode as a byte string in the char, assuming its
   size is less than s
   Return length of bytes written to c, or -1 if error
*/
int ECnTochar (ECn &e, char *c, int s);
int ZZn2Tochar (ZZn2 &z, char *c, int s);

void bufrand(char* seedbuf, int seedsize);
BOOL entropyCollect(char *entropyBuf, int entropyBytes);

// Debug output routine
void printDebugString(string debugString);

//
// Set parameter sizes. For example change PBITS to 1024
//

#define PBITS 512
#define QBITS 160

#define HASH_LEN 20

#define SIMPLE
#define PROJECTIVE

// use static temp variables in crypto routines-- faster, but not
// thread safe
#define SAFESTATIC static
  
//
// Benchmarking
//

//#define BENCHMARKING 1

#define NUMBENCHMARKS 7
#define LEVELONEENCTIMING 0
#define LEVELTWOENCTIMING 1
#define DELEGATETIMING 2
#define REENCTIMING 3
#define LEVELONEDECTIMING 4
#define LEVELTWODECTIMING 5
#define REENCDECTIMING 6

#define LEVELONEENCDESC "Level-1 Encryption"
#define LEVELTWOENCDESC "Level-2 Encryption"
#define DELEGATEDESC "Proxy Delegation"
#define REENCDESC "Proxy Re-encryption"
#define LEVELONEDECDESC "Level-1 Decryption"
#define LEVELTWODECDESC "Level-2 Decryption"
#define REENCDECDESC "Re-encrypted Decryption"

#endif // __PROXYLIB_H__
