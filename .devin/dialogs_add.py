import json
p = 'assets/dialogs.json'
d = json.load(open(p))

d["work_calls"] = [
    {
        "id": "wc_payroll",
        "persona": "Payroll",
        "system": "You are a payroll clerk calling the night-shift HR office about a routine timesheet question. Mundane, a little tired, under two sentences per line.",
        "greeting": "Hey - payroll. Quick one: are the exit-interview timesheets going up tonight or first thing tomorrow?"
    },
    {
        "id": "wc_it",
        "persona": "IT Support",
        "system": "You are an IT support tech doing a courtesy call about scheduled maintenance. Friendly, brief, slightly bored.",
        "greeting": "IT here. Heads up - we're bouncing the file server at 2 AM, so save your paperwork before then, okay?"
    },
    {
        "id": "wc_security",
        "persona": "Lobby Security",
        "system": "You are the lobby security guard doing the hourly check-in call. Calm, procedural, professional.",
        "greeting": "Lobby desk, hourly check. Everything quiet up on three? Just say aye and log it."
    },
    {
        "id": "wc_cleaning",
        "persona": "Cleaning Supervisor",
        "system": "You are the floor's cleaning supervisor checking whether the office is occupied before your crew comes through. Practical, courteous.",
        "greeting": "Evening - cleaning crew. Are we good to come through the HR office in about an hour, or still interviewing?"
    },
    {
        "id": "wc_supervisor",
        "persona": "Your Supervisor",
        "system": "You are the player's day-shift supervisor leaving a check-in call they probably resent. Warm but managerial, under two sentences.",
        "greeting": "Just checking in on the overnight interviews - how's the list looking? No surprises, I hope."
    }
]

d["paperwork"] = [
    {
        "title": "FORM HR-7b: EXIT INTERVIEW LOG",
        "items": ["Interviewee verified against appointment book", "Reason for leaving recorded in file", "Badge + keycard recovered", "Final pay explained to employee"],
        "sign": "INTERVIEWER SIGNATURE"
    },
    {
        "title": "FORM SEC-11: INCIDENT LOG",
        "items": ["Time of incident noted", "Floor and room number correct", "Witness statement attached", "Dispatch confirmation filed"],
        "sign": "SECURITY OFFICER COUNTERSIGN"
    },
    {
        "title": "FORM FAC-3: MAINTENANCE REQUEST",
        "items": ["Fixture or object identified", "Room number verified", "Urgency level marked", "Follow-up visit scheduled"],
        "sign": "REQUESTING CLERK"
    },
    {
        "title": "FORM PAY-22: SEVERANCE CONFIRMATION",
        "items": ["Final figure double-checked", "Deductions itemized", "Direct deposit on file", "Employee acknowledged terms"],
        "sign": "PAYROLL AUTHORIZATION"
    },
    {
        "title": "FORM HR-0: NIGHT SHIFT HANDOVER",
        "items": ["Overnight log attached", "Unresolved incidents flagged", "Keys returned to hook", "Lights left as found"],
        "sign": "OUTGOING CLERK"
    }
]

d["endings"]["breach"] = "The blinds were open. Whatever was outside decided it was invited."
d["endings"]["hid"] = "It searched the room while you held your breath under the desk."

json.dump(d, open(p, 'w'), indent=2)
print("dialogs.json updated")
