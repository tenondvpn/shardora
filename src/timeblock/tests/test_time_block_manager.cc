#include <stdlib.h>
#include <math.h>

#include <iostream>

#include <gtest/gtest.h>

#include "bzlib.h"

#define private public
#include "timeblock/time_block_manager.h"
#include "timeblock/time_block_utils.h"

namespace zjchain {

namespace tmblock {

namespace test {

class TestTimeBlockManager : public testing::Test {
public:
    static void WriteDefaultLogConf(
        const std::string& log_conf_path,
        const std::string& log_path) {
        FILE* file = NULL;
        file = fopen(log_conf_path.c_str(), "w");
        if (file == NULL) {
            return;
        }
        std::string log_str = ("# log4cpp.properties\n"
            "log4cpp.rootCategory = WARN\n"
            "log4cpp.category.sub1 = WARN, programLog\n"
            "log4cpp.appender.rootAppender = ConsoleAppender\n"
            "log4cpp.appender.rootAppender.layout = PatternLayout\n"
            "log4cpp.appender.rootAppender.layout.ConversionPattern = %d [%p] %m%n\n"
            "log4cpp.appender.programLog = RollingFileAppender\n"
            "log4cpp.appender.programLog.fileName = ") + log_path + "\n" +
            std::string("log4cpp.appender.programLog.maxFileSize = 1073741824\n"
                "log4cpp.appender.programLog.maxBackupIndex = 1\n"
                "log4cpp.appender.programLog.layout = PatternLayout\n"
                "log4cpp.appender.programLog.layout.ConversionPattern = %d [%p] %m%n\n");
        fwrite(log_str.c_str(), log_str.size(), 1, file);
        fclose(file);
    }

    static void SetUpTestCase() {    
        common::global_stop = true;
        std::string config_path_ = "./";
        std::string conf_path = config_path_ + "/tenon.conf";
        std::string log_conf_path = config_path_ + "/log4cpp.properties";
        std::string log_path = config_path_ + "/tenon.log";
        WriteDefaultLogConf(log_conf_path, log_path);
        log4cpp::PropertyConfigurator::configure(log_conf_path);
    }

    static void TearDownTestCase() {
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

private:
};

TEST_F(TestTimeBlockManager, LeaderNewTimeBlockValid) {
    TimeBlockManager tmblock_manager;
    tmblock_manager.latest_time_block_height_ = 0;
    tmblock_manager.latest_time_block_tm_ = common::TimeUtils::TimestampSeconds() - common::kTimeBlockCreatePeriodSeconds;
    std::cout << "tmblock_manager.latest_time_block_tm_: " << tmblock_manager.latest_time_block_tm_ << std::endl;
    tmblock_manager.latest_time_blocks_.push_back(tmblock_manager.latest_time_block_tm_);
    uint64_t new_tm;
    tmblock_manager.LeaderNewTimeBlockValid(&new_tm);
    std::cout << "new_tm: " << new_tm << std::endl;
    for (uint32_t i = 0; i < 10; ++i) {
        tmblock_manager.latest_time_block_tm_ = new_tm;
        tmblock_manager.latest_time_blocks_.push_back(new_tm);
        tmblock_manager.LeaderNewTimeBlockValid(&new_tm);
        std::cout << i <<" new_tm: " << new_tm << std::endl;
    }
}

}  // namespace test

}  // namespace tmblock

}  // namespace zjchain
