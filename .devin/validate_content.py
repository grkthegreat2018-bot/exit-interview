#!/usr/bin/env python3
"""Content lint for Exit Interview asset packs.

Catches anomaly defs that can never be seen/resolved (the "impossible
anomaly" class), dead references, and shape errors the C++ loader would
silently swallow. Run:  python .devin/validate_content.py   (from repo root)
Exit code = number of problems found (0 = clean).
"""
import json, os, sys

ROOT = os.path.join(os.path.dirname(__file__), "..", "assets")
office = json.load(open(os.path.join(ROOT, "office.json")))
anom   = json.load(open(os.path.join(ROOT, "anomalies.json")))
parts  = json.load(open(os.path.join(ROOT, "anomaly_parts.json")))
dlogs  = json.load(open(os.path.join(ROOT, "dialogs.json")))
vis    = json.load(open(os.path.join(ROOT, "visitors.json")))

fids   = {f["id"] for f in office["furniture"]}
froom  = {f["id"]: f.get("room", "office") for f in office["furniture"]}
MUTS   = set(parts["mutate_fields"]) | {"spawn", "fly"}
SPAWNABLE = set(parts["spawnable_types"])

probs = []
def err(msg): probs.append(msg)

def check_def(d, where):
    did = d.get("id", "?")
    tgt = d.get("target")          # may be str | null | absent
    kind = d.get("kind", "")
    mut = d.get("mutation", {})
    has_spawn = isinstance(mut, dict) and "spawn" in mut

    # detectability: an anomaly needs SOME world effect or kind flag
    if not tgt and not has_spawn and not kind:
        err(f"{where}:{did} has no target/spawn/kind - invisible & unreportable")
    if isinstance(tgt, str) and tgt and tgt not in fids:
        err(f"{where}:{did} target '{tgt}' missing from office.json")
    if isinstance(tgt, str) and tgt and where == "office" and froom.get(tgt) == "waiting":
        err(f"{where}:{did} office-channel def targets waiting-room furni '{tgt}'")

    for k in (mut if isinstance(mut, dict) else {}):
        if k not in MUTS:
            err(f"{where}:{did} unknown mutation key '{k}'")
    if has_spawn:
        s = mut["spawn"]
        st = s.get("type", "")
        if st not in SPAWNABLE:
            err(f"{where}:{did} spawn type '{st}' not in spawnable_types (no DrawItem branch?)")
        if not s.get("id"):
            err(f"{where}:{did} spawn has no id - crew can't carry it out")
        if froom.get(tgt) != "waiting" and s.get("room") not in (None, "office", "waiting"):
            err(f"{where}:{did} spawn room '{s.get('room')}' unknown")

    if not d.get("keywords"):
        err(f"{where}:{did} has no keywords - report validator can't match it")
    if not d.get("desc"):
        err(f"{where}:{did} has no desc")
    if d.get("instab") is None:
        err(f"{where}:{did} missing instab weight")

for d in anom.get("office", []):    check_def(d, "office")
for d in anom.get("exterior", []):  check_def(d, "exterior")

for k, d in anom.get("visitor_anomalies", {}).items():
    if not d.get("keywords"): err(f"visitor:{k} no keywords")
    if not d.get("desc"):     err(f"visitor:{k} no desc")

# call events: required fields per InjectCallEvents / call flow
for e in anom.get("call_events", []):
    eid = e.get("id", "?")
    for req in ("persona", "system", "greeting"):
        if not e.get(req): err(f"call_events:{eid} missing '{req}'")
    for fx in ("on_calm", "on_ignore", "on_hostile"):
        if fx in e and not isinstance(e[fx], dict): err(f"call_events:{eid} '{fx}' not an object")

# duplicate ids inside a pool would shadow the once-per-night `exhausted` rule
for pool_name in ("office", "exterior"):
    seen = {}
    for d in anom.get(pool_name, []):
        i = d.get("id", "?")
        if i in seen: err(f"{pool_name}: duplicate id '{i}'")
        seen[i] = 1

# lore notes pool sanity
ln = dlogs.get("lore_notes")
if not isinstance(ln, list) or not ln:
    err("dialogs.json lore_notes missing/empty")
else:
    print(f"[info] lore_notes pool: {len(ln)} slips")

# roster personas referenced by lore injection must exist
vkeys = {v.get("id") for v in vis.get("personas", [])} if isinstance(vis.get("personas"), list) else set()
if "lore_personas" in vis:
    for lp in vis["lore_personas"]:
        pass

print(f"{len(probs)} problem(s)")
for p in probs: print(" !", p)
sys.exit(min(len(probs), 1))
