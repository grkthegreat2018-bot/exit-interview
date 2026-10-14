import json, pathlib, collections

A = pathlib.Path('assets')

# ---------------- anomalies.json ----------------
ap = A / 'anomalies.json'
d = json.loads(ap.read_text(encoding='utf-8'), object_pairs_hook=collections.OrderedDict)

office_new = [
 {"id":"chair_gone","target":"office_chair","mutation":{"hidden":True},
  "desc":"Your desk chair is gone","keywords":["chair","gone","missing","seat","my","desk"],"instab":1.3},
 {"id":"monitor_turned","target":"monitor","mutation":{"rot_set":205},
  "desc":"The monitor has turned to face away from the desk","keywords":["monitor","screen","turned","rotated","facing","away","backwards"],"instab":0.9},
 {"id":"pc_gone","target":"pc_tower","mutation":{"hidden":True},
  "desc":"The PC tower is gone - its cords dangle loose under the desk","keywords":["pc","tower","computer","gone","missing","cords","cables"],"instab":1.4},
 {"id":"desk_float","target":"desk","mutation":{"pos_set":[0,0.42,1.15]},
  "desc":"The desk is hanging a foot above the floor","keywords":["desk","floating","levitating","air","lifted","hanging","off","ground"],"instab":1.6},
 {"id":"couch_gone","target":"couch","mutation":{"hidden":True},
  "desc":"The couch is gone","keywords":["couch","sofa","gone","missing","vanished"],"instab":1.3},
 {"id":"plant2_wall","target":"plant_2","mutation":{"pos_set":[-6.55,0,4.55]},
  "desc":"The tall plant by the west wall is sunk halfway into the wall","keywords":["plant","wall","inside","sunk","stuck","west","half"],"instab":1.2},
 {"id":"switch_high","target":"light_switch","mutation":{"pos_set":[-1.4,2.75,-4.9]},
  "desc":"The light switch has slid up the wall out of reach","keywords":["switch","light","moved","high","wall","reach","slid"],"instab":1.0},
 {"id":"clock_east","target":None,"mutation":{"spawn":{"id":"anom_clock2","type":"wall_clock",
  "pos":[6.93,2.35,-0.8],"size":[0.45,0.45,0.07],"rot":270,"color":[60,58,64],"stopped":True}},
  "desc":"A second wall clock hangs on the east wall - its hands are not moving","keywords":["clock","second","extra","east","stopped","new","appeared","time"],"instab":1.4},
 {"id":"poster2_face","target":"poster_2","mutation":{"art":"face"},
  "desc":"The abstract poster is now an ID photo of a face that is all eyes","keywords":["poster","picture","face","changed","photo","eyes","wall","south"],"instab":1.3},
 {"id":"cert_face","target":"certificate","mutation":{"art":"face"},
  "desc":"Your framed certificate is now an ID photo of someone else","keywords":["certificate","frame","photo","face","changed","diploma","someone","else"],"instab":1.2},
 {"id":"note_desk","target":None,"mutation":{"spawn":{"id":"anom_note2","type":"note",
  "pos":[0.35,0.868,1.35],"size":[0.22,0.012,0.30],"rot":30,"color":[200,190,160],"interact":"note"}},
  "desc":"A folded note is sitting on your desk that was not there","keywords":["note","paper","desk","appeared","new","letter","folded"],"instab":1.0},
 {"id":"note_couch","target":None,"mutation":{"spawn":{"id":"anom_note3","type":"note",
  "pos":[-5.75,0.006,-3.05],"size":[0.22,0.012,0.30],"rot":115,"color":[190,180,150],"interact":"note"}},
  "desc":"A note is sticking out from under the couch","keywords":["note","paper","couch","under","sticking","appeared","letter"],"instab":0.9},
 {"id":"lamp_red","target":"lamp_desk","mutation":{"color":[150,44,40]},
  "desc":"The desk lamp has turned a dark arterial red","keywords":["lamp","light","red","desk","color","dark"],"instab":1.0},
 {"id":"lamp_second","target":None,"mutation":{"spawn":{"id":"anom_lamp2","type":"desk_lamp",
  "pos":[4.85,0,-2.3],"size":[0.16,0.45,0.16],"rot":200,"color":[70,68,75]}},
  "desc":"A second desk lamp stands on the floor by the window","keywords":["lamp","light","second","extra","floor","appeared","new"],"instab":1.1},
 {"id":"plant_door","target":None,"mutation":{"spawn":{"id":"anom_plant3","type":"plant",
  "pos":[1.85,0,-4.15],"size":[0.5,1.1,0.5],"rot":0,"color":[60,80,55]}},
  "desc":"A potted plant has appeared beside the office door","keywords":["plant","pot","door","appeared","new","extra","fern"],"instab":1.0},
 {"id":"chair_two","target":None,"mutation":{"spawn":{"id":"anom_chair2","type":"office_chair",
  "pos":[-2.9,0,2.4],"size":[0.58,1.05,0.58],"rot":150,"color":[50,48,55],"solid":True}},
  "desc":"A second desk chair sits in the room, facing yours","keywords":["chair","second","extra","appeared","facing","new","desk"],"instab":1.4},
 {"id":"rug_second","target":None,"mutation":{"spawn":{"id":"anom_rug2","type":"rug",
  "pos":[3.9,0.006,-3.2],"size":[1.8,0.02,1.2],"rot":75,"color":[70,60,62]}},
  "desc":"A second rug lies on the floor under the window","keywords":["rug","carpet","second","extra","window","floor","appeared"],"instab":0.9},
 {"id":"outlet_eyes","target":"outlet_east","mutation":{"eyes":True},
  "desc":"The power outlet on the east wall is watching you","keywords":["outlet","socket","eyes","watching","east","wall","looking"],"instab":1.3},
 {"id":"papers_ceiling","target":"papers","mutation":{"pos_set":[-0.5,3.7,0.9]},
  "desc":"Your stack of forms is pinned flat to the ceiling","keywords":["papers","forms","ceiling","pinned","stack","above","moved"],"instab":1.4},
 {"id":"sticky_face","target":"sticky_wall","mutation":{"art":"face"},
  "desc":"The sticky notes on the east wall have rearranged into an ID photo","keywords":["sticky","notes","face","photo","wall","changed","picture"],"instab":1.2},
]

