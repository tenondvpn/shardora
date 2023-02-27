/*
 *    MIRACL  C++ Header file ZZn6a.h
 *
 *    AUTHOR  : M. Scott
 *
 *    NOTE:   : Must be used in conjunction with zzn6a.cpp zzn2.cpp big.cpp and zzn.cpp
 *            : This is designed as a "towering extension", so a ZZn6 consists
 *            : of three ZZn2. 
 *
 *    PURPOSE : Definition of class zzn6  (Arithmetic over n^6)
 *
 * WARNING: This class has been cobbled together for a specific use with
 * the MIRACL library. It is not complete, and may not work in other 
 * applications
 *
 * irreducible poly is X^3+n, where n=sqrt(m), or n=1+sqrt(m), m=-1 or -2
 *
 *    Copyright (c) 2006 Shamus Software Ltd.
 */

#ifndef ZZN6A_H
#define ZZN6A_H

#include "zzn2.h"

class ZZn6
{
    ZZn2 a,b,c;
public:
    ZZn6()   {}
    ZZn6(int w) {a=(ZZn2)w; b.clear(); c.clear();}
    ZZn6(const ZZn6& w) {a=w.a; b=w.b; c=w.c;}
    ZZn6(const ZZn2 &x) {a=x; b.clear(); c.clear(); }
    ZZn6(const ZZn2 &x,const ZZn2& y,const ZZn2& z) {a=x; b=y; c=z;}
    ZZn6(const ZZn &x) {a=(ZZn2)x; b.clear(); c.clear(); }
    ZZn6(const Big &x) {a=(ZZn2)x; b.clear(); c.clear(); }
    
    void set(const ZZn2 &x,const ZZn2 &y,const ZZn2 &z) {a=x; b=y; c=z; }
    void set(const ZZn2 &x) {a=x; b.clear(); c.clear();}
    void set(const ZZn2 &x,const ZZn2 &y) {a=x; b=y; c.clear(); }
    void set1(const ZZn2 &x) {a.clear(); b=x; c.clear();}
    void set2(const ZZn2 &x) {a.clear(); b.clear(); c=x; }
    void set(const Big &x) {a=(ZZn2)x; b.clear(); c.clear();}

    void get(ZZn2 &,ZZn2 &,ZZn2 &) ;
    void get(ZZn2 &) ;
    
    void clear() {a.clear(); b.clear(); c.clear();}
    
    BOOL iszero()  const {if (a.iszero() && b.iszero() && c.iszero()) return TRUE; return FALSE; }
    BOOL isunity() const {if (a.isunity() && b.iszero() && c.iszero()) return TRUE; return FALSE; }
 //   BOOL isminusone() const {if (a.isminusone() && b.iszero()) return TRUE; return FALSE; }

    ZZn6& powq(const ZZn6&);
    ZZn6& operator=(int i) {a=i; b.clear(); c.clear(); return *this;}
    ZZn6& operator=(const ZZn& x) {a=(ZZn2)x; b.clear(); c.clear(); return *this; }
    ZZn6& operator=(const ZZn2& x) {a=x; b.clear(); c.clear(); return *this; }
    ZZn6& operator=(const ZZn6& x) {a=x.a; b=x.b; c=x.c; return *this; }
    ZZn6& operator+=(const ZZn& x) {a+=(ZZn2)x; return *this; }
    ZZn6& operator+=(const ZZn2& x) {a+=x; return *this; }
    ZZn6& operator+=(const ZZn6& x) {a+=x.a; b+=x.b; c+=x.c; return *this; }
    ZZn6& operator-=(const ZZn& x) {a-=(ZZn2)x;  return *this; }
    ZZn6& operator-=(const ZZn2& x) {a-=x; return *this; }
    ZZn6& operator-=(const ZZn6& x) {a-=x.a; b-=x.b; c-=x.c; return *this; }
    ZZn6& operator*=(const ZZn6&); 
    ZZn6& operator*=(const ZZn2& x) {a*=x; b*=x; c*=x; return *this; }
    ZZn6& operator*=(const ZZn& x) {a*=x; b*=x; c*=x; return *this; }
    ZZn6& operator*=(int x) {a*=x; b*=x; c*=x; return *this;}
    ZZn6& operator/=(const ZZn6&); 
    ZZn6& operator/=(const ZZn2&);
    ZZn6& operator/=(const ZZn&);
    ZZn6& operator/=(int);

    friend ZZn6 operator+(const ZZn6&,const ZZn6&);
    friend ZZn6 operator+(const ZZn6&,const ZZn2&);
    friend ZZn6 operator+(const ZZn6&,const ZZn&);
    friend ZZn6 operator-(const ZZn6&,const ZZn6&);
    friend ZZn6 operator-(const ZZn6&,const ZZn2&);
    friend ZZn6 operator-(const ZZn6&,const ZZn&);
    friend ZZn6 operator-(const ZZn6&);

    friend ZZn6 operator*(const ZZn6&,const ZZn6&);
    friend ZZn6 operator*(const ZZn6&,const ZZn2&);
    friend ZZn6 operator*(const ZZn6&,const ZZn&);
    friend ZZn6 operator*(const ZZn&,const ZZn6&);
    friend ZZn6 operator*(const ZZn2&,const ZZn6&);

    friend ZZn6 operator*(int,const ZZn6&);
    friend ZZn6 operator*(const ZZn6&,int);

    friend ZZn6 operator/(const ZZn6&,const ZZn6&);
    friend ZZn6 operator/(const ZZn6&,const ZZn2&);
    friend ZZn6 operator/(const ZZn6&,const ZZn&);
    friend ZZn6 operator/(const ZZn6&,int);

    friend ZZn6 tx(const ZZn6&);
    friend ZZn6 pow(const ZZn6&,const Big&);
//    friend ZZn6 pow(int,const ZZn6*,const Big*);
    friend ZZn6 powl(const ZZn6&,const Big&);
    friend ZZn6 inverse(const ZZn6&);
#ifndef MR_NO_RAND
    friend ZZn6 randn6(void);        // random ZZn6
#endif
    friend BOOL operator==(const ZZn6& x,const ZZn6& y)
    {if (x.a==y.a && x.b==y.b && x.c==y.c) return TRUE; else return FALSE; }

    friend BOOL operator!=(const ZZn6& x,const ZZn6& y)
    {if (x.a!=y.a || x.b!=y.b || x.c!=y.c) return TRUE; else return FALSE; }

#ifndef MR_NO_STANDARD_IO
    friend ostream& operator<<(ostream&,const ZZn6&);
#endif

    ~ZZn6()  {}
};
#ifndef MR_NO_RAND
extern ZZn6 randn6(void);   
#endif
#endif

