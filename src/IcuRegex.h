#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
class IcuRegex {
  public:
    IcuRegex(const std::string &pattern, bool matchCase, bool dotAll);
    ~IcuRegex();
    void load(const std::string &text);
    bool find(int64_t start, int64_t end, std::vector<std::pair<int64_t, int64_t>> &groups);
    bool wholeWord(int64_t start, int64_t end) const;
    const std::vector<std::pair<std::wstring, int>> &names() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