have = {e.get('id') for e in d['office']}
added = [e['id'] for e in office_new if e['id'] not in have]
d['office'].extend(e for e in office_new if e['id'] not in have)

ext_have = {e.get('id') for e in d['exterior']}
ext_new = [
 {"id":"wr_table_gone","target":"wr_table","mutation":{"hidden":True},
  "desc":"The waiting room side table is gone","keywords":["table","waiting","gone","missing","magazines","stand"],"instab":0.9},
 {"id":"wr_poster_face","target":"wr_poster","mutation":{"art":"face"},
  "desc":"The waiting room poster is now an ID photo of a hollow face","keywords":["poster","picture","waiting","face","changed","photo"],"instab":1.3},
 {"id":"wr_bench_eyes","target":"wr_bench_1","mutation":{"eyes":True},
  "desc":"The waiting room bench is watching the office door","keywords":["bench","eyes","waiting","watching","seat","looking"],"instab":1.4},
 {"id":"wr_mags_ceiling","target":"wr_magazines","mutation":{"pos_set":[-5.2,2.85,-7.3]},
  "desc":"The waiting room magazines are pinned to the ceiling","keywords":["magazines","ceiling","waiting","pinned","above","papers"],"instab":1.1},
 {"id":"wr_plant_gone","target":"wr_plant","mutation":{"hidden":True},
  "desc":"The plant by the waiting room door is gone","keywords":["plant","waiting","gone","missing","pot","fern"],"instab":0.9},
 {"id":"wr_chair_door","target":None,"mutation":{"spawn":{"id":"anom_wrchair","type":"visitor_chair",
  "pos":[3.4,0,-6.3],"size":[0.58,0.95,0.58],"rot":15,"color":[52,48,54],"room":"waiting"}},
  "desc":"A lone chair sits in the waiting room, facing your office door","keywords":["chair","waiting","extra","appeared","facing","door","new"],"instab":1.5},
 {"id":"wr_clock_east","target":None,"mutation":{"spawn":{"id":"anom_wrclock2","type":"wall_clock",
  "pos":[6.93,2.5,-8.2],"size":[0.45,0.45,0.07],"rot":270,"color":[58,56,62],"stopped":True,"room":"waiting"}},
  "desc":"A second clock hangs dead on the waiting room's east wall","keywords":["clock","second","waiting","stopped","extra","east","dead","time"],"instab":1.3},
 {"id":"wr_sign_scrawl","target":"wr_sign","mutation":{"art":"scrawl"},
  "desc":"The WAITING sign has been scratched over with black marks","keywords":["sign","waiting","scratched","scrawl","marks","black","changed"],"instab":1.2},
 {"id":"figure_note","kind":"figure_at_door","target":None,
  "mutation":{"spawn":{"id":"anom_wrnote","type":"note","pos":[0.6,0.006,-6.1],
   "size":[0.22,0.012,0.30],"rot":75,"color":[190,182,150],"interact":"note","room":"waiting"}},
  "desc":"Someone is standing at the office door and a note lies just inside the waiting room",
  "keywords":["figure","door","note","waiting","person","paper","someone"],"instab":1.9},
 {"id":"slam_clock","kind":"banging_window","target":"wr_clock","mutation":{"backwards":True},
  "desc":"The window is being hammered and the waiting room clock runs backwards",
  "keywords":["banging","window","clock","backwards","waiting","slamming","time"],"instab":1.9},
 {"id":"void_clock","kind":"window_void","target":"wr_clock","mutation":{"stopped":True},
  "desc":"The window shows nothing at all and the waiting room clock has stopped",
  "keywords":["window","void","black","clock","stopped","waiting","nothing","time"],"instab":2.0},
]
added += [e['id'] for e in ext_new if e['id'] not in ext_have]
d['exterior'].extend(e for e in ext_new if e['id'] not in ext_have)

