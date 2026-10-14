import json
p = 'assets/anomalies.json'
d = json.load(open(p))
# severity: subtle ~0.5, standard 1.0, major ~1.5, severe ~2.0+
office = {"plant_moved":1.0,"poster_dark":0.8,"mug_gone":0.8,"extra_chair":1.2,"cabinet_open":0.9,
"photo_face":1.0,"clock_stop":0.5,"rug_gone":0.9,"keyboard_gone":0.9,"printer_moved":1.0,
"plant2_dark":0.7,"mug_moved":0.6,"papers_scatter":0.5,"office_lights":1.8,"desk_turned":1.6,
"rug_ceiling":1.5,"mug_giant":1.4,"papers_float":1.3,"plant_giant":1.3,"bookshelf_door":1.5,
"second_door":1.6,"eyes_mug":1.4,"eyes_cabinet":1.4,"eyes_clock":1.4,"eyes_photo":1.4}
ext = {"figure_window":1.6,"wr_lights_out":1.5,"wr_empty":1.3,"figure_door":1.6,"window_flip":1.5,
"window_void":1.7,"banging_door":1.8,"banging_window":1.8,"waiting_mutated":1.8,"hall_long":1.4}
van = {"unscheduled":1.0,"repeat":1.1,"record_mismatch":0.9,"impossible_visitor":1.6,"hostile":2.0}
for e in d['office']: e['instab'] = office.get(e['id'], 1.0)
for e in d['exterior']: e['instab'] = ext.get(e['id'], 1.0)
for k,v in d['visitor_anomalies'].items(): v['instab'] = van.get(k, 1.0)
d['intruder']['instab'] = 2.2
for e in d['call_events']: e['instab'] = 1.2
json.dump(d, open(p,'w'), indent=2)
print('instab fields written')
