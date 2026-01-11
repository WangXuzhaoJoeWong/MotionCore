#include "internal/config_fetcher.h"

#include <curl/curl.h>
#include <iostream>
#include <sstream>

namespace {
size_t write_to_string(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    auto* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), total_size);
    return total_size;
}

std::unordered_map<std::string, std::string> parse_body(const std::string& body) {
    std::unordered_map<std::string, std::string> result;
    std::istringstream iss(body);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) {
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos || pos == 0 || pos == line.size() - 1) {
            continue;  // skip malformed line
        }
        auto key = line.substr(0, pos);
        auto value = line.substr(pos + 1);
        result[std::move(key)] = std::move(value);
    }
    return result;
}
}  // namespace

std::unordered_map<std::string, std::string> fetch_kv_over_http(const std::string& url, long timeout_ms) {
    std::unordered_map<std::string, std::string> empty;
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "config_fetcher: failed to init curl" << std::endl;
        return empty;
    }

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // avoid signals on timeouts

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "config_fetcher: curl error " << res << std::endl;
        return empty;
    }

    const bool is_file_url = url.rfind("file://", 0) == 0;
    if (!is_file_url) {
        if (http_code < 200 || http_code >= 300) {
            std::cerr << "config_fetcher: http status " << http_code << std::endl;
            return empty;
        }
    }

    return parse_body(response_body);
}
