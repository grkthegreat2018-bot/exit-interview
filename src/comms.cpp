#include "game.h"
#include "hud.h"
#include <rlgl.h>
#include <cstdlib>
#include <cstring>


// LLM generates tonight's cast in the background while the title idles; the
// roster is validated + applied at NewShift. Seen names carry across runs so
// the same faces don't come back shift after shift.
void Game::RequestRoster() {
    if (rosterReq != -1) return;
    LLMClient::Request r;
    r.purpose = "roster";
    r.maxTokens = 3200;   // 9 personas + personas strings; gpt-oss also spends reasoning tokens
    r.temperature = 0.95f;
    r.systemPrompt = "You write the cast of a horror game about a night-shift HR exit-interview clerk. "
        "Reply with ONLY a JSON array - no prose, no markdown fences.";
    std::string avoid;
    for (auto& n : seenNames) avoid += "\"" + n + "\", ";
    r.user =
        "Generate 8 employee personas for exit interviews at a slightly wrong office building. "
        "Each element: {\"name\":\"First Last\",\"dept\":\"<department>\",\"role\":\"<job title>\","
        "\"employed\":\"<startyear>-2026\",\"reason\":\"<why leaving>\",\"persona\":\"<1 sentence: voice, mannerism, quirk>\","
        "\"appearance\":{\"height\":<0.92-1.14>,\"gender\":\"male|female\",\"skin\":[r,g,b],\"shirt\":[r,g,b],"
        "\"pants\":[r,g,b],\"hair\":[r,g,b],\"hair_style\":\"cap|buzz|long|ponytail|bun|balding|bald\","
        "\"eye_color\":[r,g,b],\"eyes\":\"normal\",\"glasses\":true|false|[r,g,b],"
        "\"beard\":\"stubble|full\"|false,\"tie\":true|[r,g,b]|false,\"badge\":true|false}}.\n"
        "Rules: every run must feel like a different building's staff - pull names from varied cultures "
        "and generations, never reuse surnames, invent different departments each time (Archival, Drainage, "
        "Compliance, Night Records - not just HR/IT), and vary reasons from mundane to quietly wrong "
        "(\"retiring\", \"the floor moved\", \"was never hired\"). Personas feel like real tired employees "
        "with small strange edges. "
        "appearance must visibly match who they are - these fields drive the 3D model the player interviews: "
        "grey or white hair on older staff, pale washed-out skin on night-shift regulars, muted worn shirt colors "
        "for long-employed ones, a rougher skin tone for field/manual workers, height near 1.0 average. "
        "Mix genders, hair styles and iris colors across the cast - eight identical silhouettes reads as a bug. "
        "ANOMALOUS personas get unsettling variants (black or wrong eyes, extreme height, drained skin/hair).\n"
        "Element 7 must be a person who could not be here (deceased or long gone) - set \"always_anomaly\":\"impossible_visitor\", "
        "reason like \"Deceased (2014, building fire)\", and persona starting with ANOMALOUS. "
        "Element 8 must be the player's uncanny \"replacement\" - set \"always_anomaly\":\"hostile\", dept HR, role Replacement, "
        "employed \"-\", reason \"-\", eyes \"black\", height ~1.12, persona starting with ANOMALOUS.\n"
        "Do NOT reuse any of these already-seen names: " + (avoid.empty() ? "(none)" : avoid) +
        "\nRespond with ONLY the JSON array.";
    rosterReq = llm.Submit(r);
}


// overnight paperwork - generated per shift so there's desk work between
// visitors. Verified items keep the player honest.
void Game::RequestPaperwork() {
    if (paperReq != -1) return;
    LLMClient::Request r;
    r.purpose = "paperwork";
    r.maxTokens = 2600;
    r.temperature = 0.8f;
    std::string keys;
    for (auto& [k, v] : VERIFY_REQ) keys += "\"" + k + "\"(" + v + "), ";
    r.systemPrompt = "You write bureaucratic form content for a horror game about a night-shift HR clerk. "
        "Reply with ONLY a JSON array - no prose, no markdown fences.";
    r.user =
        "Generate 6 in-universe overnight paperwork forms for an HR clerk's desk terminal. "
        "Each element: {\"title\":\"FORM <code>: <NAME>\",\"items\":[...],\"sign\":\"<signature line label>\"}.\n"
        "Each item is {\"t\":\"<checklist line>\",\"verify\":\"<key>\"}. "
        "Most items are pure clerical work with \"verify\":\"none\" (dates, room numbers, staples, copies to Records). "
        "1-2 items per form MUST reference real clerk actions using these verify keys ONLY: " + keys + "\n"
        "Verify items must read like things the clerk genuinely did tonight (e.g. verify interview_done -> "
        "\"Interview conducted and logged\"). Keep item text under 8 words. "
        "Form names should be mundane with slightly wrong edges. Respond with ONLY the JSON array.";
    paperReq = llm.Submit(r);
}