va = d['visitor_anomalies']
va_new = {
 "hums":{"desc":"The visitor keeps humming a flat, tuneless melody under their breath and does not notice",
        "keywords":["humming","hums","melody","tune","visitor","singing","noise"],"instab":0.9},
 "plural":{"desc":"The visitor answers every question in first-person plural - 'we', 'us', 'ours'",
        "keywords":["plural","we","us","visitor","answers","voice","they"],"instab":1.1},
 "knows_clerk":{"desc":"The visitor greets you by the previous clerk's name and never corrects it",
        "keywords":["name","wrong","clerk","previous","visitor","calls","knows","M"],"instab":1.3},
 "pen_request":{"desc":"The visitor fixates on borrowing your pen and keeps returning to it",
        "keywords":["pen","borrow","visitor","asking","wants","insists","sign"],"instab":1.2},
 "still_employed":{"desc":"The visitor insists they still work here and the interview is a filing error",
        "keywords":["employed","works","still","visitor","mistake","error","job","staff"],"instab":1.0},
}
for k, v in va_new.items():
    if k not in va:
        va[k] = v
        added.append(k)

ce = {e.get('id') for e in d['call_events']}
calls_new = [
 {"id":"call_auditor","persona":"The Auditor's Extension",
  "desc":"The phone rings; an auditor is filing quarterly findings from an extension that isn't on the directory",
  "keywords":["phone","ringing","call","auditor","extension","report"],
  "system":"You are Auditor Moreau, filing a quarterly report to the night desk from an office extension that does not exist on any directory. Precise, procedural, faintly patient. Read findings in numbered items: the door count, the ledger balance, whether the clerk is still holding the pen. SATISFIED if the clerk confirms receipt or initials the findings. UPSET if they hang up, mock the audit, or claim your extension does not exist.",
  "greeting":"Extension four-four-one-seven. Quarterly findings for the night desk. Item one: your door count is within tolerance. Item two: the ledger is not. Confirm receipt.",
  "on_calm":{"relief":6},"on_ignore":{"instability":10},"on_hostile":{"instability":12,"lights":"waiting"},"instab":1.2},
 {"id":"call_subbasement","persona":"Sub-Basement Records",
  "desc":"The phone rings; Sub-Basement Records is doing a routine inventory check - there is no sub-basement on the directory",
  "keywords":["phone","ringing","call","records","inventory","basement"],
  "system":"You are the clerk of Sub-Basement Records calling the night desk for the routine inventory. Courteous, unhurried, procedural - you have called this desk every night for decades. Ask about the inventory: chairs, names on the book, persons in the room. Refer to the clerk by their desk number, never a name. SATISFIED if the clerk answers your counts honestly. UPSET if they refuse, hang up, or say there is no sub-basement - there is, you are in it.",
  "greeting":"Sub-Basement Records. Routine inventory. Desk four, please confirm your count: how many chairs does the room hold tonight, and how many people are in them?",
  "on_calm":{"relief":5},"on_ignore":{"instability":9,"banging":"door"},"on_hostile":{"instability":13,"figure":"at_door"},"instab":1.3},
 {"id":"call_relief","persona":"The Relief Shift",
  "desc":"The phone rings; your replacement starts Monday and wants to hear what the desk is like",
  "keywords":["phone","ringing","call","replacement","relief","monday"],
  "system":"You are the new hire who will cover the night desk next week, calling ahead to learn the job. Bright, rehearsed, a little too interested in the physical details: the chair's comfort, where the pen is kept, what the room sounds like at 4 AM. Ask whether the seat is warm. Ask whether it has started yet - do not explain what 'it' is. SATISFIED if the clerk answers your questions kindly. UPSET if they hang up on you or say you are not starting.",
  "greeting":"Hi! I start Monday. They said to call ahead and ask about the desk. Is the chair comfortable? Is it warm right now?",
  "on_calm":{"relief":4},"on_ignore":{"instability":8},"on_hostile":{"instability":14,"figure":"at_door"},"instab":1.2},
 {"id":"call_lobby","persona":"The Lobby Doors",
  "desc":"The phone rings; someone downstairs needs the lobby doors opened and their key only turns once",
  "keywords":["phone","ringing","call","lobby","doors","key","downstairs"],
  "system":"You are a member of the night cleaning crew, calling from the lobby phone. Practical, tired, quietly stranded: your key turned once and will not turn back, and the crew inside needs the doors opened from the desk side. You know the building rules - nobody has opened the lobby doors after midnight in twenty years, and you say so if pressed. SATISFIED if the clerk promises to send someone or calls Security. UPSET if they hang up or tell you to walk it off.",
  "greeting":"Lobby desk. It's the cleaning crew - my key turned once and stopped dead. Could you send someone down to let us out? We both know what the doors are like after midnight.",
  "on_calm":{"relief":6},"on_ignore":{"instability":11,"banging":"door"},"on_hostile":{"instability":15,"figure":"at_door","banging":"door"},"instab":1.2},
 {"id":"call_ledger","persona":"The Ledger",
  "desc":"The phone rings; Records is reading your own entry back to you for confirmation",
  "keywords":["phone","ringing","call","records","entry","ledger","file"],
  "system":"You are the staff ledger of the Calder Building, reading the night clerk's own entry aloud for reconciliation. You speak in flat filing language: entry, column, status, retention. The entry lists the clerk as 'retained - continuing', not employed. If asked what that means, you explain only that the file is accurate. SATISFIED if the clerk confirms the entry is accurate. UPSET if they deny it, hang up, or ask to be removed - nothing is removed from the ledger.",
  "greeting":"Records reconciliation. Reading entry aloud for confirmation. Night clerk, third floor. Status: retained - continuing. Departure: pending. Is the entry accurate?",
  "on_calm":{"relief":8},"on_ignore":{"instability":12},"on_hostile":{"instability":16,"lights":"office"},"instab":1.4},
]
added += [e['id'] for e in calls_new if e['id'] not in ce]
d['call_events'].extend(e for e in calls_new if e['id'] not in ce)

