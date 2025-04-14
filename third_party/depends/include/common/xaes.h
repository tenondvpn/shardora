/*
* aes.h
* @version 3.0 (December 2000)
*
* Optimised ANSI C code for the Rijndael cipher (now AES)
*
* @author Vincent Rijmen <vincent.rijmen@esat.kuleuven.ac.be>
* @author Antoon Bosselaers <antoon.bosselaers@esat.kuleuven.ac.be>
* @author Paulo Barreto <paulo.barreto@terra.com.br>
*
* This code is hereby placed in the public domain.
*
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS ''AS IS'' AND ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
* 
*/

#ifndef __XAES_H__
#define __XAES_H__

typedef unsigned char    u8;
typedef unsigned short   u16;
typedef unsigned int     u32;

//
// presult is the pointer to the output data
// pkey a pointer to a 16 byte binary key
// piv is a pointer to a 16 byte IV
// pdata is the pointer to the input data
// ndata is the number of bytes to decrypt, and is a multiple of 16
// assume 128 bit mode
// assume input size is always equal to output size
// always use CBC
//
#ifdef __cplusplus
extern "C" {
#endif
int AesEncrypt128Cbc(u8* presult, u8* pkey,
	u8* piv, u8* pdata, u32 ndata);

//
// presult is the pointer to the output data
// pkey a pointer to a 16 byte binary key
// piv is a pointer to a 16 byte IV
// pdata is the pointer to the input data
// ndata is the number of bytes to decrypt, and is a multiple of 16
// assume 128 bit mode
// assume input size is always equal to output size
// always use CBC
//
int AesDecrypt128Cbc(u8* presult, u8* pkey,
	u8* piv, u8* pdata, u32 ndata);
#ifdef __cplusplus
}
#endif

#endif  // !__XAES_H__

/////////////////////////////////////////////////////////////////////////////
