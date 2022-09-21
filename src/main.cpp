#include <algorithm>
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
#include <lexbor/html/parser.h>
#include <lexbor/dom/dom.h>

std::size_t writeFunction(void* ptr, std::size_t size, std::size_t nmemb, std::string* data)
{
    data->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

void extractLinks(const std::string& htmlContent, std::vector<std::string>& links) noexcept
{
    links.clear();
    std::size_t searchStart{};
    while (true)
    {
        std::size_t tagStart = htmlContent.find("<a href=\"", searchStart);
        std::size_t quoteStart = htmlContent.find("\"", tagStart);
        std::size_t quoteEnd = htmlContent.find("\"", quoteStart + 1);

        if (tagStart == std::string::npos || quoteStart == std::string::npos || quoteEnd == std::string::npos)
        {
            return;
        }

        searchStart = quoteEnd + 1;
        std::string_view ref(htmlContent.c_str() + quoteStart + 1, htmlContent.c_str() + quoteEnd);
        if (ref.starts_with("/wiki/"))
        {
            links.push_back(std::string(ref));
        }
    }
}

void extractLinksLegit(const std::string& htmlContent, std::vector<std::string>& links, lxb_html_document_t& document) noexcept
{
    links.clear();
    std::cout << "test" << std::endl;
    auto status = lxb_html_document_parse(&document, reinterpret_cast<const std::uint8_t*>(htmlContent.c_str()), htmlContent.size());
    std::cout << "test2" << std::endl;
    if (status != LXB_STATUS_OK)
    {
        std::cerr << "ntm" << std::endl;
        return;
    }

    auto body = lxb_dom_interface_document(document.body);
    auto collection = lxb_dom_collection_make(&document.dom_document, 128);

    if (collection == NULL) {
        std::cerr << "Bruh" << std::endl;
        return;
    }

    std::cout << "Fuck you" <<std::endl;
}

void run(CURL* curl, const char* url, std::string& responseString, lxb_html_document_t& document)
{
    std::unordered_set<std::string> visited{};
    std::deque<std::string> toVisit{};

    std::string currentUrl = std::string(url);

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::string> links{};
    links.reserve(4096);

    do
    {
        curl_easy_setopt(curl, CURLOPT_URL, std::string("https://en.wikipedia.org" + currentUrl).c_str());
        curl_easy_perform(curl);
        visited.insert(currentUrl);
        // extractLinks(responseString, links);
        extractLinksLegit(responseString, links, document);
        responseString.clear();
        toVisit.insert(toVisit.end(), links.begin(), links.end());

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        start = end;

        std::cout << "[" << visited.size() << "] [" << links.size() << "] "
                  << "Visited " << currentUrl << " in " << elapsed.count() << "ms" << std::endl;
        std::cout << "To Visit: " << toVisit.size() << std::endl;

        do
        {
            if (toVisit.empty())
            {
                break;
            }
            currentUrl = toVisit.front();
            toVisit.pop_front();
        } while (visited.contains(currentUrl));
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
    auto document = lxb_html_document_create();

    run(std::get<0>(curls[0]), url, *std::get<1>(curls[0]), *document);
    lxb_html_document_destroy(document);
}
