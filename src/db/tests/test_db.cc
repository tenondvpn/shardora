#include <stdlib.h>
#include <math.h>

#include <iostream>

#include <gtest/gtest.h>

#include "bzlib.h"

#define private public
#include "db/db.h"

namespace zjchain {

namespace db {

namespace test {

class TestDb : public testing::Test {
public:
    static void SetUpTestCase() {    
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(TestDb, All) {
    Db test_db;
    test_db.Init("/tmp/rocksdb_simple_example");
    auto put_st = test_db.Put("key", "value");
    ASSERT_TRUE(put_st.ok());
    std::string value;
    auto get_st = test_db.Get("key", &value);
    ASSERT_TRUE(get_st.ok());
    ASSERT_EQ(value, "value");
    ASSERT_TRUE(test_db.Exist("key"));
    ASSERT_FALSE(test_db.Exist("key1"));
    auto delete_st = test_db.Delete("key");
    ASSERT_TRUE(delete_st.ok());
    auto get_st2 = test_db.Get("key", &value);
    ASSERT_FALSE(get_st2.ok());

    std::string src("压缩和归档库 bzip2：一个完全免费，免费专利和高质量的数据压缩 doboz：能够快速解压缩的压缩库"
        "PhysicsFS：对各种归档提供抽象访问的库，主要用于视频游戏，设计灵感部分来自于Quake3的文件子系统。"
        "KArchive：用于创建，读写和操作文件档案（例如zip和 tar）的库，它通过QIODevice的一系列子类，使用gzip格式，提供了透明的压缩和解压缩的数据。"
        "LZ4 ：非常快速的压缩算法"
        "LZHAM ：无损压缩数据库，压缩比率跟LZMA接近，但是解压缩速度却要快得多。"
        "LZMA ：7z格式默认和通用的压缩方法。"
        "LZMAT ：及其快速的实时无损数据压缩库"
        "miniz：单一的C源文件，紧缩 / 膨胀压缩库，使用zlib兼容API，ZIP归档读写，PNG写方式。"
        "Minizip：Zlib最新bug修复，支持PKWARE磁盘跨越，AES加密和IO缓冲。"
        "Snappy ：快速压缩和解压缩"
        "ZLib ：非常紧凑的数据流压缩库"
        "ZZIPlib：提供ZIP归档的读权限。"
        "并发性"
        "并发执行和多线程"
        "Boost.Compute ：用于OpenCL的C++GPU计算库"
        "Bolt ：针对GPU进行优化的C++模板库"
        "C++React ：用于C++11的反应性编程库"
        " Intel TBB ：Intel线程构件块"
        " Libclsph：基于OpenCL的GPU加速SPH流体仿真库"
        "OpenCL ：并行编程的异构系统的开放标准"
        "OpenMP：OpenMP API"
        "Thrust ：类似于C++标准模板库的并行算法库"
        " HPX ：用于任何规模的并行和分布式应用程序的通用C++运行时系统"
        " VexCL ：用于OpenCL / CUDA 的C++向量表达式模板库。"
        "容器"
        "C++ B - tree ：基于B树数据结构，实现命令内存容器的模板库"
        "Hashmaps： C++中开放寻址哈希表算法的实现"
        "密码学"
        "Bcrypt ：一个跨平台的文件加密工具，加密文件可以移植到所有可支持的操作系统和处理器中。"
        "BeeCrypt："
        "Botan： C++加密库"
        "Crypto++：一个有关加密方案的免费的C++库"
        "GnuPG： OpenPGP标准的完整实现"
        "GnuTLS ：实现了SSL，TLS和DTLS协议的安全通信库"
        "Libgcrypt"
        "libmcrypt"
        "LibreSSL：免费的SSL / TLS协议，属于2014 OpenSSL的一个分支"
        "LibTomCrypt：一个非常全面的，模块化的，可移植的加密工具"
        "libsodium：基于NaCI的加密库，固执己见，容易使用"
        "Nettle 底层的加密库"
        "OpenSSL ： 一个强大的，商用的，功能齐全的，开放源代码的加密库。"
        "Tiny AES128 in C ：用C实现的一个小巧，可移植的实现了AES128ESB的加密算法"
        "-------------------- -"
        "作者：benpaobagzb"
        "来源：CSDN"
        "原文：https ://blog.csdn.net/benpaobagzb/article/details/50783501 "
        "版权声明：本文为博主原创文章，转载请附上博文链接！; lawfjlasdfalsd");
    std::string cdest(src.size(), 0);
    uint32_t csize = src.size();
    auto cret = BZ2_bzBuffToBuffCompress((char*)&(cdest[0]), &csize, (char*)src.c_str(), src.size(), 9, 0, 0);
    ASSERT_TRUE(cret == BZ_OK);
    std::cout << "src size: " << src.size() << ", dest size: " << csize << std::endl;
    std::string ddtest(src.size(), 0);
    uint32_t dsize = src.size();
    auto dret = BZ2_bzBuffToBuffDecompress((char*)&(ddtest[0]), &dsize, (char*)cdest.c_str(), csize, 0, 0);
    ASSERT_TRUE(dret == BZ_OK);
    ASSERT_TRUE(dsize == src.size());
    ASSERT_TRUE(ddtest == src);
    test_db.Destroy();
}

}  // namespace test

}  // namespace db

}  // namespace zjchain
