#ifndef __GT_H__
#define __GT_H__

#include "G.h"
using namespace std;

class GT: public G {
public:
	
static GT pow2(const Pairing &e, const GT &base1, const Zr &exp1, const GT &base2, const Zr &exp2);
static GT pow3(const Pairing &e, const GT &base1, const Zr &exp1, const GT &base2, const Zr &exp2, const GT &base3, const Zr &exp3);
	
  GT(){};

 //Create and initialize an element
  GT(const Pairing &e);

  //Create an identity or a random element
  GT(const Pairing &e, bool identity);

  //Create an element from import 
  GT(const Pairing &e, const unsigned char *data, 
	 unsigned short len, unsigned short base = 0);

  //Create an element from hash
  GT(const Pairing &e, const void *data, 
	 unsigned short len);

  //Intialize with another element but with different value
  GT(const GT &h, bool identity=false):G(h,identity){}

  //Copy constructor
  //GT(const GT &h):G(h){}

  // Assignment operator 
  GT& operator=(const GT &rhs){return(GT&)G::operator=(rhs);}

  //Arithmetic Assignment Operators
  GT& operator*=(const GT &rhs){return (GT&)G::operator*=(rhs);}
  GT& operator/=(const GT &rhs){return (GT&)G::operator/=(rhs);}
  GT& operator^=(const Zr &exp){return (GT&)G::operator^=(exp);}

  // Non-assignment operators
  const GT operator*(const GT &rhs) const {
    return GT(*this) *= rhs;
  }
  const GT operator/(const GT &rhs) const {
    return GT(*this) /= rhs;
  }

  const GT operator^(const Zr &exp) const {
    return GT(*this) ^= exp;
  }

  bool operator==(const GT &rhs) const {
	return G::operator==(rhs);
  }

  const GT inverse() const{
	GT gT;
	gT.setElement(G::inverse().getElement());
	return gT;
  }
  const GT square() const{
	GT gT;
	gT.setElement(G::square().getElement());
	return gT;
  }
};

#endif