// generated callers: the LLM writes tonight's anomaly phone personas up
// front; they fold into the director's call_events pool and ring through the
// normal channel so calls stop repeating the same stock voices
void Game::RequestCallPersonas() {
    if (callEvReq != -1) return;
    LLMClient::Request r;
    r.purpose = "callpersonas";
    r.maxTokens = 2400;
    r.temperature = 0.9f;
    r.systemPrompt = "You write disturbing phone callers for a horror game about a night-shift HR "
        "clerk alone in a wrong office building. Reply with ONLY a JSON array - no prose, no markdown fences.";
    r.user =
        "Generate 3 phone-call personas - things that phone the clerk's desk at night. "
        "These are anomalies, NOT employees and NOT ordinary work calls: wrong numbers, dead extensions, "
        "things already inside the building, surveys about impossible topics, callers who know what the "
        "clerk is doing right now.\n"
        "Each element: {\"persona\":\"<short caller name>\",\"greeting\":\"<first line when answered>\","
        "\"system\":\"<2-3 sentences: who/what this is, its voice, and what satisfies vs upsets it>\","
        "\"keywords\":[\"phone\",\"call\",\"...\"],\"instab\":<0.8-2.5>,"
        "\"on_calm\":{...},\"on_hostile\":{...},\"on_ignore\":{...}}.\n"
        "system must end with a satisfaction rule, e.g. 'If the caller is civil you are SATISFIED; "
        "if rude, UPSET.'\n"
        "Effects use ONLY these keys - leave an object empty {} if the call is benign:\n"
        " \"instability\":<2-20, the room gets worse>, \"relief\":<2-12, the room settles>,"
        " \"figure\":\"at_door\"|\"at_window\" (something appears outside),"
        " \"lights\":\"office\"|\"out\" (lights fail), \"banging\":\"door\"|\"window\".\n"
        "on_calm should usually hold a small relief; hostile/ignore hold consequences. "
        "Keep greeting under 2 sentences, system under 3. ASCII text only. Respond with ONLY the JSON array.";
    callEvReq = llm.Submit(r);
}


// parse/validate the generated forms - any malformed element is dropped,
// unknown verify keys degrade to "none" rather than blocking the form
bool Game::ApplyPaperworkJson(const std::string& text) {
    auto a = text.find('['), b = text.rfind(']');
    if (a == std::string::npos || b <= a) return false;
    json arr;
    try { arr = json::parse(text.substr(a, b - a + 1)); }
    catch (...) { return false; }
    if (!arr.is_array() || arr.size() < 3) return false;
    json forms = json::array();
    for (auto& f : arr) {
        if (!f.is_object() || !f.contains("items") || !f["items"].is_array()) continue;
        json q;
        q["title"] = SanitizeText(JS(f, "title")).substr(0, 48);
        q["sign"] = SanitizeText(JS(f, "sign", "SIGNATURE")).substr(0, 40);
        json items = json::array();
        for (auto& it : f["items"]) {
            std::string t = it.is_string() ? it.get<std::string>() : JS(it, "t");
            t = SanitizeText(t);
            if (t.empty()) continue;
            std::string v = it.is_string() ? "none" : JS(it, "verify", "none");
            if (!VERIFY_REQ.count(v)) v = "none";
            items.push_back({{"t", t.substr(0, 60)}, {"verify", v}});
        }
        if (items.size() < 2) continue;
        q["items"] = items;
        forms.push_back(q);
    }
    if (forms.size() < 3) return false;
    paperForms = forms;
    return true;
}


std::string Game::BuildVisitorSystem() const {
    const Visitor& v = vs.Cur();
    if (!v.person) return JS(A.dialogs, "visitor_system_prompt");
    std::string sys = JS(A.dialogs, "visitor_system_prompt");
    auto rep = [&](const char* k, const std::string& val) { sys = ReplaceAll(sys, "{" + std::string(k) + "}", val); };
    rep("name", JS(*v.person, "name"));
    rep("role", JS(*v.person, "role"));
    rep("dept", JS(*v.person, "dept"));
    rep("employed", JS(*v.person, "employed"));
    rep("reason", JS(*v.person, "reason"));
    rep("persona", JS(*v.person, "persona"));
    if (!v.anomaly.empty())
        sys += JS(A.dialogs["visitor_anomaly_addendum"], v.anomaly.c_str());
    else {
        // normal visitors get an anti-weirdness addendum - without it the model
        // sometimes drifts creepy/contradictory and reads as a false anomaly
        std::string add = JS(A.dialogs, "visitor_normal_addendum");
        for (const char* k : {"role", "dept", "employed", "reason"})
            add = ReplaceAll(add, "{" + std::string(k) + "}", JS(*v.person, k));
        sys += add;
    }
    return sys;
}