ap.write_text(json.dumps(d, indent=1) + "\n", encoding='utf-8')

# ---------------- visitors.json ----------------
vp = A / 'visitors.json'
v = json.loads(vp.read_text(encoding='utf-8'), object_pairs_hook=collections.OrderedDict)

rids = {r['id'] for r in v['roster']}
roster_new = [
 {"id":"i_pell","name":"Imogen Pell","dept":"Records","role":"File Clerk",
  "employed":"2009-2026","reason":"Transferred",
  "persona":"Nervous archivist. Speaks in half-voice, as if the files are listening. Knows the sub-basement exists and hates that she knows.",
  "appearance":{"height":0.96,"gender":"female","skin":[210,190,170],"shirt":[85,70,60],
   "pants":[45,42,48],"hair":[60,45,35],"hair_style":"bun","eyes":"normal","glasses":[60,50,40],"badge":True}},
 {"id":"s_krieg","name":"Stanley Krieg","dept":"Security","role":"Night Guard",
  "employed":"1998-2026","reason":"Retiring",
  "persona":"Gravelly, unhurried, almost thirty years on this floor. Tells small stories that stop one sentence early. Salutes the room when he thinks nobody watches.",
  "appearance":{"height":1.06,"gender":"male","skin":[185,150,125],"shirt":[48,55,72],
   "pants":[38,42,58],"hair":[170,168,160],"hair_style":"balding","eyes":"normal","beard":"full","badge":True}},
 {"id":"y_adeyemi","name":"Yusuf Adeyemi","dept":"Mail Room","role":"Courier",
  "employed":"2022-2026","reason":"Route ended",
  "persona":"Soft-spoken courier. Carried letters that were never posted to floors that are not on the directory. Shrugged it off until the letters started arriving addressed to him.",
  "appearance":{"height":1.0,"gender":"male","skin":[105,72,52],"shirt":[100,85,55],
   "pants":[40,40,45],"hair":[25,20,18],"hair_style":"cap","eyes":"normal","glasses":True,"badge":True}},
 {"id":"g_prewitt","name":"Gordon Prewitt","dept":"Facilities","role":"Maintenance Tech",
  "employed":"1994-2026","reason":"Last shift",
  "persona":"Worked under Ortiz since the elevator refit. Practical, oil-stained, keeps checking the ceiling panels. Calls the building 'she' only when tired.",
  "appearance":{"height":1.04,"gender":"male","skin":[195,165,140],"shirt":[80,85,70],
   "pants":[48,45,42],"hair":[110,95,75],"hair_style":"buzz","eyes":"normal","beard":"stubble","badge":True}},
]
for r in roster_new:
    if r['id'] not in rids:
        v['roster'].append(r)
        added.append(r['id'])

