#include <gtest/gtest.h>

#include <iostream>
#define private public
#include "http/http_server.h"

namespace zjchain {

namespace http {

namespace test {

class TestHttpServer : public testing::Test {
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

TEST_F(TestHttpServer, InitAndPop) {
    HttpServer http_server;
    ASSERT_EQ(http_server.Init("0.0.0.0", 19871, 4), kHttpSuccess);
    ASSERT_EQ(http_server.Start(), kHttpSuccess);
    std::cout << "start now." << std::endl;
    for (int i = 0; i < 100; ++i) {
        system("curl \"http://127.0.0.1:19871/dags?tid=172\"");
    }
    std::cout << "stop now." << std::endl;
    ASSERT_EQ(http_server.Stop(), kHttpSuccess);
}

}  // namespace test

}  // namespace http

}  // namespace zjchain
