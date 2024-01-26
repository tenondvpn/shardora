#include "common/encode.h"

namespace zjchain {

namespace common {

const char kHexChar[] = "0123456789abcdef";
const char kHexLookup[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7,  8,  9,  0,  0,  0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 11, 12, 13, 14, 15 };
const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char kPadCharacter('=');

std::string Encode::HexEncode(const std::string& str) {
    std::string hex_output(str.size() * 2, 0);
    size_t j = 0;
    for (size_t i = 0; i < str.size(); ++i) {
        hex_output[j++] = kHexChar[static_cast<unsigned char>(str[i]) / 16];
        hex_output[j++] = kHexChar[static_cast<unsigned char>(str[i]) % 16];
    }
    return hex_output;
}

std::string Encode::HexDecode(const std::string& str) {
    if (str.size() % 2 != 0) {
        return "";
    }

    std::string non_hex_output(str.size() / 2, 0);
    size_t j = 0;
    for (size_t i = 0; i < (str.size() / 2); ++i) {
        non_hex_output[i] = (kHexLookup[static_cast<int>(str[j++])] << 4);
        non_hex_output[i] |= kHexLookup[static_cast<int>(str[j++])];
    }
    return non_hex_output;
}

std::string Encode::HexSubstr(const std::string& str) {
    size_t non_hex_size(str.size());
    if (non_hex_size < 7) {
        return HexEncode(str);
    }

    std::string hex(14, 0);
    size_t non_hex_index(0), hex_index(0);
    for (; non_hex_index != 3; ++non_hex_index) {
        hex[hex_index++] = kHexChar[static_cast<unsigned char>(str[non_hex_index]) / 16];
        hex[hex_index++] = kHexChar[static_cast<unsigned char>(str[non_hex_index]) % 16];
    }
    hex[hex_index++] = '.';
    hex[hex_index++] = '.';
    non_hex_index = non_hex_size - 3;
    for (; non_hex_index != non_hex_size; ++non_hex_index) {
        hex[hex_index++] = kHexChar[static_cast<unsigned char>(str[non_hex_index]) / 16];
        hex[hex_index++] = kHexChar[static_cast<unsigned char>(str[non_hex_index]) % 16];
    }
    return hex;
}

std::string Encode::Base64Encode(const std::string& str) {
    std::basic_string<unsigned char> encoded_string(
        ((str.size() / 3) + (str.size() % 3 > 0)) * 4, 0);
    int32_t temp;
    auto cursor = std::begin(reinterpret_cast<const std::basic_string<unsigned char>&>(str));
    size_t i = 0;
    size_t common_output_size((str.size() / 3) * 4);
    while (i < common_output_size) {
        temp = (*cursor++) << 16;
        temp += (*cursor++) << 8;
        temp += (*cursor++);
        encoded_string[i++] = kBase64Alphabet[(temp & 0x00FC0000) >> 18];
        encoded_string[i++] = kBase64Alphabet[(temp & 0x0003F000) >> 12];
        encoded_string[i++] = kBase64Alphabet[(temp & 0x00000FC0) >> 6];
        encoded_string[i++] = kBase64Alphabet[(temp & 0x0000003F)];
    }
    switch (str.size() % 3) {
    case 1:
        temp = (*cursor++) << 16;
        encoded_string[i++] = kBase64Alphabet[(temp & 0x00FC0000) >> 18];
        encoded_string[i++] = kBase64Alphabet[(temp & 0x0003F000) >> 12];
        encoded_string[i++] = kPadCharacter;
        encoded_string[i++] = kPadCharacter;
        break;
    case 2:
        temp = (*cursor++) << 16;
        temp += (*cursor++) << 8;
        encoded_string[i++] = kBase64Alphabet[(temp & 0x00FC0000) >> 18];
        encoded_string[i++] = kBase64Alphabet[(temp & 0x0003F000) >> 12];
        encoded_string[i++] = kBase64Alphabet[(temp & 0x00000FC0) >> 6];
        encoded_string[i++] = kPadCharacter;
        break;
    }
    return std::string(std::begin(encoded_string), std::end(encoded_string));
}

std::string Encode::Base64Decode(const std::string& str) {
    if (str.size() % 4 != 0) {
        return "";
    }

    size_t padding = 0;
    if (str.size() > 0) {
        if (str[str.size() - 1] == static_cast<size_t>(kPadCharacter))
            ++padding;
        if (str[str.size() - 2] == static_cast<size_t>(kPadCharacter))
            ++padding;
    }

    std::string decoded_bytes;
    decoded_bytes.reserve(((str.size() / 4) * 3) - padding);
    uint32_t temp = 0;
    auto cursor = std::begin(str);
    while (cursor < std::end(str)) {
        for (size_t quantum_position = 0; quantum_position < 4; ++quantum_position) {
            temp <<= 6;
            if (*cursor >= 0x41 && *cursor <= 0x5A) {
                temp |= *cursor - 0x41;
            } else if (*cursor >= 0x61 && *cursor <= 0x7A) {
                temp |= *cursor - 0x47;
            } else if (*cursor >= 0x30 && *cursor <= 0x39) {
                temp |= *cursor + 0x04;
            } else if (*cursor == 0x2B) {
                temp |= 0x3E;
            } else if (*cursor == 0x2F) {
                temp |= 0x3F;
            }  else if (*cursor == kPadCharacter) {
                switch (std::end(str) - cursor) {
                case 1:
                    decoded_bytes.push_back((temp >> 16) & 0x000000FF);
                    decoded_bytes.push_back((temp >> 8) & 0x000000FF);
                    return decoded_bytes;
                case 2:
                    decoded_bytes.push_back((temp >> 10) & 0x000000FF);
                    return decoded_bytes;
                default:
                    return "";
                }
            } else {
                return "";
            }
            ++cursor;
        }
        decoded_bytes.push_back((temp >> 16) & 0x000000FF);
        decoded_bytes.push_back((temp >> 8) & 0x000000FF);
        decoded_bytes.push_back(temp & 0x000000FF);
    }
    return decoded_bytes;
}

std::string Encode::Base64Substr(const std::string& str) {
    std::string base64(Base64Encode(str));
    if (base64.size() > 16) {
        return (base64.substr(0, 7) + ".." + base64.substr(base64.size() - 7));
    }
    return base64;
}

}  // namespace common

}  // namespace zjchain
