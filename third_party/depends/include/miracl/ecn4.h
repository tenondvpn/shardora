/*
 *    MIRACL  C++ Header file ecn4.h
 *
 *    AUTHOR  : M. Scott
 *  
 *    PURPOSE : Definition of class ECn4 (Arithmetic on an Elliptic Curve,
 *               mod n^4)
 *
 *    NOTE    : Must be used in conjunction with zzn.cpp, big.cpp and 
 *              zzn2.cpp  and zzn4.cpp
 *
 * WARNING: This class has been cobbled together for a specific use with
 * the MIRACL library. It is not complete, and may not work in other 
 * applications
 *
 *    Copyright (c) 2001-2004 Shamus Software Ltd.
 */

#ifndef ECN4_H
#define ECN4_H

#include "zzn4.h"

class ECn4
{
    ZZn4 x,y;
    int marker;
public:
    ECn4()     {marker=MR_EPOINT_INFINITY;}
    ECn4(const ECn4& b) {x=b.x; y=b.y; marker=b.marker; }

    ECn4& operator=(const ECn4& b) 
        {x=b.x; y=b.y; marker=b.marker; return *this; }
    
    BOOL add(const ECn4&,ZZn4&);

    ECn4& operator+=(const ECn4&); 
    ECn4& operator-=(const ECn4&); 
    ECn4& operator*=(const Big&); 
   
    void clear() {x=y=0; marker=MR_EPOINT_INFINITY;}
    BOOL iszero() {if (marker==MR_EPOINT_INFINITY) return TRUE; return FALSE;}

    void get(ZZn4&,ZZn4&);
    void get(ZZn4&);

    BOOL set(const ZZn4&,const ZZn4&); // set on the curve - returns FALSE if no such point
    BOOL set(const ZZn4&);      // sets x coordinate on curve, and finds y coordinate
    
    friend ECn4 operator-(const ECn4&);
    friend ECn4 operator+(const ECn4&,const ECn4&);
    friend ECn4 operator-(const ECn4&,const ECn4&);

    friend BOOL operator==(const ECn4& a,const ECn4 &b) 
        {return (a.x==b.x && a.y==b.y && a.marker==b.marker); }
    friend BOOL operator!=(const ECn4& a,const ECn4 &b) 
        {return (a.x!=b.x || a.y!=b.y || a.marker!=b.marker); }

    friend ECn4 operator*(const Big &,const ECn4&);

#ifndef MR_NO_STANDARD_IO
    friend ostream& operator<<(ostream&,ECn4&);
#endif

    ~ECn4() {}
};

#endif