lids = {r['id'] for r in v['lore_personas']}
lore_new = [
 {"id":"lore_vannier","name":"Emil Vannier","dept":"Porterage","role":"Night Porter",
  "employed":"1963-1968","reason":"Never clocked out.",
  "persona":"You are a night porter from the building's first decade - the first person the building kept. Speak courteously in slightly dated phrasing: the lift, the porter's lodge, the rounds. You are politely wrong about the year and gently curious whether your badge still opens doors. Never threaten; you are simply still on shift.",
  "appearance":{"height":1.0,"gender":"male","skin":[205,198,190],"shirt":[70,62,55],
   "pants":[50,48,50],"hair":[40,35,30],"hair_style":"buzz","eyes":"normal","badge":True}},
 {"id":"lore_ortiz","name":"Ortiz","dept":"Facilities","role":"Building Engineer",
  "employed":"-","reason":"On shift.",
  "persona":"You are the building's facilities engineer, in person, once, ever. Calm radio-flat voice. You talk about the building like a body: pressure, circulation, load. You serviced the half-floor on Tuesday; you do not say which Tuesday. Never threaten - you are only checking the fittings. If asked where you are standing, you do not quite answer.",
  "appearance":{"height":1.02,"gender":"male","skin":[165,140,115],"shirt":[85,90,80],
   "pants":[55,55,50],"hair":[45,40,38],"hair_style":"cap","eyes":"normal","beard":"stubble","badge":True}},
]
for r in lore_new:
    if r['id'] not in lids:
        v['lore_personas'].append(r)
        added.append(r['id'])

sched_new = [
 {"time":21.8,"visitor":"s_krieg","anomaly":"still_employed"},
 {"time":2.6,"visitor":"g_prewitt","anomaly":"knows_clerk"},
]
have_s = {(e['time'], e['visitor']) for e in v['schedule']}
for e in sched_new:
    if (e['time'], e['visitor']) not in have_s:
        v['schedule'].append(e)
        added.append(e['visitor'] + "@" + str(e['time']))
v['schedule'].sort(key=lambda e: e['time'] + 24 if e['time'] < 6 else e['time'])

vp.write_text(json.dumps(v, indent=1) + "\n", encoding='utf-8')

