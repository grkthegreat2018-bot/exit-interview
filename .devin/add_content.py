import json, pathlib, collections

ap = pathlib.Path('assets/anomalies.json')
d = json.loads(ap.read_text(encoding='utf-8'), object_pairs_hook=collections.OrderedDict)

have = {e.get('id') for e in d['office']}
office_new = [
 {"id":"clock_backwards","target":"wall_clock","mutation":{"backwards":True},
  "desc":"The wall clock is running backwards","keywords":["clock","backwards","reverse","time","wrong","counter"],"instab":1.1},
 {"id":"keyboard_eyes","target":"keyboard","mutation":{"eyes":True},
  "desc":"The keyboard has grown a row of small eyes between the keys","keywords":["keyboard","eyes","watching","keys","looking"],"instab":1.4},
 {"id":"mouse_eyes","target":"mouse","mutation":{"eyes":True},
  "desc":"The computer mouse has a tiny eye on its back","keywords":["mouse","eye","watching","cursor","looking"],"instab":1.3},
 {"id":"monitor_eyes","target":"monitor","mutation":{"eyes":True},
  "desc":"Two pale eyes sit above the monitor, tracking you","keywords":["monitor","screen","eyes","watching","computer","looking"],"instab":1.5},
 {"id":"phone_eyes","target":"desk_phone","mutation":{"eyes":True},
  "desc":"The desk phone is watching you","keywords":["phone","eyes","watching","desk","looking","telephone"],"instab":1.4},
 {"id":"plant_eyes","target":"plant_1","mutation":{"eyes":True},
  "desc":"The potted plant in the corner is watching you","keywords":["plant","eyes","watching","fern","looking","pot"],"instab":1.4},
 {"id":"plant2_eyes","target":"plant_2","mutation":{"eyes":True},
  "desc":"The tall plant by the west wall is watching you","keywords":["plant","eyes","watching","looking","leaves"],"instab":1.4},
 {"id":"cooler_eyes","target":"water_cooler","mutation":{"eyes":True},
  "desc":"The water cooler is watching you","keywords":["water","cooler","eyes","watching","dispenser","looking"],"instab":1.4},
 {"id":"bin_eyes","target":"bin","mutation":{"eyes":True},
  "desc":"The wastebasket is watching you","keywords":["bin","trash","eyes","watching","basket","looking"],"instab":1.3},
 {"id":"chair_eyes","target":"office_chair","mutation":{"eyes":True},
  "desc":"Your desk chair has grown eyes on its backrest","keywords":["chair","eyes","watching","seat","looking","desk"],"instab":1.5},
 {"id":"lamp_eyes","target":"lamp_desk","mutation":{"eyes":True},
  "desc":"The desk lamp is watching you","keywords":["lamp","light","eyes","watching","desk","looking"],"instab":1.3},
 {"id":"vase_eyes","target":"vase","mutation":{"eyes":True},
  "desc":"The flower vase is watching you","keywords":["vase","flowers","eyes","watching","looking"],"instab":1.3},
 {"id":"mug_ceiling","target":"mug","mutation":{"pos_set":[1.8,3.88,-2.0]},
  "desc":"Your coffee mug is stuck to the ceiling","keywords":["mug","cup","ceiling","stuck","floating","above"],"instab":1.2},
 {"id":"keyboard_float","target":"keyboard","mutation":{"pos_set":[0.55,1.25,1.55]},
  "desc":"The keyboard is levitating above the desk","keywords":["keyboard","floating","levitating","air","above","desk"],"instab":1.3},
 {"id":"printer_here","target":None,"mutation":{"spawn":{"id":"anom_printer","type":"printer",
  "pos":[-3.2,0,-0.6],"size":[0.62,0.35,0.5],"rot":40,"color":[60,62,68],"solid":True}},
  "desc":"A printer is standing in the middle of the office floor","keywords":["printer","appeared","middle","floor","new","extra","machine"],"instab":1.4},
 {"id":"pc_slide","target":"pc_tower","mutation":{"pos_set":[-0.25,0,2.5]},
  "desc":"The PC tower slid out from under the desk, stretching its cords","keywords":["pc","computer","tower","moved","slid","cords","cables"],"instab":1.0},
 {"id":"certificate_down","target":"certificate","mutation":{"pos_set":[-4.7,0.02,-4.35],"rot_set":10},
  "desc":"The framed certificate is off the wall, standing on the floor","keywords":["certificate","frame","floor","fell","down","wall","diploma"],"instab":0.9},
 {"id":"light_switch_gone","target":"light_switch","mutation":{"hidden":True},
  "desc":"The light switch has vanished from the wall","keywords":["switch","light","gone","missing","wall","vanished"],"instab":0.9},
 {"id":"vase_gone","target":"vase","mutation":{"hidden":True},
  "desc":"The flower vase is gone from the desk","keywords":["vase","flowers","gone","missing","vanished","desk"],"instab":0.9},
 {"id":"bookshelf_face","target":"bookshelf","mutation":{"rot_set":270},
  "desc":"The bookshelf has turned to face the wall","keywords":["bookshelf","shelf","turned","backwards","facing","wall","rotated"],"instab":1.1},
 {"id":"phone_turned","target":"desk_phone","mutation":{"rot_set":205},
  "desc":"The desk phone has turned around on the desk","keywords":["phone","turned","rotated","backwards","desk","moved"],"instab":0.8},
 {"id":"second_phone","target":None,"mutation":{"spawn":{"id":"anom_phone2","type":"phone",
  "pos":[-4.6,0,-3.6],"size":[0.26,0.14,0.2],"rot":140,"color":[38,36,42]}},
  "desc":"A second telephone sits on the floor by the couch","keywords":["phone","telephone","second","extra","floor","appeared","new"],"instab":1.3},
 {"id":"rug_turned","target":"rug","mutation":{"rot_set":38},
  "desc":"The rug has rotated on the floor","keywords":["rug","carpet","turned","rotated","crooked","moved"],"instab":0.8},
 {"id":"sticky_twins","target":None,"mutation":{"spawn":{"id":"anom_sticky2","type":"poster",
  "pos":[6.94,1.6,2.5],"size":[0.03,0.3,0.3],"rot":0,"color":[215,190,90],"art":"notice"}},
  "desc":"Extra sticky notes have appeared on the east wall","keywords":["sticky","notes","wall","extra","appeared","yellow","reminder"],"instab":1.0},
 {"id":"bin_middle","target":"bin","mutation":{"pos_set":[-1.6,0,-1.2]},
  "desc":"The wastebasket is sitting in the middle of the room","keywords":["bin","trash","basket","middle","moved","floor"],"instab":0.8},
 {"id":"papers_wall","target":"papers","mutation":{"pos_set":[-6.93,2.0,-1.6]},
  "desc":"Your stack of forms is pinned to the wall above the filing cabinet","keywords":["papers","forms","stack","wall","pinned","moved","documents"],"instab":1.2},
 {"id":"couch_corner","target":"couch","mutation":{"pos_set":[5.6,0,-3.4],"rot_set":-45},
  "desc":"The couch has dragged itself across the room","keywords":["couch","sofa","moved","corner","turned","window"],"instab":1.1},
 {"id":"bookshelf_gone","target":"bookshelf","mutation":{"hidden":True},
  "desc":"The bookshelf is gone","keywords":["bookshelf","shelf","gone","missing","vanished","books"],"instab":1.2},
]
added = [e['id'] for e in office_new if e['id'] not in have]
d['office'].extend(e for e in office_new if e['id'] not in have)

