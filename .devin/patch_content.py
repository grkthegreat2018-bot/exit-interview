import json

# dialogs.json - readable lore slips for the note anomalies
p = 'assets/dialogs.json'
d = json.load(open(p))
d['lore_notes'] = [
    "the door count changed again. it was seven last shift. count them before you lock up. - M.",
    "whoever you are now - the chair goes missing on the bad nights. check the corners. sit anyway.",
    "they promoted the building. nobody else got the memo. that's why the elevator skips us.",
    "do not thank the tall ones. they remember gratitude. it is how they find the desk again.",
    "if a name appears twice in the book, the second one is not a person. i learned the hard way. - M.",
    "the water runs up on the wrong nights. maintenance is still pumping. they have been pumping since 1987.",
    "i left the report filed and the chair where it was. none of it mattered. the file said i was deceased.",
    "your handwriting is in more places than your desk. i'm sorry. some of it is instructions."
]
json.dump(d, open(p, 'w'), indent=1, ensure_ascii=False)

# visitors.json - rare lore personas injectable into mundane slots
p2 = 'assets/visitors.json'
v = json.load(open(p2))
v['lore_personas'] = [
    {"id": "lore_whitmore", "name": "Elias Whitmore", "dept": "Records",
     "role": "Night Clerk", "employed": "1987-1989", "reason": "Shift ended.",
     "persona": "You are a night clerk who worked this exact desk decades ago - or something wearing that memory. Speak quietly and warmly about the building: the third floor that is not on the directory, the interviews that never quite ended, the note you left under the desk. Never threaten. If asked the year, you are politely, briefly wrong.",
     "appearance": {"height": 1.0, "skin": [215, 200, 185], "shirt": [60, 58, 66], "hair": [180, 175, 165], "eyes": "normal"}},
    {"id": "lore_dana", "name": "Dana Moreau", "dept": "Compliance",
     "role": "Auditor", "employed": "-", "reason": "Routine check.",
     "persona": "You are an auditor who knows far too much about this office. You count the doors out loud. You ask whether the window still shows the car park. Drop quiet practical warnings: the minutes are wrong tonight, they only come in when a defense is open, the crew that answers a real call is the only crew. Leave satisfied if heard out.",
     "appearance": {"height": 0.98, "skin": [190, 170, 160], "shirt": [45, 50, 60], "hair": [30, 26, 24], "eyes": "normal"}},
    {"id": "lore_crouch", "name": "Old Crouch", "dept": "Maintenance",
     "role": "Caretaker", "employed": "1979-", "reason": "Permanent transfer to night shift.",
     "persona": "You are the building's caretaker, older than the directory print. Speak in short practical warnings - what to do when the banging starts, why the second door is wrong, why the chair migrates. Fond of the building the way one is fond of a dangerous dog. Answer plainly, creepily.",
     "appearance": {"height": 0.94, "skin": [120, 95, 80], "shirt": [52, 58, 50], "hair": [150, 145, 138], "eyes": "normal"}}
]
json.dump(v, open(p2, 'w'), indent=1, ensure_ascii=False)
print('lore notes:', len(d['lore_notes']), '| lore personas:', [x['id'] for x in v['lore_personas']])
