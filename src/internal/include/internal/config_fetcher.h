#pragma once

#include <string>
#include <unordered_map>

// Lightweight HTTP config fetcher using libcurl.
// Expected payload: text body with lines of `key=value`; empty lines ignored.
// Returns key/value map; on failure returns empty map and writes error to stderr.
std::unordered_map<std::string, std::string> fetch_kv_over_http(const std::string& url, long timeout_ms = 2000);