void Game::SayToVisitor(const std::string& text) {
    if (vs.VisitorSeated()) askedCur = true;   // a real question was put to them
    if (!vs.VisitorSeated()) { Notify("No one is sitting across from you."); return; }
    vs.transcript.push_back({"You", text});
    LLMClient::Request r;
    r.purpose = "dialog";
    r.systemPrompt = BuildVisitorSystem();
    std::string ctx;
    int n = (int)vs.transcript.size();
    for (int i = std::max(0, n - 7); i < n; i++) {
        // label player lines as the interviewer - "You" makes the model think
        // those are its own turns and it answers like an assistant
        const std::string& spk = vs.transcript[i].first;
        ctx += (spk == "You" ? "Interviewer" : spk) + ": " + vs.transcript[i].second + "\n";
    }
    r.user = "CONVERSATION SO FAR:\n" + ctx + "\nReply in character as " + vs.Cur().Name() +
             ", the visitor being interviewed. Speak only " + vs.Cur().Name() + "'s next line.";
    dlg.pendingReply = true;
    dlgReq = llm.Submit(r);

    // dismissals end the interview without the END button: "that's all",
    // "you can go", "we're done here" - the things an interviewer actually
    // says to wrap up. The reply lands first, then they take the hint.
    static const std::vector<std::string> kEndPhrases = {
        "that's all", "that is all", "that'll be all", "that will be all",
        "nothing further", "you can go", "you may go", "you may leave",
        "you are free to go", "free to go", "you're free to go", "can leave now",
        "interview is over", "interview's over", "end the interview",
        "end this interview", "conclude the interview", "we're done here",
        "we are done", "that concludes", "wrap it up", "wrap this up",
        "dismissed", "you're dismissed", "you are dismissed",
        "thank you for your time", "thanks for your time", "that'll do",
        "see yourself out", "show yourself out", "goodbye", "good bye",
        "take care", "no more questions", "nothing else to discuss",
        "we can end here", "let's end here", "done talking"};
    if (TextHasAny(text, kEndPhrases) && autoEndT < 0.f)
        autoEndT = 2.6f;   // let them answer, then they rise and go

    // hostility trigger: enough engagement makes it start slipping
    if (vs.Cur().anomaly == "hostile") {
        int youCount = 0;
        for (auto& t : vs.transcript) if (t.first == "You") youCount++;
        if (youCount >= 3) vs.MarkHostileRevealed();
    }
}


void Game::SubmitReport(const std::string& text) {
    if (repReq != -1) { os.SetStatus("Terminal is processing..."); return; }
    LLMClient::Request r;
    r.purpose = "report";
    std::string active = ad.ActiveDescriptions("office");
    if (ad.IntruderActive()) active += "- [intruder] " + JS(A.anomalies["intruder"], "desc") + "\n";
    r.systemPrompt = ReplaceAll(
        ReplaceAll(JS(A.dialogs, "validator_prompt"), "{active}", active),
        "{report}", text);
    r.user = "REPORT: " + text;
    r.candidates = ad.Candidates("office");
    if (ad.IntruderActive()) {
        std::vector<std::string> kw;
        for (auto& k : A.anomalies["intruder"]["keywords"]) kw.push_back(k.get<std::string>());
        r.candidates.push_back({"intruder", kw});
    }
    r.acceptLine = JS(A.dialogs["stub"], "report_accept");
    r.failLine = JS(A.dialogs["stub"], "report_fail");
    repText = text;
    repReq = llm.Submit(r);
    os.SetStatus("Submitting...");
}


// when a report/call misses, see if the text matches a live anomaly on a
// different channel and tell the player where to send it instead
std::string Game::OtherChannelHint(const std::string& text, const std::string& tried) {
    auto figKws = ad.ActiveKeywords("figure");
    auto bangKws = ad.ActiveKeywords("banging");
    if (tried != "security" && !figKws.empty() && TextHasAny(text, figKws))
        return "Someone out there is a person problem - PHONE > Security.";
    if (tried != "security" && !bangKws.empty() && TextHasAny(text, bangKws))
        return "That's a PERSON hammering on the building - PHONE > Security > INTRUDER.";
    if (tried != "exterior")
        for (auto& c : ad.Candidates("exterior"))
            if (ad.KindOf(c.first).rfind("figure", 0) != 0 &&
                ad.KindOf(c.first).rfind("banging", 0) != 0 && TextHasAny(text, c.second))
                return "That's outside the office - PHONE > Facilities.";
    if (tried != "office")
        for (auto& c : ad.Candidates("office"))
            if (TextHasAny(text, c.second))
                return "That's an in-office detail - file it via REPORT.";
    if (tried != "security")
        for (auto& c : ad.Candidates("visitor"))
            if (TextHasAny(text, c.second))
                return "That's about your visitor - PHONE > Security.";
    return "";
}


