#pragma once
#include "common.h"
#include <functional>

class Assets;

// Fake OS rendered when seated + focused on the monitor. Mouse-driven.
class FakeOS {
public:
    enum class App { None, Appointments, Records, Report, Phone, Paperwork };

    std::function<std::vector<std::string>()> getAppointments;   // display lines
    std::function<std::vector<std::string>()> getRecord;         // current visitor record
    std::function<void(const std::string& report)> onSubmitReport;
    std::function<void(const std::string& kind)> onCallSecurity;   // "intruder" | "anomaly"
    std::function<void(const std::string& desc)> onCallFacilities;
    std::function<void(const std::string& msg)> onCallFrontDesk;
    std::function<std::string()> onCallNext;              // front desk: send next visitor up early
    std::function<json()> getPaperwork;                   // current form json (null = done)
    std::function<void()> onPaperworkFiled;               // a form was signed + submitted
    // verify-key -> "" if the deed was actually done, else a short requirement
    // label ("finish an interview") - blocks ticking boxes you didn't do
    std::function<std::string(const std::string&)> canCertify;

    // virtual mouse: set each frame to the player's aim point in OS pixel space;
    // input (clicks/typing) only processed when interactive == true
    Vector2 mouse{-9999.f, -9999.f};
    bool interactive = false;
    bool acceptsText = false;       // owns the char queue this frame (set by Game;
                                    // prevents stealing chars from other text fields)
    bool typing = false;            // a text field has focus - A/D must not lean
    float clockHour = -1.f;         // in-game hour for the taskbar clock; <0 draws nothing

    App app = App::None;
    std::string reportText;
    std::string statusLine;
    float statusT = 0;
    // phone call state
    std::string phoneActive;            // "", "security", "facilities", "frontdesk"
    std::string secKind;                // security dispatch choice: "" = not picked yet
    std::string callInput;
    std::string callReply;
    bool callWaiting = false;
    // paperwork side-task state (checklist + signature per form)
    std::vector<char> paperChecks;
    std::string paperSign;
    int paperDenied = -1;               // item index that just refused a check
    float paperDeniedT = 0;
    bool focusPaper = false, reqFocusPaper = false;

    bool focusReport = false, focusCall = false;
    bool reqFocusReport = false, reqFocusCall = false;   // focus on next draw (auto-focus on open)
    float focusGrace = 0;   // briefly ignore input after focus flips on - the
                            // interact keypress that sat us down must not type

    void Reset() {
        app = App::None; reportText.clear(); statusLine.clear(); statusT = 0;
        phoneActive.clear(); secKind.clear(); callInput.clear(); callReply.clear(); callWaiting = false;
        focusReport = focusCall = reqFocusReport = reqFocusCall = false;
        paperChecks.clear(); paperSign.clear();
        focusPaper = reqFocusPaper = false;
        focusGrace = 0;
    }

    void SetStatus(const std::string& s) { statusLine = s; statusT = 6.f; }
    void SetCallReply(const std::string& s) { callReply = s; callWaiting = false; }
    void SetCallWaiting() { callWaiting = true; callReply.clear(); }

    void Update(float dt) { if (statusT > 0) statusT -= dt; }

    void Draw(const Assets& a, int W, int H);

private:
    bool Button(const Assets& a, Rectangle r, const std::string& label, bool disabled = false);
    void TextField(const Assets& a, Rectangle r, std::string& text, bool& focus, int maxLen, bool& reqFocus);
    void Panel(Rectangle r, Color c) const;
};
