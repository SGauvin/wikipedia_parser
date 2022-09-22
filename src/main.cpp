#include <iostream>
#include <unordered_set>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/dom.h>
#include <lexbor/html/parser.h>
#include "init_curl.h"
#include "thread_safe_queue.h"

using LinksToCurlQueue = ThreadSafeQueueFixedSize<std::string, 200>;
using HtmlToParseQueue = ThreadSafeQueueFixedSize<std::string, 200>;
using LinksToFilterQueue = ThreadSafeQueue<std::string>;

void fetchPages(LinksToCurlQueue& inQueue, HtmlToParseQueue& outQueue)
{
    auto [curl, responseString] = initCurl();
    std::string toFetch;

    while (true)
    {
        inQueue.pop(toFetch);
        auto start = std::chrono::high_resolution_clock::now();
        responseString->clear();
        curl_easy_setopt(curl, CURLOPT_URL, std::string("https://en.wikipedia.org" + toFetch).c_str());
        curl_easy_perform(curl);
        auto end = std::chrono::high_resolution_clock::now();
        std::cout << "Fetched " << toFetch << " in " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";
        outQueue.push(*responseString);
    }
}

void parseHtml(HtmlToParseQueue& inQueue, LinksToFilterQueue& outQueue)
{
    std::string responseString;
    while (true)
    {
        inQueue.pop(responseString);

        auto start = std::chrono::high_resolution_clock::now();

        auto document = lxb_html_document_create();
        auto collection = lxb_dom_collection_make(lxb_dom_interface_document(document), 128);
        auto status = lxb_html_document_parse(document, (const unsigned char*)responseString.c_str(), responseString.size());

        if (status != LXB_STATUS_OK)
        {
            std::cerr << "Error while parsing HTML" << std::endl;
            lxb_dom_collection_destroy(collection, false);
            lxb_html_document_destroy(document);
            continue;
        }

        auto body = lxb_dom_interface_element(document->body);
        status = lxb_dom_elements_by_attr_begin(body, collection, (const lxb_char_t*)"href", 4, (const lxb_char_t*)"/wiki", 5, true);

        if (status != LXB_STATUS_OK)
        {
            std::cerr << "Error while parsing HTML" << std::endl;
            lxb_dom_collection_destroy(collection, false);
            lxb_html_document_destroy(document);
            continue;
        }

        auto num = lxb_dom_collection_length(collection);
        for (size_t i = 0; i < lxb_dom_collection_length(collection); i++)
        {
            auto element = lxb_dom_collection_element(collection, i);
            const char* link = (const char*)lxb_dom_element_get_attribute(element, reinterpret_cast<const unsigned char*>("href"), 4, nullptr);
            outQueue.push(std::string((const char*)link));
        }

        lxb_dom_collection_destroy(collection, false);
        lxb_html_document_destroy(document);

        auto end = std::chrono::high_resolution_clock::now();
        // std::cout << "Parsed " << num << " links in " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms\n";
    }
}

void filterLinks(LinksToFilterQueue& inQueue, LinksToCurlQueue& outQueue)
{
    std::unordered_set<std::string> visited{};
    std::string toFilter;
    std::size_t totalMs;
    std::size_t n;

    while (true)
    {
        auto start = std::chrono::high_resolution_clock::now();
        inQueue.pop(toFilter);

        if (visited.contains(toFilter))
        {
            continue;
        }

        visited.insert(toFilter);
        outQueue.push(toFilter);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        totalMs += duration;
        n++;
        std::cout << "[" << n << "] Speed: " << totalMs / n << " ms\n";
    }
}

int main(int argc, char** argv)
{
    const char* url = argc <= 2 ? "/wiki/Sun" : argv[1];

    LinksToCurlQueue toCurl{};
    HtmlToParseQueue toParse{};
    LinksToFilterQueue toFilter{};

    toCurl.push(std::string(url));

    std::vector<std::thread> threads;
    threads.reserve(60);
    for (auto i = 0UL; i < 40; i++)
    {
        threads.emplace_back(fetchPages, std::ref(toCurl), std::ref(toParse));
    }

    for (auto i = 0UL; i < 10; i++)
    {
        threads.emplace_back(parseHtml, std::ref(toParse), std::ref(toFilter));
    }

    threads.emplace_back(filterLinks, std::ref(toFilter), std::ref(toCurl));

    for (auto& t : threads)
    {
        t.join();
    }
}
