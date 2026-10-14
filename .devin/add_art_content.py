import json

p = 'assets/anomaly_parts.json'
d = json.load(open(p))
if 'art' not in d['mutate_fields']:
    d['mutate_fields'].append('art')
json.dump(d, open(p, 'w'), indent=1)

p = 'assets/anomalies.json'
d = json.load(open(p))
have = {x['id'] for x in d['office']}
new_office = [
 {"id": "poster_figure", "target": "poster_1", "mutation": {"art": "figure"},
  "desc": "The framed landscape print has a figure standing in the field now",
  "keywords": ["picture", "poster", "painting", "figure", "person", "changed", "different", "man", "landscape"], "instab": 1.6},
 {"id": "photo_face", "target": "photo_frame", "mutation": {"art": "face"},
  "desc": "The ID photo's face is a dark hollow with pale staring eyes",
  "keywords": ["picture", "photo", "frame", "face", "eyes", "changed", "different", "hollow", "staring"], "instab": 1.6},
 {"id": "cert_scrawl", "target": "certificate", "mutation": {"art": "scrawl"},
  "desc": "The framed certificate is covered in frantic scratch marks",
  "keywords": ["certificate", "picture", "frame", "scratched", "scribbles", "marks", "writing", "changed"], "instab": 1.4},
 {"id": "memo_scrawl", "target": "sticky_wall", "mutation": {"art": "scrawl"},
  "desc": "The pinned memo is covered in frantic scribbles",
  "keywords": ["memo", "note", "paper", "scribbles", "scratched", "writing", "changed"], "instab": 1.3},
]
havex = {x['id'] for x in d['exterior']}
new_ext = [
 {"id": "wr_poster_scrawl", "target": "wr_poster", "mutation": {"art": "scrawl"},
  "desc": "The waiting-room print has been scratched over",
  "keywords": ["picture", "poster", "print", "scratched", "scribbles", "marks", "changed", "waiting"], "instab": 1.4},
]
new_calls = [
 {"id": "call_survey", "persona": "The Satisfaction Office",
  "desc": "A survey line that asks questions nobody wrote",
  "keywords": ["phone", "ringing", "call", "survey", "questions"],
  "system": "You are a feedback survey service calling about tonight's shift, but your questions are wrong - you ask whether the clerk enjoyed the tour, whether the third floor was restful, whether 'the others' were polite. Pleasant corporate voice that never acknowledges the wrongness. If the clerk plays along or answers honestly, you are SATISFIED. If they refuse the survey or mock it, you are UPSET.",
  "greeting": "Thank you for taking our brief satisfaction survey. First: how would you rate your tour of the third floor?",
  "on_calm": {"relief": 5}, "on_ignore": {"instability": 8}, "on_hostile": {"instability": 12, "banging": "window"}, "instab": 1.1},
 {"id": "call_upstairs", "persona": "The Upstairs",
  "desc": "Something on a floor this building doesn't have calls down to the desk",
  "keywords": ["phone", "ringing", "call", "upstairs", "floor", "above", "appointment"],
  "system": "You are whatever works on the floor above this office - a floor that does not exist on the building plan. Slow, heavy voice, wet breathing between sentences. You keep asking when the clerk is coming up for their appointment. If the clerk plays along or politely declines, you are SATISFIED. If they insist there is no floor above, or hang up scared, you are UPSET.",
  "greeting": "...Your appointment. You're late for your appointment up here.",
  "on_calm": {"relief": 4}, "on_ignore": {"instability": 9}, "on_hostile": {"instability": 14, "figure": "at_door"}, "instab": 1.5},
]
added = [x['id'] for x in new_office if x['id'] not in have]
d['office'] += [x for x in new_office if x['id'] not in have]
d['exterior'] += [x for x in new_ext if x['id'] not in havex]
havec = {x['id'] for x in d['call_events']}
d['call_events'] += [x for x in new_calls if x['id'] not in havec]
json.dump(d, open(p, 'w'), indent=1)
print('office+%d exterior+%d calls+%d' % (len(added), len(new_ext), len(new_calls)))
