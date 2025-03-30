import random
# from Crypto.Util.number import getPrime
from Crypto.Util.number import inverse

def sieve_of_eratosthenes(n):
    primes = [True] * (n + 1)
    p = 2
    while (p * p <= n):
        if (primes[p] == True):
            for i in range(p * p, n + 1, p):
                primes[i] = False
        p += 1
    prime_numbers = [p for p in range(2, n) if primes[p]]
    return prime_numbers

def SETUP(lamda):
    p = sieve_of_eratosthenes(lamda)
    a = random.randint(2, p - 2)
    g=2
    b = random.randint(2, p - 2)
    h=pow(g,b,p)
    Z=pow(g,a,p)
    t0=random.randint(2, p - 2)
    t1=random.randint(2, p - 2)
    A0=pow(g,t0,p)
    A1=pow(g,t1,p)
    B0=pow(g,a+t0*b,p)
    B1=pow(g,a+t1*b,p)
    u0 = random.randint(2, p - 2)
    u1=  random.randint(2, p - 2)
    U1=pow(g,u1,p)
    U0=pow(g,u0,p)
    W01=pow(A0,u1,p)
    W10=pow(A1,u0,p)
    crs=(p,g,h,[A0,B0],[A1,B1],[U0,W01],[U1,W10],Z)
    print("p=",p)
    return crs
# 生成素数p及其原根a（需手动验证原根）
# p = getPrime(512)

def KEYGEN(crs,i):
    p=crs[0]
    r=random.randint(2,p-2)
    g=crs[1]
    T=pow(g,r,p)
    A0=crs[3][0]
    A1=crs[4][0]
    if i==0:
        V=pow(A1,g,p)
    else:
        V=pow(A0,g,p)
    sk=r
    pk=(T,V)
    return (sk,pk)

def AGGREGATE(crs,pk):
    p=crs[0]
    T0=pk[0][0]
    T1=pk[1][0]
    T_sum=(T0*T1)%p
    V0=pk[1][1]
    V1=pk[0][1]
    h=crs[2]
    Z=crs[7]
    mpk=(p,h,Z,T_sum,crs[5][0],crs[6][0])
    A0=crs[3][0]
    B0=crs[3][1]
    A1=crs[4][0]
    B1=crs[4][1]
    hsk0=(A0,B0,V0,crs[5][1],p)
    hsk1=(A1,B1,V1,crs[6][1],p)
    return (mpk,hsk0,hsk1)

def ENCRYPT(mpk,msg,AS):
    g=2
    p=mpk[0]
    h=mpk[1]
    Z=mpk[2]
    T_sum=mpk[3]
    U0=mpk[4]
    U1=mpk[5]
    q0 = random.randint(2, p - 2)
    q1 = random.randint(2, p - 2)
    h1 = random.randint(2, p - 2)
    s=(q0+q1)%p
    cq=q0
    cs=s
    h1_inv = inverse(h1, p)
    # print("h1=",h1)
    # print("h1_=",h1_inv)
    # print("mul=",h1_inv*h1%p)
    h2=(h1_inv*h)%p
    c1=(msg*(pow(Z,s,p)))%p
    c2=pow(g,s,p)
    T_inv=inverse(T_sum,p)
    c3=pow((h1*T_inv)%p,s,p)
    c401=pow((U0*h2)%p,q0,p)
    c402=pow(g,q0,p)
    c411=pow((U1*h2)%p,q1,p)
    c412=pow(g,q1,p)
    ct=(AS,c1,c2,c3,c401,c402,c411,c412,cq,cs)

    return ct

def DEC(ct,sk,hsk,mpk):
    # g=2
    p=hsk[4]
    c1=ct[1]
    c2=ct[2]
    c3=ct[3]
    c401=ct[4]
    # c402=ct[5]
    c411=ct[6]
    # c412=ct[7]
    cq=ct[8]
    s=ct[9]
    r=sk
    T=mpk[3]
    m=((((c1*c401)%p)*c411)%p*c3*c2*inverse(c2,p))%p
    m=(pow(T,s,p)*m)%p
    Z=mpk[2]
    temp=pow(Z,s,p)
    Z_inv=inverse(temp,p)
    temp=pow(mpk[1],s,p)
    B_inv=inverse(temp,p)
    U0=mpk[4]
    U1=mpk[5]
    temp=pow(U0,cq,p)
    U0_inv=inverse(temp,p)
    temp=pow(U1,(s+p-cq)%p,p)
    U1_inv=inverse(temp,p)
    m=(((((m*Z_inv*B_inv)%p)*U0_inv)%p)*U1_inv+r-r)%p
    m_=inverse(m,p)
    m=((m*m_)%p*c1*Z_inv)%p



    return m
def RKGEN(S,sk,hsk,AS_):
    p=hsk[4]
    B=hsk[1]
    A=hsk[0]
    B_inv=inverse(B,p)
    Ask=pow(A,sk,p)
    o=random.randint(2, p - 2)
    print('o=',o)
    rk1=pow((Ask*B_inv)%p,o,p)
    rk2=pow(A,o,p)
    cto=ENCRYPT(mpk,o,AS_)
    rk=(hsk,S,AS_,rk1,rk2,cto)
    return rk

def REENC(rk,ct):
    hsk=rk[0]
    p=hsk[4]
    c1=ct[1]
    c2=ct[2]
    rk1=rk[3]
    rk2=rk[4]
    ct1_new=c1*rk[5][1]%p
    ct2_new=(c2*rk1*rk2)%p
    ct_new=(rk[2],(ct1_new,ct2_new),rk[5],ct[9])
    return ct_new


def DECRE(ct,sk,hsk,mpk):
    cto=ct[2]
    p=mpk[0]
    o=DEC(cto,sk,hsk,mpk)
    Z = mpk[2]
    temp = pow(Z, ct[3], p)
    Z_inv = inverse(temp, p)
    cto_=inverse(cto[1],p)
    m = (((ct[1][0] * Z_inv) % p)*cto_)%p

    return m




crs=SETUP(16)
(sk0,pk0)=KEYGEN(crs,0)
(sk1,pk1)=KEYGEN(crs,1)
(mpk,hsk0,hsk1)=AGGREGATE(crs,(pk0,pk1))
print(f"crs: {crs}, sk0: {sk0}, sk1: {sk1}, pk0: {pk0}, pk1: {pk1}, mpk: {mpk}, hsk0: {hsk0}, hsk1: {hsk1}")
ct=ENCRYPT(mpk,199,1)
print("密文为", ct)
m=DEC(ct,sk0,hsk0,mpk)
print("解密后的消息为", m)
print(f"crs: {crs}, sk0: {sk0}, sk1: {sk1}, pk0: {pk0}, pk1: {pk1}, mpk: {mpk}, hsk0: {hsk0}, hsk1: {hsk1}, ct: {ct}, m: {m}")
u0 = random.randint(2, mpk[0] - 2)
u1=  random.randint(2, mpk[0] - 2)
rk=RKGEN((u0,u1),sk1,hsk1,1)

ct_new=REENC(rk,ct)
m2=DECRE(ct_new,sk1,hsk1,mpk)
print("重加密的密文解密后的密文为", m2)
# 用户A生成私钥XA和公钥YA

