#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <curl/curl.h>
#include <lexbor/dom/collection.h>
#include <lexbor/dom/dom.h>
#include <lexbor/html/parser.h>
#include "init_curl.h"
#include "thread_safe_queue.h"

using LinksToCurlThrottleQueue = ThreadSafeQueueFixedSize<std::string, 4096 * 4>;
using LinksToCurlQueue = ThreadSafeQueueFixedSize<std::string, 256>;
using HtmlToParseQueue = ThreadSafeQueueFixedSize<std::pair<std::string, std::string>, 64>;
using LinksToFilterQueue = ThreadSafeQueueFixedSize<std::string, 4096>;
using LinksToDispatchQueue = ThreadSafeQueueFixedSize<std::string, 2048>;
using LinksToSerializeQueue = ThreadSafeQueueFixedSize<std::string, 4096>;
using PagesToSerializeQueue = ThreadSafeQueueFixedSize<std::pair<std::string, std::string>, 256>;

void fetchPages(LinksToCurlQueue& inQueue, HtmlToParseQueue& outQueue, std::atomic<std::uint64_t>& throttleQuantity)
{
    auto [curl, responseString] = initCurl();
    std::string toFetch;

    while (true)
    {
        if (inQueue.pop(toFetch))
        {
            outQueue.quit();
            std::cout << "Terminating fetch" << std::endl;
            break;
        }

        responseString->clear();
        curl_easy_setopt(curl, CURLOPT_URL, std::string("https://en.wikipedia.org" + toFetch).c_str());
        curl_easy_perform(curl);
        auto end = std::chrono::high_resolution_clock::now();
        if (outQueue.push({toFetch, *responseString}))
        {
            inQueue.quit();
            std::cout << "Terminating fetch" << std::endl;
            break;
        }
    }
    curl_easy_cleanup(curl);
}

void throttleFetch(std::chrono::microseconds durationToWaitBetweenSends, std::uint32_t sendCount, LinksToCurlThrottleQueue& inQueue,
                   LinksToCurlQueue& outQueue)
{
    if (durationToWaitBetweenSends.count() == 0.0)
    {
        std::string link;
        while (true)
        {
            if (inQueue.pop(link))
            {
                outQueue.quit();
                break;
            }

            if (outQueue.push(link))
            {
                inQueue.quit();
                break;
            }
        }
        return;
    }

    auto timestamp = std::chrono::high_resolution_clock::now();
    auto timeToSleepTotal = std::chrono::microseconds(0);
    std::string link;
    while (true)
    {
        if (inQueue.pop(link))
        {
            outQueue.quit();
            break;
        }

        if (outQueue.push(link))
        {
            inQueue.quit();
            break;
        }

        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - timestamp);
        auto timeToSleepThisLoop = std::chrono::duration_cast<std::chrono::microseconds>(durationToWaitBetweenSends - duration);

        timeToSleepTotal += timeToSleepThisLoop;
        if (timeToSleepTotal.count() < -1000)
        {
            timeToSleepTotal = std::chrono::microseconds(-1000);
        }
        timestamp = now;

        if (timeToSleepTotal.count() > 0)
        {
            std::this_thread::sleep_for(timeToSleepTotal);
        }
    }
}

void dispatchLinksToCurl(LinksToCurlThrottleQueue& inQueue, HtmlToParseQueue& outQueue, std::atomic<std::uint64_t>& throttleQuantity)
{
    auto [curl, responseString] = initCurl();
    std::string toFetch;

    while (true)
    {
        if (inQueue.pop(toFetch))
        {
            outQueue.quit();
            std::cout << "Terminating fetch" << std::endl;
            break;
        }

        responseString->clear();
        curl_easy_setopt(curl, CURLOPT_URL, std::string("https://en.wikipedia.org" + toFetch).c_str());
        curl_easy_perform(curl);
        auto end = std::chrono::high_resolution_clock::now();
        if (outQueue.push({toFetch, *responseString}))
        {
            inQueue.quit();
            std::cout << "Terminating fetch" << std::endl;
            break;
        }
    }
    curl_easy_cleanup(curl);
}