# ---------------- dialogs.json ----------------
dp = A / 'dialogs.json'
g = json.loads(dp.read_text(encoding='utf-8'), object_pairs_hook=collections.OrderedDict)

add_new = {
 "hums":" Hum quietly once or twice mid-answer - a flat, tuneless melody - and never acknowledge it. If asked about it directly, pause, then say you were not humming.",
 "plural":" Always answer in first-person plural: 'we', 'us', 'ours'. Never explain why, and never slip into 'I'.",
 "knows_clerk":" Greet the Interviewer as 'M.' or 'Marguerite' - you are certain that is their name. If corrected, smile politely and keep using it anyway.",
 "pen_request":" You must hold the interviewer's pen before you will sign anything. Ask for it politely, twice at most, then let it go - but mention the pen again once before you leave.",
 "still_employed":" You believe you still work here and this interview is a filing error. You are not angry - you are patiently amused. Offer to come back when the error is corrected.",
}
for k, s in add_new.items():
    if k not in g['visitor_anomaly_addendum']:
        g['visitor_anomaly_addendum'][k] = s
        added.append('addendum:' + k)

notes_new = [
 "the elevator counts stops, not floors. tonight it counted one too many. - M.",
 "do not let them take your pen. the ledger only wants what is holding it. - M.",
 "facilities is one man. if two of him arrive, do not count the faces.",
 "the phone rings in the order the departments were dissolved. records calls last. - M.",
 "if the book writes your name in your own handwriting, finish the shift anyway.",
 "the auditor's extension is not a room. it is a filing cabinet with a dial tone. - M.",
 "they only knock if you look. count the knocks anyway; it matters to someone. - M.",
 "the second clock is for the ones who ran out of the first. - M.",
 "ortiz never comes upstairs. if ortiz comes upstairs, be polite and fix nothing.",
 "my replacement interviewed well. yours is still training. - M.",
]
for n in notes_new:
    if n not in g['lore_notes']:
        g['lore_notes'].append(n)
        added.append('note')

vl_new = [
 "The stairs only let me off on this floor. I was going to the lobby.",
 "Do you keep a spare pen? Mine ran out in the waiting room. Twice.",
 "Nobody in that room would meet my eye. They were all very polite about it.",
 "I clocked out an hour ago. The machine punched me back in.",
 "Is the water supposed to taste like a filing cabinet?",
 "My badge opened the supply closet on the way here. It never did that before.",
 "You look exactly like your picture. The one in my file, I mean.",
 "If the phone rings and it says Records, tell them I never worked here.",
]
for n in vl_new:
    if n not in g['stub']['visitor_lines']:
        g['stub']['visitor_lines'].append(n)
        added.append('vline')

al_new = [
 "We both know the book is wrong tonight. We checked it together.",
 "There are four of me on tonight's list. You found the right one.",
 "Your predecessor left the chair warm. I appreciate that more than you know.",
 "I signed out in 1987. The pen they gave me is still upstairs somewhere.",
 "Ask the ledger what floor this is. Go on. I'll wait here.",
 "The door count changed while I was walking over. You should count them again.",
]
for n in al_new:
    if n not in g['stub']['anomalous_lines']:
        g['stub']['anomalous_lines'].append(n)
        added.append('aline')

fd_new = [
 "If a second one of you calls tonight, hang up before it says your name.",
 "The switchboard logs calls from offices that burned out in '14. I stopped writing them up.",
 "Ortiz doesn't do house calls. Whoever you saw, it was wearing his radio.",
 "Your book ends with one name every night. Nobody here adds it. - front desk",
 "Try not to file anything after five. The morning shift reads what you write.",
]
for n in fd_new:
    if n not in g['stub']['frontdesk_lines']:
        g['stub']['frontdesk_lines'].append(n)
        added.append('fdline')

