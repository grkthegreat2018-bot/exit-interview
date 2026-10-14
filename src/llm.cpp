#include "llm.h"
#include <httplib.h>

void LLMClient::Configure(Provider p, const std::string& ep, const std::string& key,
                          const std::string& mdl, int mt, float temp, int tmo, int rlPause) {
    provider = p; endpoint = ep; apiKey = key; model = mdl;
    maxTokens = mt; temperature = temp; timeoutSec = tmo;
    rateLimitPause = (double)rlPause;
}

void LLMClient::Start(int nWorkers) {
    stop = false;
    for (int i = 0; i < nWorkers; i++) workers.emplace_back([this] { WorkerLoop(); });
}
void LLMClient::Shutdown() {
    {
        std::lock_guard<std::mutex> lk(cvMtx);
        stop = true;
    }
    cv.notify_all();
    for (auto& t : workers) if (t.joinable()) t.join();
    workers.clear();
}

int LLMClient::Submit(Request r) {
    r.id = nextId++;
    {
        std::lock_guard<std::mutex> lk(cvMtx);
        pending.push(std::move(r));
    }
    cv.notify_one();
    return r.id;
}

bool LLMClient::Poll(int id, Result& out) {
    std::lock_guard<std::mutex> lk(mtx);
    auto it = done.find(id);
    if (it == done.end()) return false;
    out = it->second;
    done.erase(it);
    return true;
}
void LLMClient::Cancel(int id) {
    std::lock_guard<std::mutex> lk(mtx);
    done.erase(id);
}

std::string LLMClient::ProviderName() const {
    switch (provider) {
        case Provider::Groq: return "groq";
        case Provider::Local: return "local";
        default: return "stub";
    }
}

void LLMClient::WorkerLoop() {
    while (true) {
        Request r;
        {
            std::unique_lock<std::mutex> lk(cvMtx);
            cv.wait(lk, [&] { return stop || !pending.empty(); });
            if (stop && pending.empty()) return;
            r = pending.front();
            pending.pop();
        }
        Result res = DoRequest(r);
        res.id = r.id;
        {
            std::lock_guard<std::mutex> lk(mtx);
            done[res.id] = std::move(res);
        }
    }
}

LLMClient::Result LLMClient::DoRequest(const Request& r) {
    auto stub = [&](const char* why) {
        Result s = StubRespond(r);
        s.id = r.id;
        s.backend = std::string("stub:") + why;
        TraceLog(LOG_WARNING, "[llm] %s request -> %s", r.purpose.c_str(), s.backend.c_str());
        return s;
    };
    if (provider == Provider::Stub) return stub("provider");
    if (provider == Provider::Groq && apiKey.empty()) return stub("no_api_key");
    if (RateLimited()) return stub("rate_limited");
    Result res = CallChatAPI(r);
    if (res.rateLimited) {
        rateLimitUntil = Now() + rateLimitPause;
        // fall back to stub for this request so the game keeps flowing
        Result s = stub("rate_limited_429");
        s.rateLimited = true;
        return s;
    }
    if (!res.ok) {
        Result s = stub(res.errDetail.empty() ? "http_fail" : res.errDetail.c_str());
        return s;
    }
    res.backend = ProviderName();
    TraceLog(LOG_INFO, "[llm] %s -> %s (http %d)", r.purpose.c_str(), res.backend.c_str(), res.httpStatus);
    return res;
}

static void SplitEndpoint(const std::string& ep, bool& https, std::string& host, std::string& path) {
    std::string e = ep;
    https = false;
    if (e.rfind("https://", 0) == 0) { https = true; e = e.substr(8); }
    else if (e.rfind("http://", 0) == 0) { e = e.substr(7); }
    auto slash = e.find('/');
    host = slash == std::string::npos ? e : e.substr(0, slash);
    path = slash == std::string::npos ? "/" : e.substr(slash);
}

