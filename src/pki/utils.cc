#include "pki/utils.h"

namespace shardora {

namespace pki {

std::string byte2string(const std::string& bytes) {
  std::string result;
  for (unsigned char byte : bytes) {
    char hex[3];
    std::snprintf(hex, sizeof(hex), "%02x", byte);
    result += hex;
  }
  return result;
}

std::string xor_strings(const std::string& str1, const std::string& str2) {
  if (str1.empty() || str2.empty()) {
    return "";
  }

  std::string result;
  size_t max_length = std::max(str1.size(), str2.size());
  result.reserve(max_length);

  for (size_t i = 0; i < max_length; ++i) {
    char c1 = (i < str1.size()) ? str1[i] : '\0';
    char c2 = (i < str2.size()) ? str2[i] : '\0';
    result.push_back(c1 ^ c2);
  }

  return result;
}

}

}