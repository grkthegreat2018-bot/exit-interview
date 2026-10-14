import json, pathlib, collections

p = pathlib.Path('assets/visitors.json')
d = json.loads(p.read_text(encoding='utf-8'), object_pairs_hook=collections.OrderedDict)

roster_ids = {r.get('id') for r in d['roster']}
roster_new = [
 {"id":"k_oshiro","name":"Kenji Oshiro","dept":"Mailroom","role":"Mail Clerk",
  "employed":"2022-2026","reason":"Department closed",
  "persona":"Cheerful mail clerk who knows everyone's handwriting. Keeps a count of stamps in his head. Talks about parcels that arrive addressed to nobody.",
  "appearance":{"height":0.99,"skin":[205,175,145],"shirt":[85,90,70],"hair":[25,22,20],"eyes":"normal"}},
 {"id":"a_bexley","name":"Adele Bexley","dept":"Public Relations","role":"Press Officer",
  "employed":"2017-2026","reason":"Merger",
  "persona":"Polished PR officer, speaks in careful soundbites. Asks how the exit interview 'reads' and whether her statement is quotable.",
  "appearance":{"height":1.01,"skin":[225,195,170],"shirt":[120,55,65],"hair":[95,60,35],"eyes":"normal"}},
 {"id":"n_futura","name":"N. Futura","dept":"HR","role":"Night Clerk, Incoming",
  "employed":"2031-2041","reason":"Orientation",
  "persona":"ANOMALOUS: a clerk whose employment dates are ten years from now. Cheerfully calls tonight 'orientation', refers to the player by a slightly wrong name, and treats the office like they've worked here for years. If the dates are challenged, pauses, then says paperwork takes time.",
  "appearance":{"height":1.03,"skin":[200,190,180],"shirt":[70,75,85],"hair":[55,50,48],"eyes":"normal"},
  "always_anomaly":"impossible_visitor"},
]
for r in roster_new:
    if r['id'] not in roster_ids: d['roster'].append(r)

lore_ids = {r.get('id') for r in d['lore_personas']}
if 'lore_mara' not in lore_ids:
    d['lore_personas'].append(
     {"id":"lore_mara","name":"Mara Iliescu","dept":"Custodial","role":"Night Cleaner",
      "employed":"1976-2006","reason":"The wastebaskets stopped needing emptying.",
      "persona":"You cleaned this floor for thirty years and somehow have an exit interview tonight. Gruff, practical, motherly toward the furniture. You know which chair bites, which outlet hums, and why the bin in the corner should stay exactly where it is. You warn the clerk to empty the wastebasket before 3 AM - 'it fills itself otherwise'. Never threaten.",
      "appearance":{"height":0.93,"skin":[195,165,140],"shirt":[60,70,65],"hair":[160,155,150],"eyes":"normal"}})

# tag two mundane slots with the new dialog-driven anomaly kinds
for slot in d['schedule']:
    if slot.get('visitor') == 's_bell' and slot.get('anomaly') is None:
        slot['anomaly'] = 'counting'
    if slot.get('visitor') == 'e_lindqvist' and slot.get('anomaly') is None:
        slot['anomaly'] = 'under_desk'
    if slot.get('visitor') == 'r_halloran' and slot.get('anomaly') is None:
        slot['anomaly'] = 'early_answer'

p.write_text(json.dumps(d, indent=1) + "\n", encoding='utf-8')
print('roster:', len(d['roster']), '| lore:', len(d['lore_personas']), '| schedule:', len(d['schedule']))
