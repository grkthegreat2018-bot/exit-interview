#pragma once
#include "common.h"
#include <thread>
#include <mutex>
#include <queue>
#include <map>
#include <atomic>
#include <chrono>
#include <condition_variable>

// Async LLM client. Groq (OpenAI-compatible HTTPS) + local endpoint + offline stub.
// Requests queue onto worker threads so overlapping generations don't block the game.
class LLMClient {
public:
    enum class Provider { Groq, Local, Stub };

    struct Request {
        int id = 0;
        std::string purpose;              // "dialog" | "report" | "facilities" | "frontdesk" | "phonecall" | "phoneeval" | "remix" | "roster"
        std::string systemPrompt;
        std::string user;
        // per-request overrides (<=0 / <0 fall back to configured values)
        int maxTokens = 0;
        float temperature = -1.f;
        // for stub validation: candidate anomalies with keywords
        std::vector<std::pair<std::string, std::vector<std::string>>> candidates;
        std::string acceptLine, failLine;
    };
    struct Result {
        int id = 0;
        bool ok = false;
        bool rateLimited = false;
        std::string text;
        std::string matchedId;   // for validations: which candidate matched
        bool match = false;      // validation verdict
        std::string feedback;
        std::string backend;     // "groq" | "local" | "stub" / "stub:<why>"
        int httpStatus = 0;      // last HTTP status when a real call was attempted
        std::string errDetail;   // transport error (no key material)
    };

    void Configure(Provider p, const std::string& endpoint, const std::string& apiKey,
                   const std::string& model, int maxTokens, float temperature, int timeoutSec,
                   int rateLimitPauseSec);
    void Start(int workers);
    void Shutdown();

    int Submit(Request r);
    bool Poll(int id, Result& out);   // true when result ready (result removed)
    void Cancel(int id);

    Provider GetProvider() const { return provider; }
    std::string ProviderName() const;
    bool RateLimited() const { return rateLimitUntil.load() > Now(); }
    double RateLimitRemaining() const { return std::max(0.0, rateLimitUntil.load() - Now()); }
    bool NoApiKey() const { return provider == Provider::Groq && apiKey.empty(); }

private:
    static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

    void WorkerLoop();
    Result DoRequest(const Request& r);
    Result CallChatAPI(const Request& r);      // groq/local openai-compatible
    Result StubRespond(const Request& r);      // offline fallback
    static bool ParseVerdict(const std::string& text, std::string& matchedId, std::string& feedback, bool& match);

    Provider provider = Provider::Stub;
    std::string endpoint, apiKey, model;
    int maxTokens = 220;
    float temperature = 0.7f;
    int timeoutSec = 25;
    double rateLimitPause = 60.0;

    std::mutex mtx;
    std::queue<Request> pending;
    std::map<int, Result> done;
    std::vector<std::thread> workers;
    std::atomic<bool> stop{false};
    std::atomic<int> nextId{1};
    std::atomic<double> rateLimitUntil{0};
    std::condition_variable cv;
    std::mutex cvMtx;
};
