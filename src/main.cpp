#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <memory.h>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include <curl/curl.h>
#include <lexbor/dom/dom.h>
#include <lexbor/html/parser.h>
#include "lexbor/dom/collection.h"

std::size_t writeFunction(void* ptr, std::size_t size, std::size_t nmemb, std::string* data)
{
    data->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

void run(CURL* curl, const char* url, std::string& responseString)
{
    std::deque<std::string> toVisit{};
    std::string currentUrl = std::string(url);

    do
    {
        auto start = std::chrono::high_resolution_clock::now();
        responseString.clear();
        curl_easy_setopt(curl, CURLOPT_URL, std::string("https://en.wikipedia.org" + currentUrl).c_str());
        curl_easy_perform(curl);

        {
            auto document = lxb_html_document_create();
            auto collection = lxb_dom_collection_make(lxb_dom_interface_document(document), 128);
            auto status = lxb_html_document_parse(document, (const unsigned char*)responseString.c_str(), responseString.size());

            if (status != LXB_STATUS_OK)
            {
                std::cerr << "Error while parsing HTML" << std::endl;
                continue;
            }

            auto body = lxb_dom_interface_element(document->body);
            status = lxb_dom_elements_by_attr_begin(body, collection, (const lxb_char_t*)"href", 4, (const lxb_char_t*)"/wiki", 5, true);

            if (status != LXB_STATUS_OK)
            {
                std::cerr << "Error while parsing HTML" << std::endl;
                continue;
            }

            for (size_t i = 0; i < lxb_dom_collection_length(collection); i++)
            {
                auto element = lxb_dom_collection_element(collection, i);
                const char* link = (const char*)lxb_dom_element_get_attribute(element, reinterpret_cast<const unsigned char*>("href"), 4, nullptr);
                toVisit.insert(toVisit.end(), std::string((const char*)link));
            }

            lxb_dom_collection_destroy(collection, false);
            lxb_html_document_destroy(document);
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "Visited " << currentUrl << " in " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";

        currentUrl = toVisit.front();
        toVisit.pop_front();
    } while (toVisit.empty() == false);
}

std::tuple<CURL*, std::unique_ptr<std::string>> initCurl()
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

int main(int argc, char** argv)
{
    std::vector<decltype(initCurl())> curls;
    for (std::size_t i = 0; i < 1; i++)
    {
        curls.push_back(initCurl());
    }
    const char* url = argc <= 2 ? "/wiki/Sun" : argv[1];

    run(std::get<0>(curls[0]), url, *std::get<1>(curls[0]));

    // auto files = std::to_array<std::string>({"<html><a href=\"/test\"></a></html>", "<html><body></body></html>"});
    // auto document = lxb_html_document_create();
    // auto collection = lxb_dom_collection_make(lxb_dom_interface_document(document), 128);
    //
    // for (const auto& file : files)
    // {
    //     auto status = lxb_html_document_parse(document, reinterpret_cast<const unsigned char*>(file.c_str()), file.size());
    //     if (status != LXB_STATUS_OK)
    //     {
    //         std::cerr << "Error while parsing HTML" << std::endl;
    //         continue;
    //     }
    //
    //     auto body = lxb_dom_interface_element(document->body);
    //     status = lxb_dom_elements_by_attr_begin(body, collection, (const lxb_char_t*)"href", 4, (const lxb_char_t*)"/test", 5, true);
    //
    //     if (status != LXB_STATUS_OK)
    //     {
    //         std::cerr << "Error while parsing HTML" << std::endl;
    //         continue;
    //     }
    //
    //     for (size_t i = 0; i < lxb_dom_collection_length(collection); i++)
    //     {
    //         auto element = lxb_dom_collection_element(collection, i);
    //         const unsigned char* link = lxb_dom_element_get_attribute(element, reinterpret_cast<const unsigned char*>("href"), 4, nullptr);
    //         std::cout << link << std::endl;
    //     }
    //
    //     lxb_dom_collection_clean(collection);
    //     lxb_html_document_clean(document);
    // }
    // lxb_dom_collection_destroy(collection, false);
    // lxb_html_document_destroy(document);
}
