#pragma once

#include <string>
#include <memory>

#include "common/utils.h"

namespace zjchain {

namespace common {

class ConvertException : public std::exception {
public:
    ConvertException() : err_message_("ZJC_ERROR: convert string to number failed!") {}
    explicit ConvertException(const std::string& err) : err_message_(err) {
    }
    virtual char const* what() const noexcept { return err_message_.c_str(); }

private:
    std::string err_message_;
};

class StringUtil {
public:
    static void Trim(std::string& str);
    
    static bool IsNumeric(const char* str);
    static bool ToBool(const char* str, bool* res);
    static bool ToInt8(const char* str, int8_t* res);
    static bool ToInt16(const char* str, int16_t* res);
    static bool ToInt32(const char* str, int32_t* res);
    static bool ToInt64(const char* str, int64_t* res);
    static bool ToUint8(const char* str, uint8_t* res);
    static bool ToUint16(const char* str, uint16_t* res);
    static bool ToUint32(const char* str, uint32_t* res);
    static bool ToUint64(const char* str, uint64_t* res);
    static bool ToFloat(const char* str, float* res);
    static bool ToDouble(const char* str, double* res);

    static bool IsNumeric(const std::string& str);
    static bool ToBool(const std::string& str, bool* res);
    static bool ToInt8(const std::string& str, int8_t* res);
    static bool ToInt16(const std::string& str, int16_t* res);
    static bool ToInt32(const std::string& str, int32_t* res);
    static bool ToInt64(const std::string& str, int64_t* res);
    static bool ToUint8(const std::string& str, uint8_t* res);
    static bool ToUint16(const std::string& str, uint16_t* res);
    static bool ToUint32(const std::string& str, uint32_t* res);
    static bool ToUint64(const std::string& str, uint64_t* res);
    static bool ToFloat(const std::string& str, float* res);
    static bool ToDouble(const std::string& str, double* res);

    template<typename ... Args>
    static std::string Format(const std::string& format, Args ... args) {
        size_t size = ::snprintf(nullptr, 0, format.c_str(), args ...) + 1;
        std::unique_ptr<char[]> buf(new char[size]);
        ::snprintf(buf.get(), size, format.c_str(), args ...);
        return std::string(buf.get(), buf.get() + size - 1);
    }

private:
    StringUtil() {}
    ~StringUtil() {}

    DISALLOW_COPY_AND_ASSIGN(StringUtil);
};

}

}