// security dispatch is explicit: INTRUDER = a person problem (real intruder or
// an anomalous visitor to escort out), ANOMALY = something non-human outside
// (shadow figures at the window/door). Picking wrong redirects, not punishes.
void Game::CallSecurity(const std::string& kind) {
    std::string reply;
    bool visitorAnom = vs.HasActiveVisitor() && !vs.Cur().anomaly.empty() && vs.Cur().state != VState::Gone;
    bool figures = ad.HasKind("figure_at_window") || ad.HasKind("figure_at_door");
    bool banging = ad.HasKind("banging_door") || ad.HasKind("banging_window");
    deeds["security_called"]++;   // a completed call counts for the forms either way

    if (kind == "intruder") {
        if (ad.IntruderActive()) {
            auto o = ad.BeginResolve("intruder", "intruder", office, vs, true);
            if (o.ok) { o.workPos = ad.IntruderPos(); DispatchCrew({o}, 2, true); deeds["anomaly_resolved"]++; }
            reply = JS(A.dialogs["stub"], "security_arriving");
            Notify("Security is moving on the intruder.");
            audio.Play(AudioBank::Sfx::Thump, 0.6f);
        } else if (visitorAnom) {
            ad.TryResolve("visitor", "v_" + vs.Cur().anomaly, office, vs);
            // guards physically come collect them - escort choreography
            ResolveOutcome eo;
            eo.ok = true; eo.id = "escort_" + vs.Cur().anomaly; eo.channel = "office";
            eo.workPos = vs.Cur().pos;
            DispatchCrew({eo}, 2, true, true);
            deeds["anomaly_resolved"]++;
            reply = JS(A.dialogs["stub"], "security_arriving");
            Notify("Security is coming to collect them.");
            audio.Play(AudioBank::Sfx::Thump, 0.6f);
        } else if (banging) {
            // somebody hammering the door/glass IS a person problem - guards
            // go chase them off, or the knocking never stops
            std::vector<ResolveOutcome> outs;
            for (auto& a : ad.Active())
                if (a.channel == "exterior" && a.kind.rfind("banging", 0) == 0)
                    outs.push_back(ad.BeginResolve("exterior", a.id, office, vs, true));
            if (outs.empty()) {
                reply = "Team's already on it. Give it a minute.";
            } else {
                DispatchCrew(outs, 2, true);
                deeds["anomaly_resolved"] += (int)outs.size();
                reply = JS(A.dialogs["stub"], "security_arriving");
                Notify("Security is moving on whoever is hammering the building.");
            }
            audio.Play(AudioBank::Sfx::Thump, 0.6f);
        } else {
            reply = "No hostile person on our cameras.";
            if (figures) reply += " If you mean the shape outside, that's an ANOMALY dispatch.";
            else if (ad.HasKind("broken_window")) reply += " Broken glass is a repair job - that's Facilities.";
            else if (!ad.Candidates("exterior").empty()) reply += " If it's the building acting up, that's Facilities.";
            else if (!ad.Candidates("office").empty()) reply += " If something's off in your office, file a REPORT.";
            if (ad.ActiveCount() == 0) {
                ad.AddInstability(JF(Cfg()["instability"], "wrong_security_call_penalty", 10));
                Notify("Security found nothing. That was a mistake.");
            } else Notify("Nothing for an intruder team to grab.");
        }
    } else {   // "anomaly" - sweep non-office threats: shadow figures outside,
               // and waiting-room people who aren't people anymore
        bool mutants = ad.HasKind("mutated_waiting");
        if (figures || mutants || banging) {
            std::vector<ResolveOutcome> outs;
            for (auto& a : ad.Active())
                if (a.channel == "exterior" &&
                    (a.kind.rfind("figure", 0) == 0 || a.kind == "mutated_waiting" ||
                     a.kind.rfind("banging", 0) == 0))
                    outs.push_back(ad.BeginResolve("exterior", a.id, office, vs, true));
            if (outs.empty()) {
                reply = "They're already out there sweeping. Give it a minute.";
            } else {
                DispatchCrew(outs, 2, true);
                deeds["anomaly_resolved"] += (int)outs.size();
                reply = JS(A.dialogs["stub"], "security_arriving");
                Notify(mutants ? "Security is clearing the waiting room."
                               : "Security is sweeping the perimeter.");
            }
            audio.Play(AudioBank::Sfx::Thump, 0.6f);
        } else {
            reply = "Perimeter reads clear.";
            if (ad.IntruderActive() || visitorAnom) reply += " If the problem is a PERSON in the building, dispatch INTRUDER.";
            else if (ad.HasKind("broken_window")) reply += " That's property damage, not a threat - call Facilities for the window.";
            else if (!ad.Candidates("exterior").empty()) reply += " If it's the building acting up, that's Facilities.";
            else if (!ad.Candidates("office").empty()) reply += " If something's off in your office, file a REPORT.";
            if (ad.ActiveCount() == 0) {
                ad.AddInstability(JF(Cfg()["instability"], "wrong_security_call_penalty", 10));
                Notify("Security found nothing. That was a mistake.");
            } else Notify("No anomaly outside for security to sweep.");
        }
    }
    os.SetCallReply(reply);
}


