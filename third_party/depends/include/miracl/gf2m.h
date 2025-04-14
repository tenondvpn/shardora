/*
 *    MIRACL C++ Header file gf2m.h
 *
 *    AUTHOR  : M.Scott
 *    
 *    PURPOSE : Definition of class GF2m (Arithmetic in the field GF(2^m)
 *
 *    NOTE:   : The field basis is set dynamically via the modulo() routine.
 *              Must be used with big.h and big.cpp
 *
 *    Copyright (c) 2000-2004 Shamus Software Ltd.
 */

#ifndef GF2M_H
#define GF2M_H

#include "big.h"

/*
#ifdef GF2MS
#define MR_INIT_GF2M memset(mem,0,mr_big_reserve(1,GF2MS)); fn=(big)mirvar_mem_variable(mem,0,GF2MS); 
#define MR_CLONE_GF2M(x) fn->len=x->len; for (int i=0;i<GF2MS;i++) fn->w[i]=x->w[i];
#define MR_ZERO_GF2M {fn->len=0; for (int i=0;i<GF2MS;i++) fn->w[i]=0;} 
#else
#define MR_INIT_GF2M mem=(char *)memalloc(1); fn=(big)mirvar_mem(mem,0);
#define MR_CLONE_GF2M(x) copy(x,fn);
#define MR_ZERO_GF2M zero(fn);
#endif
*/


#ifdef GF2MS
#define MR_INIT_GF2M fn=&b; b.w=a; b.len=GF2MS; 
#define MR_CLONE_GF2M(x) b.len=x->len; for (int i=0;i<GF2MS;i++) a[i]=x->w[i]; 
#define MR_ZERO_GF2M {b.len=0; for (int i=0;i<GF2MS;i++) a[i]=0;}  
#else
#define MR_INIT_GF2M fn=mirvar(0);
#define MR_CLONE_GF2M(x) copy(x,fn);
#define MR_ZERO_GF2M zero(fn);
#endif

class GF2m
{
    big fn;
/*
#ifdef GF2MS
    char mem[mr_big_reserve(1,GF2MS)];
#else
    char *mem;
#endif
*/

#ifdef GF2MS
    mr_small a[GF2MS];
    bigtype b;
#endif

public:
    GF2m()              {MR_INIT_GF2M MR_ZERO_GF2M}
    GF2m(int j)         {MR_INIT_GF2M if (j==0) MR_ZERO_GF2M else {convert(j,fn); reduce2(fn,fn);}}
    GF2m(const Big& c)  {MR_INIT_GF2M reduce2(c.getbig(),fn); }   /* Big -> GF2m */
    GF2m(big& c)        {MR_INIT_GF2M MR_CLONE_GF2M(c)}
    GF2m(const GF2m& c) {MR_INIT_GF2M MR_CLONE_GF2M(c.fn)}
    GF2m(char *s)       {MR_INIT_GF2M cinstr(fn,s); reduce2(fn,fn);}
   
    GF2m& operator=(const GF2m& c)  {MR_CLONE_GF2M(c.fn) return *this;}
    GF2m& operator=(big c)          {MR_CLONE_GF2M(c) return *this;}

    GF2m& operator=(int i)   {if (i==0) MR_ZERO_GF2M else {convert(i,fn); reduce2(fn,fn);} return *this;}
    GF2m& operator=(const Big& b)   { reduce2(b.getbig(),fn); return *this; }
    GF2m& operator=(char *s)        { cinstr(fn,s); reduce2(fn,fn); return *this;}
    GF2m& operator++() {incr2(fn,1,fn); return *this; }

    GF2m& operator+=(const GF2m& c)
    {
#ifdef GF2MS
        for (int i=0;i<GF2MS;i++)
            fn->w[i]^=c.fn->w[i];
        fn->len=GF2MS;
        if (fn->w[GF2MS-1]==0) mr_lzero(fn);
#else
        add2(fn,c.fn,fn);
#endif
        return *this;
    }

    GF2m& operator+=(int i) {incr2(fn,i,fn); return *this; }
    GF2m& operator*=(const GF2m& b) {modmult2(fn,b.fn,fn); return *this;}
    GF2m& square() {modsquare2(fn,fn); return *this;}
    GF2m& inverse() {inverse2(fn,fn); return *this;} 
    BOOL quadratic(GF2m& b) {return quad2(fn,b.fn);}
    int degree() {return degree2(fn);}

    BOOL iszero() const;
    BOOL isone() const;
    operator Big() {return (Big)fn;}   /* GF2m -> Big */
    friend big getbig(GF2m& z) {return z.fn;}
    friend int trace(GF2m & z) {return trace2(z.fn);}

    GF2m& operator/=(const GF2m&);
    
    friend GF2m operator+(const GF2m&,const GF2m&);
    friend GF2m operator+(const GF2m&,int);
    friend GF2m operator*(const GF2m&,const GF2m&);
    friend GF2m operator/(const GF2m&,const GF2m&);
    
    friend BOOL operator==(const GF2m& b1,const GF2m& b2)
    { if (mr_compare(b1.fn,b2.fn)==0) return TRUE; else return FALSE;}
    friend BOOL operator!=(const GF2m& b1,const GF2m& b2)
    { if (mr_compare(b1.fn,b2.fn)!=0) return TRUE; else return FALSE;}
     
    friend GF2m square(const GF2m&);
    friend GF2m inverse(const GF2m&);
    friend GF2m pow(const GF2m&,int);
    friend GF2m sqrt(const GF2m&);
    friend GF2m halftrace(const GF2m&);
	friend GF2m quad(const GF2m&);
#ifndef MR_NO_RAND
    friend GF2m random2(void);
#endif
    friend GF2m gcd(const GF2m&,const GF2m&);

    friend void kar2x2(const GF2m*,const GF2m*,GF2m*);
    friend void kar3x3(const GF2m*,const GF2m*,GF2m*);

    friend int degree(const GF2m& x) {return degree2(x.fn);}
    
    ~GF2m()   
    {
      //  zero(fn);
#ifndef GF2MS    
        mr_free(fn); 
#endif  
    }
};
#ifndef MR_NO_RAND
extern GF2m random2(void);
#endif
#endif  
