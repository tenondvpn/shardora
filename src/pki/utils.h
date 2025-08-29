#pragma once

#include <string>

namespace shardora {

namespace pki {

std::string byte2string(const std::string& bytes);
// Function to XOR two strings
std::string xor_strings(const std::string& str1, const std::string& str2);

}

}