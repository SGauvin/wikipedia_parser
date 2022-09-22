#pragma once

#include <curl/curl.h>
#include <tuple>
#include <string>
#include <memory>
#include <iostream>

static std::size_t writeFunction(void* ptr, std::size_t size, std::size_t nmemb, std::string* data)
{
    data->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

static std::tuple<CURL*, std::unique_ptr<std::string>> initCurl()
{
    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        std::cerr << "Couldn't init curl" << std::endl;
        return {nullptr, nullptr};
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);

    auto responseString = std::make_unique<std::string>();
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, responseString.get());

    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    return {curl, std::move(responseString)};
}