void parseHtml(HtmlToParseQueue& inQueue, LinksToFilterQueue& outQueue, PagesToSerializeQueue& pagesQueue,
               std::atomic<std::uint64_t>& throttleQuantity, std::uint8_t numberOfCurlThreads)
{
    std::pair<std::string, std::string> fetchedData;
    std::string links;

    auto document = lxb_html_document_create();
    auto collection = lxb_dom_collection_make(lxb_dom_interface_document(document), 128);
    while (true)
    {
        lxb_dom_collection_clean(collection);
        lxb_html_document_clean(document);

        if (inQueue.pop(fetchedData))
        {
            outQueue.quit();
            pagesQueue.quit();
            std::cout << "Terminating parse" << std::endl;
            break;
        }
        const auto& [pageName, responseString] = fetchedData;

        auto status = lxb_html_document_parse(document, (const unsigned char*)responseString.c_str(), responseString.size());

        if (status != LXB_STATUS_OK)
        {
            std::cerr << "Error while parsing HTML" << std::endl;
            continue;
        }

        auto body = lxb_dom_interface_element(document->body);

        // Check if page is error
        std::size_t titleLen;
        const char* title = (const char*)lxb_html_document_title_raw(document, &titleLen);
        if (title != nullptr)
        {
            if (std::string_view(title, titleLen) == "Wikimedia Error")
            {
                // WE ARE GETTING THROTTLED
                std::cout << "GETTING THROTTLED!!!\n";
                throttleQuantity += numberOfCurlThreads * 4;
                // Put back the link in the queue to re fetch later
                outQueue.push(pageName);
                continue;
            }
        }

        // Parse page
        status = lxb_dom_elements_by_attr_begin(body, collection, (const lxb_char_t*)"href", 4, (const lxb_char_t*)"/wiki", 5, true);
        if (status != LXB_STATUS_OK)
        {
            std::cerr << "Error while parsing HTML" << std::endl;
            continue;
        }

        links.clear();
        bool quit = false;
        for (size_t i = 0; i < lxb_dom_collection_length(collection); i++)
        {
            auto element = lxb_dom_collection_element(collection, i);
            const char* link =
                (const char*)lxb_dom_element_get_attribute(element, reinterpret_cast<const unsigned char*>("href"), 4, nullptr);
            if (outQueue.push(std::string((const char*)link)))
            {
                quit = true;
                break;
            }

            links += link;
            links += '\n';
        }

        if (quit)
        {
            inQueue.quit();
            pagesQueue.quit();
            std::cout << "Terminating parse" << std::endl;
            break;
        }

        if (pagesQueue.push({pageName, links}))
        {
            inQueue.quit();
            outQueue.quit();
            std::cout << "Terminating parse" << std::endl;
            break;
        }
    }
    lxb_dom_collection_destroy(collection, true);
    lxb_html_document_destroy(document);
}

void filterLinks(LinksToFilterQueue& inQueue, LinksToDispatchQueue& outQueue, LinksToCurlThrottleQueue& curlQueue,
                 std::condition_variable& deserializeCondition, std::vector<std::string>& disallowedLinks)
{
    std::unordered_set<std::string> visited{};
    std::string toFilter;
    std::size_t totalMs;
    std::size_t n{};

    disallowedLinks.push_back("/wiki/Category:");
    disallowedLinks.push_back("/wiki/File:");
    disallowedLinks.push_back("/wiki/Wikipedia:");

    while (true)
    {
        const float fullness = static_cast<float>(curlQueue.size()) / curlQueue.capacity();
        if (fullness < 0.75f)
        {
            deserializeCondition.notify_one();
        }

        if (inQueue.pop(toFilter))
        {
            outQueue.quit();
            std::cout << "Terminating filter" << std::endl;
            break;
        }

        std::size_t hashtagPos = toFilter.find("#");
        if (hashtagPos != std::string::npos)
        {
            toFilter.resize(hashtagPos);
        }

        if (visited.contains(toFilter))
        {
            continue;
        }

        const bool isDisallowed = std::any_of(disallowedLinks.cbegin(),
                                              disallowedLinks.cend(),
                                              [&toFilter](const auto& disallowedLink) { return toFilter.starts_with(disallowedLink); });

        if (isDisallowed)
        {
            continue;
        }

        visited.insert(toFilter);
        if (outQueue.push(toFilter))
        {
            inQueue.quit();
            std::cout << "Terminating filter" << std::endl;
            break;
        }
    }
}

