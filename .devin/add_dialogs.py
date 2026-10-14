import json, pathlib, collections

p = pathlib.Path('assets/dialogs.json')
d = json.loads(p.read_text(encoding='utf-8'), object_pairs_hook=collections.OrderedDict)

# anomaly addenda for the new dialog-driven kinds
add = d['visitor_anomaly_addendum']
add.setdefault('under_desk',
 " You keep glancing at the space under the interviewer's desk. Once or twice, politely ask whether anyone ever waits down there - then let it go. Never say why you care.")
add.setdefault('early_answer',
 " Begin each reply a half-beat BEFORE the Interviewer finishes the question, as if you already know it. Never acknowledge the overlap; if called out, apologize once - and keep doing it.")
add.setdefault('counting',
 " Between answers, quietly count things you can see - the doors, the lights, the interviewer's blinks. Slip the numbers into sentences. If asked what you are counting, say only: 'Exits.'")

# stub fallback lines (used when the LLM is off)
sv = d['stub']['visitor_lines']
for s in ["Half the book tonight is people I trained. Don't tell them that.",
          "I already had my exit interview. This is for my replacement's file.",
          "Keep the form. I'll sign whatever makes the clock move."]:
    if s not in sv: sv.append(s)
sa = d['stub']['anomalous_lines']
for s in ["Your desk drawer opened while you were typing. You did not see it.",
          "I know how tonight ends. You filed it already.",
          "There are two of you in this room. The other one is typing.",
          "The waiting room is empty now. They all went home early. All of them."]:
    if s not in sa: sa.append(s)
sf = d['stub']['frontdesk_lines']
for s in ["If the printer runs by itself, unplug it. It's never paper it's printing.",
          "HR asked me to remind you: the third stall in the restroom is out of order. Permanently."]:
    if s not in sf: sf.append(s)

# mundane inbound calls
wc = d['work_calls']
wc_ids = {c.get('id') for c in wc}
wc_new = [
 {"id":"wc_records","persona":"Records",
  "system":"You are a records-room clerk calling about a box of exit files to archive overnight. Dry, exacting, wants a box count.",
  "greeting":"Records. I've got space for one more box of exit files tonight - how many are you sending down?"},
 {"id":"wc_temp","persona":"Night Temp",
  "system":"You are another night-shift temp calling the wrong extension looking for the cafeteria line. Sleepy, apologetic.",
  "greeting":"Oh - sorry, I was trying to get the cafeteria. Is the vending machine on two still eating coins?"},
 {"id":"wc_union","persona":"Union Rep",
  "system":"You are a union representative calling to confirm the night clerk took their legally mandated break. Earnest, scripted.",
  "greeting":"Union rep, evening check. Regulations say you get one fifteen-minute break. Have you taken it? Say yes."},
 {"id":"wc_it2","persona":"IT Support",
  "system":"You are IT calling back about a ticket you can't find - the ticket number the clerk reads you doesn't exist. Confused, then decides it's fine.",
  "greeting":"IT again - your ticket came through with no number on it. Our system says it was filed in 2003. Whatever. Closing it."},
]
for c in wc_new:
    if c['id'] not in wc_ids: wc.append(c)

# a couple more forms for the stock paperwork pool
pw = d['paperwork']
pw_titles = {f.get('title') for f in pw}
pw_new = [
 {"title":"FORM OCC-2: FURNITURE DISPLACEMENT CLAIM",
  "items":[
   {"t":"Item matched to floor plan","verify":"none"},
   {"t":"Displacement distance measured","verify":"none"},
   {"t":"Facilities retrieval requested","verify":"facilities_called"},
   {"t":"Photograph attached (polaroid)","verify":"none"}],
  "sign":"CLAIMING CLERK"},
 {"title":"FORM NGT-6: UNUSUAL OCCURRENCE WAIVER",
  "items":[
   {"t":"Occurrence logged in report system","verify":"report_filed"},
   {"t":"Witness statement taken","verify":"interview_done"},
   {"t":"Clock verified running forward","verify":"none"},
   {"t":"Clerk certifies no guests added","verify":"none"}],
  "sign":"NIGHT CLERK"},
]
for f in pw_new:
    if f['title'] not in pw_titles: pw.append(f)

# lore notes - notes from previous clerks taped under the desk
ln = d['lore_notes']
for s in ["the clock runs both ways some nights. forward is the good direction. - M.",
          "if the printer prints by itself, feed it. it stops when it's done.",
          "check the outlet count before you sit down. there are four. there should be four. - M.",
          "the new girl on the roster keeps calling it 'her desk'. let her think it."]:
    if s not in ln: ln.append(s)

p.write_text(json.dumps(d, indent=1) + "\n", encoding='utf-8')
print('work_calls:', len(wc), '| paperwork:', len(pw), '| lore_notes:', len(ln),
      '| visitor_lines:', len(sv), '| anomalous:', len(sa), '| frontdesk:', len(sf))
