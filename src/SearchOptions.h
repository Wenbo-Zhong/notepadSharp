#pragma once
#include "App.h"
#include <memory>
struct RegexState;
struct PreparedSearch {
    std::string find, replace;
    int flags = 0;
    bool regex = false;
    // Batch operations suppress an empty match immediately after a nonempty match.
    bool allowEmptyAfterMatch = true;
    std::shared_ptr<RegexState> engine;
    bool cached = false;
    int cachedDoc = 0;
    std::string document;
};
bool prepareSearch(const FindRequest &request, PreparedSearch &query, std::string &error,
                   bool replacement = false);

intptr_t searchInTarget(Host &host, int doc, PreparedSearch &query);
intptr_t replaceInTarget(Host &host, int doc, PreparedSearch &query);

intptr_t advanceSearchPosition(Host &host, int doc, intptr_t position, bool forward = true);