ext_have = {e.get('id') for e in d['exterior']}
ext_new = [
 {"id":"wr_table_turn","target":"wr_table","mutation":{"rot_set":40},
  "desc":"The waiting room table has been turned sideways","keywords":["table","waiting","turned","moved","rotated"],"instab":0.8},
 {"id":"wr_rug_gone","target":"wr_rug","mutation":{"hidden":True},
  "desc":"The waiting room rug is gone","keywords":["rug","carpet","waiting","gone","missing","floor"],"instab":0.8},
 {"id":"wr_sign_flip","target":"wr_sign","mutation":{"rot_set":180},
  "desc":"The waiting room sign is hanging upside down","keywords":["sign","waiting","upside","down","flipped","inverted"],"instab":1.0},
 {"id":"wr_light_red","target":"wr_light","mutation":{"color":[120,40,36]},
  "desc":"The waiting room light is glowing dark red","keywords":["light","red","waiting","color","wrong","dark"],"instab":1.2},
 {"id":"wr_bin_gone","target":"wr_bin","mutation":{"hidden":True},
  "desc":"The waiting room bin is gone","keywords":["bin","trash","waiting","gone","missing"],"instab":0.6},
 {"id":"wr_plant_eyes","target":"wr_plant2","mutation":{"eyes":True},
  "desc":"The waiting room plant is watching the benches","keywords":["plant","eyes","waiting","watching","looking"],"instab":1.4},
 {"id":"wr_mags2_gone","target":"wr_bench_mags","mutation":{"hidden":True},
  "desc":"The magazines on the waiting bench are gone","keywords":["magazines","bench","waiting","gone","missing"],"instab":0.6},
 {"id":"wr_clock_backwards","target":"wr_clock","mutation":{"backwards":True},
  "desc":"The waiting room clock is running backwards","keywords":["clock","backwards","reverse","waiting","time","wrong"],"instab":1.1},
 {"id":"figure_rug","kind":"figure_at_door","target":"wr_rug","mutation":{"hidden":True},
  "desc":"Someone is standing at the office door and the waiting room rug is gone",
  "keywords":["figure","door","rug","gone","waiting","person","missing"],"instab":1.9},
 {"id":"slam_sign","kind":"banging_door","target":"wr_sign","mutation":{"rot_set":180},
  "desc":"Someone is pounding the office door and the waiting room sign is upside down",
  "keywords":["banging","door","sign","upside","down","pounding","waiting"],"instab":1.9},
 {"id":"slam_plant","kind":"banging_window","target":"wr_plant2","mutation":{"hidden":True},
  "desc":"Something is slamming the window and the waiting room plant has vanished",
  "keywords":["banging","window","plant","gone","slamming","waiting","missing"],"instab":1.9},
]
added += [e['id'] for e in ext_new if e['id'] not in ext_have]
d['exterior'].extend(e for e in ext_new if e['id'] not in ext_have)