void dispatchLinks(LinksToDispatchQueue& inQueue, LinksToCurlThrottleQueue& curlQueue, LinksToSerializeQueue& serializeQueue,
                   std::condition_variable& deserializeCondition, std::atomic<bool>& quitDeserialize)
{
    std::string link;
    while (true)
    {
        const float fullness = static_cast<float>(curlQueue.size()) / curlQueue.capacity();
        if (fullness < 0.75f)
        {
            deserializeCondition.notify_one();
        }

        if (inQueue.pop(link))
        {
            curlQueue.quit();
            serializeQueue.quit();
            quitDeserialize = true;
            deserializeCondition.notify_all();
            std::cout << "Terminating dispatch" << std::endl;
            break;
        }

        if (fullness > 0.98f)
        {
            if (serializeQueue.push(link))
            {
                inQueue.quit();
                curlQueue.quit();
                quitDeserialize = true;
                deserializeCondition.notify_all();
                std::cout << "Terminating dispatch" << std::endl;
                break;
            }
        }
        else
        {
            if (curlQueue.push(link))
            {
                inQueue.quit();
                serializeQueue.quit();
                quitDeserialize = true;
                deserializeCondition.notify_all();
                std::cout << "Terminating dispatch" << std::endl;
                break;
            }
        }
    }
}

void serializeLinks(LinksToSerializeQueue& inQueue, const std::string& linksFolder)
{
    std::atomic<std::uint32_t> counter = 0;
    std::vector<std::string> links;
    links.resize(1024);

    while (true)
    {
        if (inQueue.pop(links))
        {
            std::cout << "Terminating serialize" << std::endl;
            break;
        }

        std::uint32_t fileNumber = counter.fetch_add(1);
        std::string file = std::filesystem::path(linksFolder) / std::filesystem::path(std::to_string(fileNumber));
        std::ofstream stream(file);
        if (!stream)
        {
            std::cerr << "Couldn't create file " << fileNumber << std::endl;
            exit(1);
        }

        for (const auto& link : links)
        {
            stream << link << '\n';
        }
    }
}