LLMClient::Result LLMClient::CallChatAPI(const Request& r) {
    Result res;
    bool https; std::string host, path;
    SplitEndpoint(endpoint, https, host, path);

    json body = {
        {"model", model},
        {"temperature", r.temperature >= 0.f ? r.temperature : temperature},
        {"max_tokens", r.maxTokens > 0 ? r.maxTokens : maxTokens},
        // gpt-oss burns max_tokens on hidden reasoning first; "low" keeps
        // enough budget for the actual reply (ignored by plain models)
        {"reasoning_effort", "low"},
        {"messages", json::array({
            {{"role", "system"}, {"content", r.systemPrompt}},
            {{"role", "user"}, {"content", r.user}}
        })}
    };

    httplib::Headers headers{{"Content-Type", "application/json"}};
    if (!apiKey.empty()) headers.emplace("Authorization", "Bearer " + apiKey);

    httplib::Result hr;
    if (https) {
        httplib::SSLClient cli(host);
        cli.set_connection_timeout(timeoutSec, 0);
        cli.set_read_timeout(timeoutSec, 0);
        cli.enable_server_certificate_verification(false);
        hr = cli.Post(path.c_str(), headers, body.dump(), "application/json");
    } else {
        httplib::Client cli(host);
        cli.set_connection_timeout(timeoutSec, 0);
        cli.set_read_timeout(timeoutSec, 0);
        hr = cli.Post(path.c_str(), headers, body.dump(), "application/json");
    }
    if (!hr) { res.errDetail = "net:" + httplib::to_string(hr.error()); return res; }
    res.httpStatus = hr->status;
    if (hr->status == 429) { res.rateLimited = true; return res; }
    if (hr->status < 200 || hr->status >= 300) {
        res.errDetail = "http_" + std::to_string(hr->status);
        // keep a snippet of the body for diagnosis (error JSON is non-secret)
        std::string snip = hr->body.substr(0, 160);
        for (auto& ch : snip) if (ch == '\n' || ch == '\r') ch = ' ';
        TraceLog(LOG_WARNING, "[llm] http %d: %s", hr->status, snip.c_str());
        return res;
    }

    try {
        json jr = json::parse(hr->body);
        std::string content = jr["choices"][0]["message"]["content"].get<std::string>();
        res.ok = true;
        res.text = SanitizeText(content);   // curly quotes/dashes -> ASCII glyphs
        if (r.purpose == "report" || r.purpose == "facilities" || r.purpose == "phoneeval") {
            ParseVerdict(res.text, res.matchedId, res.feedback, res.match);
            res.feedback = SanitizeText(res.feedback);
        }
    } catch (...) { res.errDetail = "parse"; }
    return res;
}

// Extracts {...} JSON from possibly-noisy model output.
bool LLMClient::ParseVerdict(const std::string& text, std::string& matchedId, std::string& feedback, bool& match) {
    auto a = text.find('{'), b = text.rfind('}');
    if (a == std::string::npos || b == std::string::npos || b <= a) { match = false; return false; }
    try {
        json j = json::parse(text.substr(a, b - a + 1));
        match = j.value("match", false);
        matchedId = j.value("anomaly_id", std::string());
        feedback = j.value("feedback", std::string());
        return true;
    } catch (...) { match = false; return false; }
}

