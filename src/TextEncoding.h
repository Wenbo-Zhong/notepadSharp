#pragma once
#include <string>
namespace TextEncoding {
bool decode(const std::string &bytes, std::string &text, std::string &encoding, std::string &error);
bool encode(const std::string &text, const std::string &encoding, std::string &bytes, std::string &error);
} // namespace TextEncoding