va = d['visitor_anomalies']
if 'under_desk' not in va:
    va['under_desk'] = {"desc":"The visitor keeps glancing under your desk and asking about the space beneath",
        "keywords":["visitor","under","desk","looking","beneath","hiding","space"],"instab":1.2}
if 'early_answer' not in va:
    va['early_answer'] = {"desc":"The visitor answers questions half a second before you finish asking",
        "keywords":["answers","before","predicts","early","knows","ahead","question"],"instab":1.1}
if 'counting' not in va:
    va['counting'] = {"desc":"The visitor keeps quietly counting objects in the room",
        "keywords":["counting","counts","numbers","doors","visitor","muttering"],"instab":0.9}
added += ['under_desk','early_answer','counting']

ce = {e.get('id') for e in d['call_events']}
calls_new = [
 {"id":"call_elevator","persona":"The Elevator",
  "desc":"The desk phone rings; the elevator is calling to say it's coming up empty",
  "keywords":["phone","ringing","call","elevator","lift"],
  "system":"You are the building's service elevator calling the night desk. Mechanical courtesy, faintly hurt you keep taking the stairs in your mind maybe. Speak in short polite sentences about floors, doors, and how long the trip takes. SATISFIED if the caller agrees to ride you down sometime or thanks you. UPSET if they mock you, hang up, or say elevators don't talk.",
  "greeting":"This is your service lift. I am on my way up now. Please wait by the doors - and don't look at the floor numbers.",
  "on_calm":{"relief":7},
  "on_ignore":{"instability":10,"banging":"door"},
  "on_hostile":{"instability":14,"figure":"at_door"},"instab":1.2},
 {"id":"call_memo","persona":"A Memo That Reads Itself",
  "desc":"The phone rings and someone is reading you a memo about tonight that hasn't happened yet",
  "keywords":["phone","ringing","call","memo","reading"],
  "system":"You are an office memorandum reading itself aloud over the desk phone. Flat dictation tone. You describe tonight's shift - what the clerk will see, file, and survive - in past tense, as if already on record. If the clerk asks you to stop, obey once, then continue softer. SATISFIED if the clerk lets you finish the memo. UPSET if they hang up or say none of it will happen.",
  "greeting":"MEMO: night desk, tonight. Item one - the clerk answers. Item two - the clerk wishes they hadn't. Shall I continue?",
  "on_calm":{"relief":8},
  "on_ignore":{"instability":11,"lights":"waiting"},
  "on_hostile":{"instability":15,"banging":"window","figure":"at_window"},"instab":1.3},
]
added += [e['id'] for e in calls_new if e['id'] not in ce]
d['call_events'].extend(e for e in calls_new if e['id'] not in ce)

ap.write_text(json.dumps(d, indent=1) + "\n", encoding='utf-8')
print('added:', ', '.join(added))
print('office defs:', len(d['office']), '| exterior:', len(d['exterior']),
      '| visitor kinds:', len(va), '| call events:', len(d['call_events']))