LLMClient::Result LLMClient::StubRespond(const Request& r) {
    Result res;
    res.ok = true;

    if (r.purpose == "report" || r.purpose == "facilities") {
        // keyword matching against candidate anomaly list
        for (auto& c : r.candidates) {
            if (TextHasAny(r.user, c.second)) {
                res.match = true;
                res.matchedId = c.first;
                res.feedback = r.acceptLine;
                return res;
            }
        }
        res.match = false;
        res.feedback = r.failLine;
        return res;
    }

    // phone-call persona replies: eerie canned lines so the feature still works offline
    if (r.purpose == "phonecall") {
        static const char* phoneLines[] = {
            "You sound tired. It's late there, isn't it.",
            "The line is very long tonight. Longer than it should be.",
            "I used to sit at that desk. Don't look at the window.",
            "Say that again? The static ate the middle of it.",
            "You're still there. Good. Most of them stop answering."
        };
        size_t h = std::hash<std::string>{}(r.user + r.systemPrompt);
        res.text = phoneLines[h % 5];
        return res;
    }

    // call outcome judgement offline: polite/firm language calms, curtness angers
    if (r.purpose == "phoneeval") {
        res.match = TextHasAny(r.user,
            {"please", "thank", "sorry", "goodbye", "good bye", "wrong number",
             "understand", "appreciate", "help"});
        res.feedback = res.match ? "The line goes quiet, almost grateful."
                                 : "The voice on the other end goes cold.";
        return res;
    }

    // remix anomalies offline: canned valid remixes from the parts catalog
    if (r.purpose == "remix") {
        static const char* remixes[] = {
            "{\"id\":\"rx_mugs_row\",\"channel\":\"office\",\"desc\":\"Three little mugs stand in a row beside the desk phone\",\"keywords\":[\"mugs\",\"cups\",\"row\",\"extra\",\"appeared\"],\"spawn\":{\"id\":\"rx_mugs\",\"type\":\"mug\",\"pos\":[0.1,0.86,1.45],\"size\":[0.09,0.11,0.09],\"color\":[60,60,80]}}",
            "{\"id\":\"rx_clock_turned\",\"channel\":\"office\",\"desc\":\"The wall clock turned to face the wall\",\"keywords\":[\"clock\",\"turned\",\"backwards\",\"wall\"],\"target\":\"wall_clock\",\"mutation\":{\"rot_set\":180}}",
            "{\"id\":\"rx_bin_float\",\"channel\":\"office\",\"desc\":\"The waste bin is floating a hand's width off the floor\",\"keywords\":[\"bin\",\"floating\",\"hovering\",\"trash\"],\"target\":\"bin\",\"mutation\":{\"translate\":[0,0.6,0]}}"
        };
        static int ri = 0;
        res.text = remixes[(ri++) % 3];
        return res;
    }

    // roster generation offline: procedurally remix name/dept/quirk pools so
    // even stub mode hands out fresh faces each shift
    if (r.purpose == "roster") {
        static const char* first[] = {"Marcus","Dana","Jonah","Ebba","Robert","Priya","Thomas","Ada","Felix","Nadia","Cole","Mira","Otto","Sable","Iris","Grant","Lena","Hugo","Petra","Silas"};
        static const char* last[] = {"Okafor","Voss","Reyes","Lindqvist","Halloran","Anand","Merritt","Quill","Barrow","Okonkwo","Ferro","Marsh","Tyrell","Vance","Kessler","Roan","Abernathy","Locke","Pyre","Dunmore"};
        static const char* depts[] = {"Logistics","Accounting","IT","Design","Security","Legal","Archive","Facilities","Payroll","Compliance","Maintenance","Records"};
        static const char* roles[] = {"Forklift Lead","Ledger Clerk","Systems Admin","UX Lead","Night Guard","Paralegal","Records Clerk","Inspector","Payroll Officer","Auditor","Electrician","Archivist"};
        static const char* reasons[] = {"Relocation","New position","Burnout","Contract ended","Retirement","Further education","Downsizing","Personal reasons","Health","Unknown"};
        static const char* quirks[] = {
            "Terse sentences. Keeps checking the time.",
            "Overly formal. Corrects small factual errors.",
            "Jitters. Makes dark jokes about the building.",
            "Soft-spoken. Describes everything visually.",
            "Gruff. Claims the building 'has moods'.",
            "Wants everything in writing. Suspicious of the late hour.",
            "Cheerful in a way that doesn't reach the eyes.",
            "Whispers half the answers. Apologizes too much.",
            "Talks about a dog that died years ago.",
            "Keeps glancing at the door like expecting someone."
        };
        auto pick = [](const char* arr[], int n) { return arr[RandInt(0, n - 1)]; };
        json roster = json::array();
        std::vector<std::string> used;
        int n = 8;
        for (int i = 0; i < n; i++) {
            json p;
            std::string nm;
            for (int t = 0; t < 12; t++) {   // keep re-rolling dupes
                nm = std::string(pick(first, 20)) + " " + pick(last, 20);
                if (std::find(used.begin(), used.end(), nm) == used.end()) break;
            }
            used.push_back(nm);
            p["name"] = nm;
            p["dept"] = pick(depts, 12);
            p["role"] = pick(roles, 12);
            int y0 = 1998 + RandInt(0, 22), y1 = 2026;
            p["employed"] = std::to_string(y0) + "-" + std::to_string(y1);
            p["reason"] = pick(reasons, 10);
            p["persona"] = pick(quirks, 10);
            json ap;
            ap["height"] = 0.92f + Rand01() * 0.22f;
            ap["skin"] = {90 + RandInt(0, 130), 55 + RandInt(0, 130), 40 + RandInt(0, 115)};
            ap["shirt"] = {25 + RandInt(0, 100), 25 + RandInt(0, 95), 30 + RandInt(0, 100)};
            ap["hair"] = {10 + RandInt(0, 160), 10 + RandInt(0, 140), 8 + RandInt(0, 120)};
            ap["eyes"] = "normal";
            p["appearance"] = ap;
            roster.push_back(p);
        }
        // last two are the anomalous personas the schedule relies on
        roster[n - 2]["always_anomaly"] = "impossible_visitor";
        roster[n - 2]["reason"] = "Deceased (" + std::to_string(2008 + RandInt(0, 12)) + ", building fire)";
        roster[n - 2]["employed"] = std::to_string(1990 + RandInt(0, 15)) + "-" + std::to_string(2008 + RandInt(0, 12));
        roster[n - 2]["persona"] = "ANOMALOUS: a former employee who died years ago. Calm, mournful, does not know they are dead. Goes quiet if confronted.";
        roster[n - 1]["always_anomaly"] = "hostile";
        roster[n - 1]["name"] = "Night Relief";
        roster[n - 1]["dept"] = "HR"; roster[n - 1]["role"] = "Replacement";
        roster[n - 1]["employed"] = "-"; roster[n - 1]["reason"] = "-";
        roster[n - 1]["persona"] = "ANOMALOUS: claims to be your replacement arriving early. Smiles too evenly. Answers questions with questions. Never blinks.";
        roster[n - 1]["appearance"] = {{"height", 1.12f}, {"skin", {210, 200, 190}}, {"shirt", {30, 30, 34}}, {"hair", {40, 38, 36}}, {"eyes", "black"}};
        res.text = roster.dump();
        return res;
    }

    // paperwork offline: procedural forms mixing clerical items with real
    // verified actions - same shape the live model is asked for
    if (r.purpose == "paperwork") {
        static const char* formIds[] = {"HR-7b","SEC-11","FAC-3","HR-2a","IT-9","SEC-4","PAY-6","HR-12"};
        static const char* formTitles[] = {"EXIT INTERVIEW LOG","INCIDENT REPORT","MAINTENANCE REQUEST",
            "TERMINATION CHECKLIST","ACCESS REVIEW","VISITOR REGISTER","PAYROLL CONFIRMATION","NIGHT SHIFT LOG"};
        static const char* clerical[] = {
            "Room number verified", "Date field completed", "Filed in triplicate",
            "Badge number legible", "Copies to Records", "Counter-signer's initials present",
            "Correct form revision used", "No blank fields remaining", "Stapled top-left corner"};
        static const std::pair<const char*, const char*> verified[] = {
            {"Interview conducted and logged", "interview_done"},
            {"Visitor statements attached", "interview_done"},
            {"Anomaly report cross-referenced", "report_filed"},
            {"Desk phone inquiry answered", "call_answered"},
            {"Security notified of concerns", "security_called"},
            {"Facilities ticket reconciled", "facilities_called"},
            {"Front desk acknowledgment received", "frontdesk_called"},
            {"Window blinds checked/secured", "blinds_used"},
            {"Lighting verified functional", "lights_used"},
            {"Early visitor pull logged", "visitor_early"}};
        json forms = json::array();
        int n = 6;
        for (int i = 0; i < n; i++) {
            json f;
            int fi = RandInt(0, 7);
            f["title"] = "FORM " + std::string(formIds[fi]) + ": " + formTitles[fi];
            json items = json::array();
            int vCount = RandInt(1, 2);
            int cCount = 4 - vCount;
            for (int k = 0; k < cCount; k++)
                items.push_back({{"t", clerical[RandInt(0, 8)]}, {"verify", "none"}});
            for (int k = 0; k < vCount; k++) {
                auto& v = verified[RandInt(0, 9)];
                items.push_back({{"t", v.first}, {"verify", v.second}});
            }
            // shuffle so verified lines aren't always last
            for (int k = (int)items.size() - 1; k > 0; k--) {
                int s = RandInt(0, k); std::swap(items[k], items[s]);
            }
            f["items"] = items;
            f["sign"] = fi % 2 ? "SHIFT CLERK SIGNATURE" : "RESPONSIBLE OFFICER";
            forms.push_back(f);
        }
        res.text = forms.dump();
        return res;
    }

    // dialog / frontdesk: canned lines, deterministic-ish pick
    static const char* visitorLines[] = {
        "Right. I just want this on record.",
        "Sorry, say that again? The lights keep flickering.",
        "It's been a long night already.",
        "Whatever you need to close my file, let's do it.",
        "Is it always this quiet on this floor?",
        "I'd rather not talk about the third floor.",
        "Can we hurry? I don't like the waiting room after dark."
    };
    static const char* oddLines[] = {
        "I have always worked here. Since before you.",
        "The others in the waiting room stopped talking when I came in.",
        "Your records say I left. Your records are wrong.",
        "Do you ever feel like the room is listening?",
        "I can wait. I am very good at waiting.",
        "You look tired. When did you last leave this office?"
    };
    static const char* deskLines[] = {
        "Front desk. Yeah, the elevator's acting up again.",
        "Nobody's supposed to be on three. Just... don't go up there.",
        "If the lobby lights flicker, it's the wiring. It's always the wiring.",
        "You're doing great. Only a few more hours."
    };
    size_t h = std::hash<std::string>{}(r.user + r.systemPrompt);
    if (r.purpose == "frontdesk")
        res.text = deskLines[h % 4];
    else if (r.systemPrompt.find("ANOMALOUS") != std::string::npos ||
             r.systemPrompt.find("subtly menacing") != std::string::npos ||
             r.systemPrompt.find("wrong department") != std::string::npos ||
             r.systemPrompt.find("deny it calmly") != std::string::npos)
        res.text = oddLines[h % 6];
    else
        res.text = visitorLines[h % 7];
    return res;
}