void deserializeLinks(LinksToDispatchQueue& toDispatch, LinksToCurlThrottleQueue& curlQueue, std::unordered_set<std::string>& activeFiles,
                      std::mutex& filemutex, std::mutex& conditionMutex, std::condition_variable& deserializeCondition,
                      std::atomic<bool>& quitDeserialize, const std::string& linksFolder)
{
    std::string pathStr;
    while (true)
    {
        std::unique_lock<std::mutex> guard(conditionMutex);
        deserializeCondition.wait(guard,
                                  [&]
                                  {
                                      const float fullness = static_cast<float>(curlQueue.size() - 1) / curlQueue.capacity();
                                      return fullness < 0.80f || quitDeserialize == true;
                                  });

        if (quitDeserialize == true)
        {
            std::cout << "Terminating deserialize" << std::endl;
            toDispatch.quit();
            curlQueue.quit();
            break;
        }

        pathStr.clear();
        for (const auto& path : std::filesystem::directory_iterator(linksFolder))
        {
            pathStr = path.path().string();
            {
                std::lock_guard<std::mutex> fileGuard(filemutex);
                if (activeFiles.contains(pathStr) == false)
                {
                    activeFiles.insert(pathStr);
                    break;
                }
            }
        }

        if (pathStr.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::ifstream stream(pathStr);
        if (!stream)
        {
            std::cerr << "Couldn't open file " << pathStr << std::endl;
            exit(1);
        }

        std::string link;
        bool quit = false;
        std::cout << "Deserializing page << " << pathStr << "\n";
        while (stream >> link)
        {
            if (curlQueue.push(link))
            {
                quit = true;
                break;
            }
        }

        if (quit)
        {
            toDispatch.quit();
            std::cout << "Terminating deserialize" << std::endl;
            break;
        }

        if (!std::filesystem::remove(pathStr))
        {
            std::cerr << "Couldn't delete file " << pathStr << std::endl;
            exit(1);
        }

        {
            std::lock_guard<std::mutex> fileGuard(filemutex);
            activeFiles.erase(pathStr);
        }
    }
}

void serializePage(PagesToSerializeQueue& inQueue, const std::string& dataFolder, std::atomic<std::uint32_t>& pageCounter)
{
    std::pair<std::string, std::string> toSerialize;

    auto start = std::chrono::high_resolution_clock::now();
    while (true)
    {
        if (inQueue.pop(toSerialize))
        {
            std::cout << "Terminating serialize page" << std::endl;
            break;
        }
        auto& [pageName, pageLinks] = toSerialize;
        std::replace(pageName.begin(), pageName.end(), '/', '_');

        std::string_view realPageName = [&]
        {
            if (toSerialize.first.starts_with("_wiki_"))
            {
                return std::string_view(toSerialize.first.begin() + 6, toSerialize.first.end());
            }
            return std::string_view(toSerialize.first);
        }();

        auto path = std::filesystem::path(dataFolder) / realPageName;
        std::ofstream stream(path);
        if (!stream)
        {
            std::cerr << "Can't create file " << path << std::endl;
            continue;
        }

        stream << pageLinks;
        pageCounter++;
    }
}

std::pair<std::string, std::string> prepDataFolder(const std::string_view dataFolder)
{
    if (std::filesystem::exists(dataFolder))
    {
        std::filesystem::remove_all(dataFolder);
    }

    std::filesystem::create_directory(dataFolder);

    std::string linksToCurl = std::filesystem::path(dataFolder) / "to_visit";
    std::filesystem::create_directory(linksToCurl);

    std::string pagesData = std::filesystem::path(dataFolder) / "wiki_data";
    std::filesystem::create_directory(pagesData);

    return {linksToCurl, pagesData};
}

auto parseRobotsTxt()
{
    auto [curl, responseString] = initCurl();
    curl_easy_setopt(curl, CURLOPT_URL, "https://en.wikipedia.org/robots.txt");
    curl_easy_perform(curl);
    std::stringstream stream(*responseString);

    bool foundOurUserAgent = false;
    std::string line;
    std::vector<std::string> disallowedLinks;
    while (std::getline(stream, line))
    {
        if (foundOurUserAgent == false)
        {
            if (line == "User-agent: *")
            {
                foundOurUserAgent = true;
            }
        }
        else
        {
            if (line.starts_with("#") || line.starts_with(" #"))
            {
                continue;
            }
            if (line.starts_with("User-agent"))
            {
                break;
            }
            if (line.starts_with("Disallow: "))
            {
                std::string disallowedLink = line.substr(strlen("Disallow: "), std::string::npos);
                disallowedLinks.push_back(disallowedLink);
            }
        }
    }

    curl_easy_cleanup(curl);
    return disallowedLinks;
}

int main(int argc, char** argv)
{
    std::vector<std::string> disallowedLinks = parseRobotsTxt();
    auto [linksFolder, dataFolder] = prepDataFolder(argc < 3 ? "data/" : argv[2]);

    std::vector<std::thread> threads;
    threads.reserve(64);

    // Throttle
    LinksToCurlQueue toCurl{};
    LinksToCurlThrottleQueue toCurlThrottle{};
    toCurlThrottle.push(std::string(argc <= 2 ? "/wiki/Sun" : argv[1]));
    threads.emplace_back(throttleFetch, std::chrono::milliseconds(0), 2, std::ref(toCurlThrottle), std::ref(toCurl));

    // Curl threads
    std::atomic<std::uint64_t> throttleQuantity = 0;
    HtmlToParseQueue toParse{};
    const std::uint8_t numberOfCurlThreads = 30;
    for (auto i = 0UL; i < numberOfCurlThreads; i++)
    {
        threads.emplace_back(fetchPages, std::ref(toCurl), std::ref(toParse), std::ref(throttleQuantity));
    }

    // Parsing threads
    PagesToSerializeQueue pagesToSerialize{};
    LinksToFilterQueue toFilter{};
    for (auto i = 0UL; i < 10; i++)
    {
        threads.emplace_back(
            parseHtml, std::ref(toParse), std::ref(toFilter), std::ref(pagesToSerialize), std::ref(throttleQuantity), numberOfCurlThreads);
    }

    // Filter thread (did we already visit that link?)
    std::condition_variable deserializeCondition;
    LinksToDispatchQueue toDispatch{};
    threads.emplace_back(filterLinks,
                         std::ref(toFilter),
                         std::ref(toDispatch),
                         std::ref(toCurlThrottle),
                         std::ref(deserializeCondition),
                         std::ref(disallowedLinks));

    // Dispatching threads (should a link be serialized or be kept in memory?)
    LinksToSerializeQueue toSerialize{};
    std::atomic<bool> quitDeserialize = false;
    threads.emplace_back(dispatchLinks,
                         std::ref(toDispatch),
                         std::ref(toCurlThrottle),
                         std::ref(toSerialize),
                         std::ref(deserializeCondition),
                         std::ref(quitDeserialize));

    // Serializing threads
    threads.emplace_back(serializeLinks, std::ref(toSerialize), std::ref(linksFolder));

    // Deserialize (when we are running out of links to visit in the memory, fetch them from the disk)
    std::unordered_set<std::string> activeFiles;
    std::mutex fileMutex;
    std::mutex conditionMutex;
    threads.emplace_back(deserializeLinks,
                         std::ref(toDispatch),
                         std::ref(toCurlThrottle),
                         std::ref(activeFiles),
                         std::ref(fileMutex),
                         std::ref(conditionMutex),
                         std::ref(deserializeCondition),
                         std::ref(quitDeserialize),
                         std::ref(linksFolder));

    std::atomic<std::uint32_t> pageSerializingCounter = 0;
    threads.emplace_back(serializePage, std::ref(pagesToSerialize), std::ref(dataFolder), std::ref(pageSerializingCounter));
    auto timestampAtStart = std::chrono::high_resolution_clock::now();

    // Every 5 seconds check if we have finished
    std::uint8_t count = 0;
    std::uint64_t totalPagesSerialized = 0;
    while (true)
    {
        auto start = std::chrono::high_resolution_clock::now();

        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (toCurlThrottle.size() == 0 && pagesToSerialize.size() == 0)
        {
            count++;
            if (count == 3)
            {
                break;
            }
        }
        else
        {
            count = 0;
        }

        std::uint32_t pageCount = pageSerializingCounter.load(std::memory_order_relaxed);
        while(!pageSerializingCounter.compare_exchange_weak(pageCount, 0, std::memory_order_relaxed, std::memory_order_relaxed));
        totalPagesSerialized += pageCount;

        auto now = std::chrono::high_resolution_clock::now();
        auto durationSinceLast = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
        auto averageDuration = durationSinceLast / pageCount;
        auto durationSinceStart = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(now - timestampAtStart).count());

        std::cout << "Queues fullness:\n";
        std::cout << "To curl (throttled):      " << static_cast<float>(toCurlThrottle.size()) / toCurlThrottle.capacity() << '\n';
        std::cout << "To curl :                 " << static_cast<float>(toCurl.size()) / toCurl.capacity() << '\n';
        std::cout << "To parse:                 " << static_cast<float>(toParse.size()) / toParse.capacity() << '\n';
        std::cout << "To filter:                " << static_cast<float>(toFilter.size()) / toFilter.capacity() << '\n';
        std::cout << "To dispatch:              " << static_cast<float>(toDispatch.size()) / toDispatch.capacity() << '\n';
        std::cout << "To serialize:             " << static_cast<float>(toSerialize.size()) / toSerialize.capacity() << '\n';
        std::cout << "To serialize pages:       " << static_cast<float>(pagesToSerialize.size()) / pagesToSerialize.capacity() << '\n';
        std::cout << "Number of fetches:        " << pageCount << " in the last " << durationSinceLast / 1000 << "s\n";
        std::cout << "Fetch duration average:   " << averageDuration << "ms\n";
        std::cout << "Total pages serialized:   " << totalPagesSerialized << '\n';
        std::cout << "Time elapsed since start: " << durationSinceStart / 1000 << "s\n";
        std::cout << "---\n";
    }

    std::cout << "Terminating" << std::endl;
    toDispatch.quit();

    for (auto& t : threads)
    {
        t.join();
    }
}