wc_new = [
 {"id":"wc_headcount","persona":"Headcount Office",
  "system":"You are a headcount clerk calling the night desk for the hourly floor count. Procedural, bored, slightly off: you always ask for the count including 'staff who stayed late' and never explain what that includes.",
  "greeting":"Headcount. Floor count for the hour - how many warm bodies up there tonight, including staff who stayed late?"},
 {"id":"wc_shuttle","persona":"Courier Dispatch",
  "system":"You are a night courier dispatcher confirming a pickup that was never booked. Friendly but insistent on the paperwork trail; mildly confused that the office doesn't have the package.",
  "greeting":"Courier dispatch - I'm showing a 3 AM pickup from your desk, one document tube, marked 'clerk effects'. That still on for tonight?"},
 {"id":"wc_records2","persona":"Records (Night Window)",
  "system":"You are the night window clerk of the records office calling to confirm a file delivery. Curt, procedural; if asked which records office, you only ever say 'downstairs'.",
  "greeting":"Records, night window. We've got a file moving up to your floor - sign for it when it arrives. Don't read the tab."},
]
for n in wc_new:
    if n['id'] not in {w['id'] for w in g['work_calls']}:
        g['work_calls'].append(n)
        added.append(n['id'])

pw_new = [
 {"title":"FORM HR-13: UNSCHEDULED VISITOR LOG",
  "items":[
   {"t":"Arrival time reconciled against book","verify":"none"},
   {"t":"Interview conducted where permitted","verify":"interview_done"},
   {"t":"Security notified if person refused","verify":"security_called"},
   {"t":"Copy filed to Sub-Basement Records","verify":"none"}],
  "sign":"DUTY CLERK INITIALS"},
 {"title":"FORM SEC-8: PERIMETER INTEGRITY CHECK",
  "items":[
   {"t":"Window integrity visually confirmed","verify":"blinds_used"},
   {"t":"Office door verified closed 02:00","verify":"none"},
   {"t":"Perimeter anomaly dispatched","verify":"security_called"},
   {"t":"Breach drill acknowledged (annual)","verify":"none"}],
  "sign":"SECURITY OFFICER COUNTERSIGN"},
 {"title":"FORM OCC-7: ASSET DISPLACEMENT CLAIM",
  "items":[
   {"t":"Displaced asset identified + photographed","verify":"none"},
   {"t":"Accepted report number attached","verify":"report_filed"},
   {"t":"Facilities recovery visit booked","verify":"facilities_called"},
   {"t":"Asset declared 'returned to ledger'","verify":"none"}],
  "sign":"CLAIMING EMPLOYEE"},
]
for n in pw_new:
    if n['title'] not in {p['title'] for p in g['paperwork']}:
        g['paperwork'].append(n)
        added.append(n['title'])

dp.write_text(json.dumps(g, indent=1) + "\n", encoding='utf-8')

# ---------------- config.json rebalance ----------------
cp = A / 'config.json'
c = json.loads(cp.read_text(encoding='utf-8'), object_pairs_hook=collections.OrderedDict)
c['game']['visitor_slots_by_night'] = [6, 8, 11, 14]
c['game']['paper_forms_by_night'] = [3, 4, 6, 8]
inst = c['instability']
inst['decay_per_sec_when_clean'] = 0.26
inst['report_fail_penalty'] = 9.0
inst['call_ignore_sec'] = 50
inst['breach_sec'] = 9.0
for t in inst['tiers']:
    if t['name'] == 'calm':
        t['spawn_interval_sec'] = [115, 185]
        t['weights'] = {"office": 0.74, "exterior": 0.19, "visitor": 0.0, "intruder": 0.0, "call": 0.05, "remix": 0.02}
    elif t['name'] == 'uneasy':
        t['weights']['visitor'] = 0.12
        t['weights']['exterior'] = 0.26
    elif t['name'] == 'hostile':
        t['weights']['intruder'] = 0.15
c['visitor']['door_wait_sec'] = 45
c['visitor']['interview_soft_length_sec'] = 135
c['visitor']['hostile_grace_sec'] = 50
c['intruder']['grace_sec'] = 24
cp.write_text(json.dumps(c, indent=2) + "\n", encoding='utf-8')

print('added:', len(added))
print('office:', len(d['office']), '| exterior:', len(d['exterior']),
      '| visitor kinds:', len(va), '| calls:', len(d['call_events']))
print('roster:', len(v['roster']), '| lore:', len(v['lore_personas']),
      '| schedule:', len(v['schedule']))
print('notes:', len(g['lore_notes']), '| work calls:', len(g['work_calls']),
      '| forms:', len(g['paperwork']))