void Game::CallFacilities(const std::string& desc) {
    if (facReq != -1) return;
    facText = desc;
    deeds["facilities_called"]++;   // the call itself happened - a form asking
                                    // for a dispatch is satisfiable even when
                                    // nothing turns out to be broken
    // a person sighting is Security's job - redirect instead of failing
    if (TextHasAny(desc, ad.ActiveKeywords("figure"))) {
        os.SetCallReply(JS(A.dialogs["stub"], "facilities_redirect_security"));
        return;
    }
    LLMClient::Request r;
    r.purpose = "facilities";
    r.systemPrompt = ReplaceAll(
        ReplaceAll(JS(A.dialogs, "facilities_prompt"), "{active}", ad.ActiveDescriptions("exterior", "figure")),
        "{report}", desc);
    r.user = "CALLER SAYS: " + desc;
    auto cands = ad.Candidates("exterior");
    std::erase_if(cands, [&](auto& c) { return ad.KindOf(c.first).rfind("figure", 0) == 0; });
    r.candidates = cands;
    r.acceptLine = JS(A.dialogs["stub"], "facilities_accept");
    r.failLine = JS(A.dialogs["stub"], "facilities_fail");
    facReq = llm.Submit(r);
}


void Game::CallFrontDesk(const std::string& msg) {
    if (deskReq != -1) return;
    deeds["frontdesk_called"]++;
    LLMClient::Request r;
    r.purpose = "frontdesk";
    r.systemPrompt = ReplaceAll(JS(A.dialogs, "frontdesk_prompt"), "{report}", msg);
    r.user = msg;
    deskReq = llm.Submit(r);
}


// ---------- desk phone: anomaly calls with LLM personas ----------

// the intruder timer ran out - hidden under the desk it sweeps past you
void Game::OnIntruderStrike() {
    if (player.hiding) {
        ad.ClearIntruder(office);
        deeds["intruder_survived"]++;
        Notify("It searched the room and found nothing. Stay low a moment longer.");
        audio.Play(AudioBank::Sfx::Heart, 0.7f);
        return;
    }
    Jumpscare("intruder");
}


void Game::UpdatePhone(float dt) {
    if (phone.ringing) {
        phone.ringT += dt;
        phone.ignoreT += dt;
        if (phone.ringT > 2.6f) { phone.ringT = 0; audio.Play(AudioBank::Sfx::Ring, 0.7f); }
        float lim = JF(Cfg()["instability"], "call_ignore_sec", 55);
        if (phone.ignoreT > lim) {
            if (phone.isWork) {
                // blowing off a work call is dereliction - it goes in the log
                deeds["call_ignored"]++;
                ad.AddInstability(JF(Cfg()["instability"], "work_call_ignore_instab", 5.f));
                Notify("You let a work call ring out. That's on the record.");
                audio.Play(AudioBank::Sfx::Buzz, 0.45f);
            } else {
                // an ignored call ends badly - the persona's own on_ignore decides
                ad.TriggerEffect(phone.ev["on_ignore"], office, vs);
                ad.DropId(phone.anomalyId);
                deeds["call_ignored"]++;
                Notify("The ringing stops. Something feels worse.");
                audio.Play(AudioBank::Sfx::Stinger, 0.5f);
            }
            phone = PhoneEvt{};
        }
    }
}


