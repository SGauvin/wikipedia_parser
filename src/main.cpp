#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include <curl/curl.h>

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

void run(CURL* curl, const char* url, std::string& responseString)
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
        extractLinks(responseString, links);
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
            currentUrl = toVisit.front();
            toVisit.pop_front();
        } while (visited.contains(currentUrl));
    } while (toVisit.empty() == false);
}

int main(int argc, char** argv)
{
    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        std::cerr << "Couldn't init curl" << std::endl;
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);

    std::string responseString;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseString);

    std::string headerString;
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerString);

    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

    const char* url = argc <= 2 ? "/wiki/Sun" : argv[1];
    run(curl, url, responseString);
}
