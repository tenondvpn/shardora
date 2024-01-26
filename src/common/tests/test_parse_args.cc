#include <gtest/gtest.h>

#define private public
#define protected public
#include "common/parse_args.h"

namespace zjchain {

namespace common {

namespace test {

class TestParserArgs : public testing::Test {
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

TEST_F(TestParserArgs, WordDup) {
    ParserArgs args_parser;
    args_parser.AddArgType('h', "help", kNoValue);
    args_parser.AddArgType('g', "show_cmd", kMaybeValue);
    args_parser.AddArgType('p', "peer", kMaybeValue);
    args_parser.AddArgType('i', "identity_index", kMaybeValue);
    args_parser.AddArgType('l', "local_port", kMaybeValue);
    args_parser.AddArgType('a', "local_ip", kMaybeValue);
    args_parser.AddArgType('o', "country_code", kMaybeValue);
    args_parser.AddArgType('u', "business", kMaybeValue);
    args_parser.AddArgType('c', "config_path", kMaybeValue);
    args_parser.AddArgType('d', "db_path", kMaybeValue);
    args_parser.AddArgType('L', "log_path", kMaybeValue);
    args_parser.AddArgType('t', "test", kMaybeValue);

    std::string tmp_params1("-h -g 0 -p peer ");
    std::string err_pos1;
    ASSERT_EQ(args_parser.Parse(tmp_params1, err_pos1), kParseFailed);
}

TEST_F(TestParserArgs, NoWord) {
    ParserArgs args_parser;
    args_parser.AddArgType('h', "help", kNoValue);
    args_parser.AddArgType('g', "show_cmd", kMaybeValue);
    args_parser.AddArgType('p', "peer", kMaybeValue);
    args_parser.AddArgType('i', "identity_index", kMaybeValue);
    args_parser.AddArgType('l', "local_port", kMaybeValue);
    args_parser.AddArgType('a', "local_ip", kMaybeValue);
    args_parser.AddArgType('o', "country_code", kMaybeValue);
    args_parser.AddArgType('u', "business", kMaybeValue);
    args_parser.AddArgType('c', "config_path", kMaybeValue);
    args_parser.AddArgType('d', "db_path", kMaybeValue);
    args_parser.AddArgType('L', "log_path", kMaybeValue);
    args_parser.AddArgType('t', "test", kMaybeValue);

    std::string tmp_params1("-f -g 0 -p peer ");
    std::string err_pos1;
    ASSERT_EQ(args_parser.Parse(tmp_params1, err_pos1), kParseFailed);
}

TEST_F(TestParserArgs, NoValue) {
    ParserArgs args_parser;
    args_parser.AddArgType('h', "help", kNoValue);
    args_parser.AddArgType('g', "show_cmd", kMaybeValue);
    args_parser.AddArgType('p', "peer", kMaybeValue);
    args_parser.AddArgType('i', "identity_index", kMaybeValue);
    args_parser.AddArgType('l', "local_port", kMaybeValue);
    args_parser.AddArgType('a', "local_ip", kMaybeValue);
    args_parser.AddArgType('o', "country_code", kMaybeValue);
    args_parser.AddArgType('u', "business", kMaybeValue);
    args_parser.AddArgType('c', "config_path", kMaybeValue);
    args_parser.AddArgType('d', "db_path", kMaybeValue);
    args_parser.AddArgType('L', "log_path", kMaybeValue);
    args_parser.AddArgType('t', "test", kMaybeValue);

    std::string tmp_params1("-f -g 0 -p");
    std::string err_pos1;
    ASSERT_EQ(args_parser.Parse(tmp_params1, err_pos1), kParseFailed);
}

TEST_F(TestParserArgs, MustValue) {
    ParserArgs args_parser;
    args_parser.AddArgType('h', "help", kNoValue);
    args_parser.AddArgType('g', "show_cmd", kMaybeValue);
    args_parser.AddArgType('p', "peer", kMaybeValue);
    args_parser.AddArgType('i', "identity_index", kMaybeValue);
    args_parser.AddArgType('l', "local_port", kMaybeValue);
    args_parser.AddArgType('a', "local_ip", kMaybeValue);
    args_parser.AddArgType('o', "country_code", kMaybeValue);
    args_parser.AddArgType('u', "business", kMaybeValue);
    args_parser.AddArgType('c', "config_path", kMaybeValue);
    args_parser.AddArgType('d', "db_path", kMaybeValue);
    args_parser.AddArgType('L', "log_path", kMaybeValue);
    args_parser.AddArgType('t', "test", kMustValue);

    std::string tmp_params1("-f -g 0 -p 127.0.0.1:100 -t ");
    std::string err_pos1;
    ASSERT_EQ(args_parser.Parse(tmp_params1, err_pos1), kParseFailed);
}

TEST_F(TestParserArgs, MustNoValue) {
    ParserArgs args_parser;
    args_parser.AddArgType('h', "help", kNoValue);
    args_parser.AddArgType('g', "show_cmd", kMaybeValue);
    args_parser.AddArgType('p', "peer", kMaybeValue);
    args_parser.AddArgType('i', "identity_index", kMaybeValue);
    args_parser.AddArgType('l', "local_port", kMaybeValue);
    args_parser.AddArgType('a', "local_ip", kMaybeValue);
    args_parser.AddArgType('o', "country_code", kMaybeValue);
    args_parser.AddArgType('u', "business", kMaybeValue);
    args_parser.AddArgType('c', "config_path", kMaybeValue);
    args_parser.AddArgType('d', "db_path", kMaybeValue);
    args_parser.AddArgType('L', "log_path", kMaybeValue);
    args_parser.AddArgType('t', "test", kMustValue);

    std::string tmp_params1("-h test_help ");
    std::string err_pos1;
    ASSERT_EQ(args_parser.Parse(tmp_params1, err_pos1), kParseFailed);
}

TEST_F(TestParserArgs, SpecialCharStar) {
    ParserArgs args_parser;
    args_parser.AddArgType('h', "help", kNoValue);
    args_parser.AddArgType('g', "show_cmd", kMaybeValue);
    args_parser.AddArgType('p', "peer", kMaybeValue);
    args_parser.AddArgType('i', "identity_index", kMaybeValue);
    args_parser.AddArgType('l', "local_port", kMaybeValue);
    args_parser.AddArgType('a', "local_ip", kMaybeValue);
    args_parser.AddArgType('o', "country_code", kMaybeValue);
    args_parser.AddArgType('u', "business", kMaybeValue);
    args_parser.AddArgType('c', "config_path", kMaybeValue);
    args_parser.AddArgType('d', "db_path", kMaybeValue);
    args_parser.AddArgType('L', "log_path", kMaybeValue);
    args_parser.AddArgType('t', "test", kMustValue);

    std::string tmp_params1("-h test_help\" ");
    std::string err_pos1;
    ASSERT_EQ(args_parser.Parse(tmp_params1, err_pos1), kParseFailed);
}

TEST_F(TestParserArgs, SpecialCharQuote) {
    ParserArgs args_parser;
    args_parser.AddArgType('h', "help", kNoValue);
    args_parser.AddArgType('g', "show_cmd", kMaybeValue);
    args_parser.AddArgType('p', "peer", kMaybeValue);
    args_parser.AddArgType('i', "identity_index", kMaybeValue);
    args_parser.AddArgType('l', "local_port", kMaybeValue);
    args_parser.AddArgType('a', "local_ip", kMaybeValue);
    args_parser.AddArgType('o', "country_code", kMaybeValue);
    args_parser.AddArgType('u', "business", kMaybeValue);
    args_parser.AddArgType('c', "config_path", kMaybeValue);
    args_parser.AddArgType('d', "db_path", kMaybeValue);
    args_parser.AddArgType('L', "log_path", kMaybeValue);
    args_parser.AddArgType('t', "test", kMustValue);

    std::string tmp_params1("-h test_help\" ");
    std::string err_pos1;
    ASSERT_EQ(args_parser.Parse(tmp_params1, err_pos1), kParseFailed);
}

TEST_F(TestParserArgs, AddArgType) {
    ParserArgs args_parser;
    args_parser.AddArgType('h', "help", kNoValue);
    args_parser.AddArgType('g', "show_cmd", kMaybeValue);
    args_parser.AddArgType('p', "peer", kMaybeValue);
    args_parser.AddArgType('i', "identity_index", kMaybeValue);
    args_parser.AddArgType('l', "local_port", kMaybeValue);
    args_parser.AddArgType('a', "local_ip", kMaybeValue);
    args_parser.AddArgType('o', "country_code", kMaybeValue);
    args_parser.AddArgType('u', "business", kMaybeValue);
    args_parser.AddArgType('c', "config_path", kMaybeValue);
    args_parser.AddArgType('d', "db_path", kMaybeValue);
    args_parser.AddArgType('L', "log_path", kMaybeValue);
    args_parser.AddArgType('t', "test", kMaybeValue);

    std::string tmp_params1("-h -g 0 -p peer ");
    std::string err_pos1;
    ASSERT_EQ(args_parser.Parse(tmp_params1, err_pos1), kParseFailed);
    args_parser.result_.clear();
    std::string tmp_params2("-h 1 ");
    ASSERT_EQ(args_parser.Parse(tmp_params2, err_pos1), kParseFailed);
    args_parser.result_.clear();
    std::string tmp_params3(" ");
    ASSERT_EQ(args_parser.Parse(tmp_params3, err_pos1), kParseFailed);
    args_parser.result_.clear();
    std::string tmp_params4(" -g g ");
    ASSERT_EQ(args_parser.Parse(tmp_params4, err_pos1), kParseFailed);
    args_parser.result_.clear();

    std::string tmp_params(
        "-h -g 0 -p 127.0.0.1:1000 -i 1 -l 1000 "
        "-a 127.0.0.1 -o CN -u VPN -c ./test/test.conf "
        "-d ./db -L ./log_path -t 10 ");
    std::string err_pos;
    ASSERT_EQ(args_parser.Parse(tmp_params, err_pos), kParseSuccess);
    ASSERT_TRUE(args_parser.Has("h"));
    int g_val;
    ASSERT_EQ(args_parser.Get("g", g_val), kParseSuccess);
    ASSERT_EQ(g_val, 0);
    std::string p_val;
    ASSERT_EQ(args_parser.Get("p", p_val), kParseSuccess);
    ASSERT_EQ(p_val, "127.0.0.1:1000");
    int i_val;
    ASSERT_EQ(args_parser.Get("i", i_val), kParseSuccess);
    ASSERT_EQ(i_val, 1);
    uint16_t l_val;
    ASSERT_EQ(args_parser.Get("l", l_val), kParseSuccess);
    ASSERT_EQ(l_val, 1000);
    std::string a_val;
    ASSERT_EQ(args_parser.Get("a", a_val), kParseSuccess);
    ASSERT_EQ(a_val, "127.0.0.1");
    std::string o_val;
    ASSERT_EQ(args_parser.Get("o", o_val), kParseSuccess);
    ASSERT_EQ(o_val, "CN");
    std::string u_val;
    ASSERT_EQ(args_parser.Get("u", u_val), kParseSuccess);
    ASSERT_EQ(u_val, "VPN");
    std::string c_val;
    ASSERT_EQ(args_parser.Get("c", c_val), kParseSuccess);
    ASSERT_EQ(c_val, "./test/test.conf");
    std::string d_val;
    ASSERT_EQ(args_parser.Get("d", d_val), kParseSuccess);
    ASSERT_EQ(d_val, "./db");
    std::string L_val;
    ASSERT_EQ(args_parser.Get("L", L_val), kParseSuccess);
    ASSERT_EQ(L_val, "./log_path");
    int t_val;
    ASSERT_EQ(args_parser.Get("t", t_val), kParseSuccess);
    ASSERT_EQ(t_val, 10);
}

}  // namespace test

}  // namespace common

}  // namespace zjchain