void Game::AnswerPhone() {
    if (!phone.ringing || phone.ev.is_null()) return;
    phone.ringing = false;
    phone.answered = true;
    deeds["call_answered"]++;
    // the voice was heard - this caller's card is collected for good
    if (!phone.isWork) {
        std::string cid = JS(phone.ev, "id");
        if (!cid.empty() && loreSeen.insert(cid).second) {
            deeds["lore_found"]++;
            SaveSettings();
        }
    }
    phone.transcript.push_back({JS(phone.ev, "persona"), JS(phone.ev, "greeting")});
    phoneDlg.title = phone.isWork ? "INCOMING CALL" : "PHONE CALL";
    phoneDlg.endLabel = "HANG UP";
    phoneDlg.focused = true;
    EnableCursor();
    audio.Play(AudioBank::Sfx::Click, 0.6f);
}


void Game::PhoneSay(const std::string& text) {
    if (!phone.answered || phoneReq != -1 || phoneEvalReq != -1) return;
    phone.transcript.push_back({"You", text});
    std::string convo;
    // player lines are labelled CLERK - "You:" makes the model think those
    // are its own turns, which is how echoes of the player's text got in
    for (auto& [s, t] : phone.transcript)
        convo += (s == "You" ? "Clerk" : s) + ": " + t + "\n";
    LLMClient::Request r;
    r.purpose = "phonecall";
    r.systemPrompt = JS(phone.ev, "system") +
        "\nStay in character. Reply with ONE short line of dialogue, nothing else. "
        "Never repeat, quote, or echo the clerk's words back to them. Never re-ask a "
        "question already asked - every line must move the call somewhere new.";
    r.user = convo;
    phoneReq = llm.Submit(r);
    phoneDlg.pendingReply = true;
    phone.exchanges++;
}


void Game::HangUpPhone() {
    if (!phone.answered) return;
    if (phone.isWork) {
        // mundane call - handling it properly steadies the room; answering
        // and instantly hanging up counts as blowing it off
        int said = 0;
        for (auto& [s, t] : phone.transcript) if (s == "You") said++;
        if (said > 0) {
            ad.AddInstability(-3.5f);
            deeds["call_handled"]++;
            Notify("Call handled. Back to the shift.");
        } else {
            deeds["call_ignored"]++;
            ad.AddInstability(JF(Cfg()["instability"], "work_call_ignore_instab", 5.f));
            Notify("You picked up and hung up without a word. Logged.");
            audio.Play(AudioBank::Sfx::Buzz, 0.45f);
        }
        phone = PhoneEvt{};
        phoneDlg.Reset();
        if (mode == Mode::Roam) DisableCursor();
        return;
    }
    std::string convo;
    for (auto& [s, t] : phone.transcript)
        convo += (s == "You" ? "Clerk" : s) + ": " + t + "\n";
    LLMClient::Request r;
    r.purpose = "phoneeval";
    r.systemPrompt = "You are judging a phone call with the entity described here: " +
        JS(phone.ev, "system") +
        "\nReply ONLY with JSON {\"match\":true/false,\"feedback\":\"one short sentence\"}. "
        "match=true means the entity is SATISFIED; false means it is UPSET.";
    r.user = convo.empty() ? "(the caller picked up and hung up without a word)" : convo;
    phoneEvalReq = llm.Submit(r);
}


void Game::PhoneOutcome(bool calm) {
    std::string persona = JS(phone.ev, "persona");
    if (calm) {
        ad.TriggerEffect(phone.ev["on_calm"], office, vs);
        ad.TryResolve("office", phone.anomalyId, office, vs);   // resolving the call anomaly
        deeds["call_calmed"]++;
        deeds["anomaly_resolved"]++;
        Notify(persona + " hangs up. The room settles.");
        audio.Play(AudioBank::Sfx::Click, 0.5f);
    } else {
        ad.TriggerEffect(phone.ev["on_hostile"], office, vs);
        ad.DropId(phone.anomalyId);
        Notify("The line cuts to static. Whatever that was, it is unhappy.");
        audio.Play(AudioBank::Sfx::Stinger, 0.7f);
    }
    phone = PhoneEvt{};
    phoneDlg.Reset();
    if (mode == Mode::Roam) DisableCursor();
}


