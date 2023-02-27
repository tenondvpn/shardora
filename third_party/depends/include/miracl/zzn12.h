/*
 *    MIRACL  C++ Header file ZZn12.h
 *
 *    AUTHOR  : M. Scott
 *
 *    NOTE:   : Must be used in conjunction with big.cpp, zzn.cpp and zzn2.cpp and zzn6.cpp, 
 *
 *    PURPOSE : Definition of class ZZn12  (Arithmetic over n^12)
 *              Implemented as a quadratic entension over Fp^6
 *
 *
 * WARNING: This class has been cobbled together for a specific use with
 * the MIRACL library. It is not complete, and may not work in other 
 * applications
 *
 *
 *    Copyright (c) 2006 Shamus Software Ltd.
 */

#ifndef ZZN12_H
#define ZZN12_H

#include "zzn6a.h"

class ZZn12
{
    
    ZZn6 a,b;
public:
    ZZn12()   {}
    ZZn12(int w) {a=(ZZn6)w; b.clear();}
    ZZn12(const ZZn12& w) {a=w.a; b=w.b;}

    ZZn12(const Big &x)  {a=(ZZn6)x; b.clear();}
    ZZn12(const ZZn6& x) {a=x; b.clear();}

    void set(const ZZn6 &x,const ZZn6 &y) {a=x; b=y;}

    void set(const Big &x) {a=(ZZn6)x; b.clear();}
    void set(const ZZn6 &x) {a=x; b.clear();}
    void seti(const ZZn6 &x) {a.clear(); b=x;}

    void get(ZZn6 &,ZZn6 &);
    void get(ZZn6 &) ;
    
    void clear() {a.clear(); b.clear();}
    ZZn12& conj() {b=-b; return *this;}

    BOOL iszero()  const {if (a.iszero() && b.iszero()) return TRUE; return FALSE; }
    BOOL isunity() const {if (a.isunity() && b.iszero()) return TRUE; return FALSE; }

    ZZn12& powq(const ZZn12 &);
    ZZn12& operator=(int i) {a=(ZZn6)i; b.clear(); return *this;}
    ZZn12& operator=(const ZZn6& x)  {a=x; b.clear(); return *this; }
    ZZn12& operator=(const ZZn12& x) {a=x.a; b=x.b; return *this; }
    ZZn12& operator+=(const ZZn6& x) {a+=x; return *this; }
    ZZn12& operator+=(const ZZn12& x) {a+=x.a; b+=x.b; return *this; }
    ZZn12& operator-=(const ZZn6& x)  {a-=x; return *this; }
    ZZn12& operator-=(const ZZn12& x) {a-=x.a; b-=x.b; return *this; }
    ZZn12& operator*=(const ZZn12&); 
    ZZn12& operator*=(const ZZn6& x) { a*=x; b*=x; return *this;}
    ZZn12& operator*=(int x) { a*=x; b*=x; return *this;}
    ZZn12& operator/=(const ZZn12&); 
    ZZn12& operator/=(const ZZn6& x) { a/=x; b/=x; return *this; }

    friend ZZn12 operator+(const ZZn12&,const ZZn12&);
    friend ZZn12 operator+(const ZZn12&,const ZZn6&);
    friend ZZn12 operator-(const ZZn12&,const ZZn12&);
    friend ZZn12 operator-(const ZZn12&,const ZZn6&);
    friend ZZn12 operator-(const ZZn12&);

    friend ZZn12 operator*(const ZZn12&,const ZZn12&);
    friend ZZn12 operator*(const ZZn12&,const ZZn6&);
    friend ZZn12 operator*(const ZZn6&,const ZZn12&);

    friend ZZn12 operator*(int,const ZZn12&);
    friend ZZn12 operator*(const ZZn12&,int);

    friend ZZn12 operator/(const ZZn12&,const ZZn12&);
    friend ZZn12 operator/(const ZZn12&,const ZZn6&);

    friend ZZn12 pow(const ZZn12&,const Big&);
    friend ZZn12 pow(const ZZn12&,const Big*);
    friend ZZn12 pow(const ZZn12*,const ZZn12&,const Big&);
    friend ZZn12 pow(int,const ZZn12*,const Big*);
    friend void precompute(const ZZn12&,ZZn12 *);
    friend ZZn12 inverse(const ZZn12&);
    friend ZZn12 conj(const ZZn12&);
    friend ZZn6  real(const ZZn12& x)      {return x.a;}
    friend ZZn6  imaginary(const ZZn12& x) {return x.b;}
#ifndef MR_NO_RAND
    friend ZZn12 randn12(void);        // random ZZn12
#endif
    friend BOOL operator==(const ZZn12& x,const ZZn12& y)
    {if (x.a==y.a && x.b==y.b) return TRUE; else return FALSE; }

    friend BOOL operator!=(const ZZn12& x,const ZZn12& y)
    {if (x.a!=y.a || x.b!=y.b) return TRUE; else return FALSE; }
#ifndef MR_NO_STANDARD_IO
    friend ostream& operator<<(ostream&,ZZn12&);
#endif
    ~ZZn12()  {}
};
#ifndef MR_NO_RAND
extern ZZn12 randn12(void); 
#endif
#endif

