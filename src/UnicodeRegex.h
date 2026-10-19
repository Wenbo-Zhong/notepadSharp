#pragma once
#include <string>
// Adapt scalar atoms to the UTF-16 Boost engine without adding capture groups.
std::wstring prepareUnicodeRegex(const std::wstring &source);