void Game::PollLLM() {
    LLMClient::Result res;
    if (testReq != -1 && llm.Poll(testReq, res)) {
        testReq = -1;
        std::string t = res.text;
        for (auto& c : t) if (c == '\n' || c == '\r') c = ' ';
        testResult = t.empty() ? ("no reply (" + res.backend + ")")
                               : (res.backend + " -> " + t.substr(0, 60));
    }
    if (dlgReq != -1 && llm.Poll(dlgReq, res)) {
        dlgReq = -1; dlg.pendingReply = false;
        vs.transcript.push_back({vs.Cur().Name(), res.text.empty() ? "..." : res.text});
        audio.Play(AudioBank::Sfx::TypeTick, 0.4f);
    }
    if (repReq != -1 && llm.Poll(repReq, res)) {
        repReq = -1;
        TraceLog(LOG_INFO, "[llm] report verdict backend=%s match=%d id=%s",
                 res.backend.c_str(), (int)res.match, res.matchedId.c_str());
        if (!res.match)   // raw content on a miss - truncation/refusal shows here
            TraceLog(LOG_WARNING, "[llm] report raw: %.220s", res.text.c_str());
        // one-time heads-up when a real call was attempted but fell back
        if (!offlineNotified && res.backend.rfind("stub:http", 0) == 0) {
            offlineNotified = true;
            Notify("Terminal offline (" + res.backend.substr(5) + ") - local keyword matching only.");
        }
        if (res.match && res.matchedId == "intruder") {
            os.SetStatus("REPORT NOTED. THIS REQUIRES SECURITY - CALL NOW.");
        } else if (res.match) {
            ResolveOutcome o = ad.BeginResolve("office", res.matchedId, office, vs, true);
            if (o.ok) {
                os.SetStatus(res.feedback.empty() ? "REPORT ACCEPTED." : res.feedback);
                int men = (!o.spawnedId.empty() || o.kind == "lights_office" || o.kind == "lights_out") ? 2 : 1;
                DispatchCrew({o}, men, false);
                deeds["report_filed"]++;
                deeds["anomaly_resolved"]++;
                Notify("Anomaly confirmed - maintenance dispatched.");
                audio.Play(AudioBank::Sfx::Click, 0.7f);
            } else os.SetStatus("REPORT ACCEPTED - already being handled.");
        } else {
            os.SetStatus(res.feedback.empty() ? "REPORT REJECTED." : res.feedback);
            deeds["report_rejected"]++;
            ad.AddInstability(JF(Cfg()["instability"], "report_fail_penalty", 14));
            std::string hint = OtherChannelHint(repText, "office");
            Notify(hint.empty() ? "The report was wrong. The room feels worse." : hint);
            audio.Play(AudioBank::Sfx::Thump, 0.5f);
        }
    }
    if (facReq != -1 && llm.Poll(facReq, res)) {
        facReq = -1;
        if (res.match && ad.KindOf(res.matchedId).rfind("figure", 0) == 0) {
            // model matched a person - that's still Security's job
            os.SetCallReply(JS(A.dialogs["stub"], "facilities_redirect_security"));
        } else if (res.match) {
            ResolveOutcome o = ad.BeginResolve("exterior", res.matchedId, office, vs, true);
            if (o.ok) {
                os.SetCallReply(res.feedback.empty() ? "I'll handle it." : res.feedback);
                DispatchCrew({o}, (o.kind == "lights_out" || o.kind == "empty_room") ? 2 : 1, false);
                deeds["anomaly_resolved"]++;
                Notify("Facilities sent someone into the waiting room.");
            } else os.SetCallReply("Crew's already on it.");
        } else {
            os.SetCallReply(res.feedback.empty() ? "Nothing on the cameras." : res.feedback);
            ad.AddInstability(JF(Cfg()["instability"], "wrong_facilities_call_penalty", 10));
            std::string hint = OtherChannelHint(facText, "exterior");
            if (!hint.empty()) Notify(hint);
        }
    }
    if (deskReq != -1 && llm.Poll(deskReq, res)) {
        deskReq = -1;
        os.SetCallReply(res.text.empty() ? "...line's dead." : res.text);
    }
    // generated cast for the next shift - sits in rosterRaw until NewShift
    if (rosterReq != -1 && llm.Poll(rosterReq, res)) {
        rosterReq = -1;
        if (res.ok && !res.text.empty() && res.text.find('[') != std::string::npos)
            rosterRaw = res.text;
        else TraceLog(LOG_WARNING, "[roster] generation failed (%s) - stock cast next shift", res.backend.c_str());
    }
    // generated paperwork - sits in paperRaw until NewShift applies it
    if (paperReq != -1 && llm.Poll(paperReq, res)) {
        paperReq = -1;
        if (res.ok && !res.text.empty() && res.text.find('[') != std::string::npos)
            paperRaw = res.text;
        else TraceLog(LOG_WARNING, "[paperwork] generation failed (%s) - stock forms tonight", res.backend.c_str());
    }
    // generated call personas fold straight into the director's event pool
    if (callEvReq != -1 && llm.Poll(callEvReq, res)) {
        callEvReq = -1;
        callGenDone = true;   // one shot per title visit - stock callers remain on failure
        int n = 0;
        if (res.ok && !res.text.empty()) {
            try {
                std::string t = res.text;
                size_t a = t.find('['), b = t.rfind(']');
                if (a != std::string::npos && b > a)
                    n = ad.InjectCallEvents(json::parse(t.substr(a, b - a + 1)));
            } catch (...) {}
        }
        std::string what = n ? "folded " + std::to_string(n) + " callers into the pool"
                             : "failed - stock callers tonight";
        TraceLog(n ? LOG_INFO : LOG_WARNING, "[calls] persona generation %s (%s)",
                 what.c_str(), res.backend.c_str());
    }
    // LLM-remix anomaly came back - validate against the parts catalog
    if (remixReq != -1 && llm.Poll(remixReq, res)) {
        remixReq = -1;
        ad.remixPending = false;
        std::string why;
        if (ad.ApplyRemixJson(res.text, office, why)) {
            TraceLog(LOG_INFO, "[remix] spawned: %.100s", res.text.c_str());
            audio.Play(AudioBank::Sfx::Creak, 0.55f);
            blackoutT = std::max(blackoutT, 0.07f);
        } else {
            TraceLog(LOG_WARNING, "[remix] rejected (%s): %.140s", why.c_str(), res.text.c_str());
        }
    }
    if (phoneReq != -1 && llm.Poll(phoneReq, res)) {
        phoneReq = -1; phoneDlg.pendingReply = false;
        std::string reply = res.text.empty() ? "(static)" : res.text;
        // LLM echo guard: the model sometimes parrots the player's own line
        // back (in-character mirroring, "You:" speaker tags, quoted replies).
        // Carve any verbatim echo of the last clerk line out of the reply.
        if (!phone.transcript.empty() && phone.transcript.back().first == "You") {
            const std::string& last = phone.transcript.back().second;
            std::string low = ToLower(reply), lt = ToLower(last);
            size_t p = lt.size() >= 4 ? low.find(lt) : std::string::npos;
            if (p != std::string::npos) reply.erase(p, last.size());
            // a stray "You:"/"Clerk:" speaker tag it invented - cut that line
            for (const char* tag : {"You:", "Clerk:", "CLERK:", "Interviewer:"})
                if (reply.rfind(tag, 0) == 0) {
                    size_t nl = reply.find('\n');
                    reply = nl == std::string::npos ? "" : reply.substr(nl + 1);
                    break;
                }
            // collapse to the last non-empty line if it emitted several
            size_t cut = reply.find('\n');
            while (cut != std::string::npos) {
                std::string rest = reply.substr(cut + 1);
                if (rest.find_first_not_of(" \t") == std::string::npos) break;
                reply = rest;                       // keep the tail (its actual line)
                cut = reply.find('\n');
            }
            while (!reply.empty() && (reply.front() == ' ' || reply.front() == '"' ||
                                      reply.front() == '\'')) reply.erase(0, 1);
            while (!reply.empty() && (reply.back() == ' ' || reply.back() == '"'))
                reply.pop_back();
            if (reply.empty()) reply = "(static)";
        }
        phone.transcript.push_back({JS(phone.ev, "persona"), reply});
        audio.Play(AudioBank::Sfx::TypeTick, 0.35f);
        if (phone.exchanges >= JI(Cfg()["instability"], "call_max_exchanges", 4))
            HangUpPhone();   // it has heard enough - judge the call
    }
    if (phoneEvalReq != -1 && llm.Poll(phoneEvalReq, res)) {
        phoneEvalReq = -1;
        PhoneOutcome(res.match);
    }
    // delayed interview-wrap verdicts: a concluded interview counts, a guest
    // who walked mid-conversation is the write-up. The eval fires on the
    // leave edge, the reply lands a beat later - name rides along for the toast
    for (auto it = ivEvals.begin(); it != ivEvals.end();) {
        if (!llm.Poll(it->first, res)) { ++it; continue; }
        TraceLog(LOG_INFO, "[llm] intervieweval verdict backend=%s match=%d visitor=%s",
                 res.backend.c_str(), (int)res.match, it->second.c_str());
        if (res.match) {
            deeds["interview_done"]++;
            if (!res.feedback.empty()) os.SetStatus(res.feedback);
        } else {
            deeds["visitor_missed"]++;
            Notify("WARNING ISSUED - " + it->second +
                   " left without a proper interview. It goes on your record.");
            notes.back().second = 10.f;
            audio.Play(AudioBank::Sfx::Buzz, 0.55f);
        }
        it = ivEvals.erase(it);
    }
    // rate limit handling: save + pause once per cooldown (never on the menu)
    if (llm.RateLimited() && !rlHandled && mode != Mode::Title) {
        rlHandled = true;
        Save();
        pauseReason = "LLM RATE LIMIT - progress saved. Offline responses active (~1 min).";
        pauseReturn = mode;
        menuPage = 0;
        mode = Mode::Pause;
        EnableCursor();
    } else if (!llm.RateLimited()) rlHandled = false;
}
