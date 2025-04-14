// The JHU-MIT Proxy Re-encryption Library (PRL)
//
// proxylib_api.h: C language wrapper.
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

#ifndef __PROXYLIB_API_H__
#define __PROXYLIB_API_H__

extern "C" {

  typedef enum {
    CIPH_FIRST_LEVEL = 0,
    CIPH_SECOND_LEVEL = 1,
    CIPH_REENCRYPTED = 2
  } CIPHERTEXT_TYPE;
  
  typedef enum {
    SCHEME_PRE1 = 0,
    SCHEME_PRE2 = 1
  } SCHEME_TYPE;
  
  typedef enum {
    SERIALIZE_BINARY = 0,
    SERIALIZE_HEXASCII = 1
  } SERIALIZE_MODE;

  typedef enum {
    ERROR_NONE = 0,
    ERROR_PLAINTEXT_TOO_LONG = 1,
    ERROR_OTHER = 100
  } ERRORID;

  // C-compatible wrapper routines.  See documentation for usage.
  //
  int proxylib_initLibrary(char *seedbuf, int bufsize);
  int proxylib_generateParams(void **params, SCHEME_TYPE schemeID);
  int proxylib_serializeParams(void *params, char *buffer, int *bufferSize, 
		int bufferAvailSize, SCHEME_TYPE schemeID);
  int proxylib_deserializeParams(char *buffer, int bufferSize, void **params,
		SCHEME_TYPE schemeID);
  int proxylib_destroyParams(void *params);
  int proxylib_generateKeys(void *params, void **pk, void **sk, 
			    SCHEME_TYPE schemeID);
  int proxylib_serializeKeys(void *params, void *pk, void *sk, char *pkBuf, char *skBuf,
	int *pkBufSize, int *skBufSize, int bufferAvailSize, SCHEME_TYPE schemeID);
  int proxylib_deserializeKeys(void *params, char *pkBuf, char *skBuf,
	int pkBufSize, int skBufSize, void **pk, void **sk, SCHEME_TYPE schemeID);
  int proxylib_destroyKeys(void *pk, void *sk, SCHEME_TYPE schemeID);
  int proxylib_encrypt(void *params, void *pk, char *message, int messageLen, 
		       char *ciphertext, int *ciphLen, CIPHERTEXT_TYPE ctype,
		       SCHEME_TYPE schemeID);
  int proxylib_generateDelegationKey(void *params, void *sk1, void *pk2, void** delKey, 
		      SCHEME_TYPE schemeID);
  int proxylib_serializeDelegationKey(void *params, void *delKey, char *delKeyBuf,
	int *delKeyBufSize, int bufferAvailSize, SCHEME_TYPE schemeID);
  int proxylib_serializeDelegationKey(void *params, void *delKey, char *delKeyBuf,
	int *delKeyBufSize, int bufferAvailSize, SCHEME_TYPE schemeID);
  int proxylib_destroyDelegationKey(void *delKey, SCHEME_TYPE schemeID);
  int proxylib_decrypt(void *params, void *sk, char *message, int *messageLen, 
		       char *ciphertext, int ciphLen, 
		       SCHEME_TYPE schemeID);
  int proxylib_reencrypt(void *params, void *rk, 
		   char *ciphertext, int ciphLen, 
		   char *newciphertext, int *newCiphLen, SCHEME_TYPE schemeID);


} // extern "C"

#endif // __PROXYLIB_API_H__
