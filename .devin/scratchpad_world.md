# World/furniture quality pass - positions & caveats

## office.json changes
- couch -> recliner: pos (-6.3, 0, -3.4), size (1.05, 0.95, 1.1), rot KEPT 90.
  DrawItem draws the back on local -z; rot 90 maps local +z -> world +x, so the
  recliner faces the desk and its back sits ~7cm off the west wall inner face
  (x=-6.92). World AABB: x -6.85..-5.75, z -3.925..-2.875. Clear of
  filing_cabinet (z -1.91..-1.29), bookshelf (z 0.5..3.1), intruder spawn
  (-5.9,-4.2: 0.28 south of the AABB edge). Footrest VISUAL overhangs the AABB
  ~0.34 toward +x at y<0.2 (shin height, deliberate - recliner look).
- vase: id "vase", type "vase", pos (-1.45, 0.86, 0.85), size (0.13,0.30,0.13),
  color (185,190,195). Desk-front-left corner. Clear of mug/phone/papers/lamp/
  monitor/keyboard and all desk_top spawn anchors. Not solid, no interact.
- waiting_npc_seats: seat_h 0.46 added to the three bench seats
  (-3.2,-9.6 / -0.8,-9.6 / 2.2,-8.3). New bench cushions top out at ~0.465
  (s.y*0.93 for s.y=0.5) -> hips at 0.50. 4th seat (-5.0,-7.4) kept 0.5.
- certificate (office): poster, pos (-5.2, 1.9, -4.93), size (0.6,0.5,0.04),
  art "certificate". North wall left of the clock. Clear of recliner
  (y 1.65..2.15 above its 0.95 top) and wall_north spawn anchors.
- wr_plant2: plant, (3.85, 0, -10.55), size (0.5,1.0,0.5). Near hall door but
  WEST of it: task asked ~4.8 but wr_bin already sits at (4.6,-10.3) and the
  hall-door opening starts x=5.0; 3.85 clears both (extent x 3.6..4.1).
- wr_rug: rug, (-1.5, 0.005, -8.0), size (4.0,0.02,1.5) dark (52,44,50).
  x -3.5..0.5, z -8.75..-7.25: clears bench_1 (z>=-9.425), bench_2 (x>=0.9),
  wr_table (x<=-4.65). NOTE: PointBlocked checks ALL floor items (pos.y<0.6),
  so the rug blocks nav cells ~x -3.85..0.85, z -9.1..-6.9 - crews route
  around via the east/west corridors; door approach z -6.9..-5.1 stays open.
  ambient_arrival segment (2.0,-8.4)->(-2.5,-8.4) crosses it (fine visually).
- wr_sign: poster, (0.4, 2.15, -10.9), size (1.8,0.5,0.03), art "sign" (new
  kind: dark field + pale band + 7 glyph blocks). South wall z=-11 face
  (-10.92), thinZ + pos.z<0 -> art faces +z into the room. Clear of wr_clock
  (x -2.21..-1.79) and hall door (x 5.0..6.2).
- wr_bench_mags: paper_stack on bench_2, (1.3, 0.46, -8.35), size
  (0.3,0.05,0.22). Between the bench-2 seat NPC (2.2) and the bench end;
  sits on cushion top 0.465 (base at 0.46).

## office.cpp changes
- desk: added modesty panel + drawer pedestal. MODESTY PANEL (matches
  game.cpp part() collision exactly): CBox center (0, (s.y-0.13+0.04)/2,
  -s.z/2+0.04) = world z_center = desk.z - 0.71, size (s.x-0.4, s.y-0.17,
  0.08) -> y 0.04..0.73, z -0.75..-0.67 local. +z edge open = hide entrance.
  Drawer pedestal: x center s.x/2-0.41 (flush to +x leg inner face), 0.5 wide,
  y 0.05..s.y-0.13, depth s.z-0.44; 2 drawer fronts + handle bars (1 drawer if
  s.y<0.7 - keeps wr_table sane). Pedestal occupies under-desk space x
  1.04..1.54 local - no collision box for it (game.cpp only adds legs+modesty),
  acceptable: hide spawn is desk center, pedestal hugs the east leg.
- office_chair: 5-star base (5 rotated spokes + caster spheres), gas column,
  armrests (post+padded bar). Seat surface unchanged: seat top still y=0.5
  (CBox center 0.45 h 0.1) - VisitorSitPos/hip math unaffected.
  visitor_chair keeps the original 3-part branch.
- couch: now a recliner - low base, seat cushion + seam, fat arms with top
  pads, back panel hinged at seat and rlRotatef(-8) reclined with headrest
  pillow, footrest plate+pad out local +z (~0.34 jut).
- bench: own branch - 4 legs, seat frame, 2-3 segmented cushions
  (s.x>3 -> 3 segments), 2 back posts + low back rail (top ~s.y*1.32).
- poster: rewritten. Backing plate oversized +0.07 for a dark frame edge,
  4 wood lip bars proud of the face, all art on one plane just proud of the
  card (draw order decides ties - same trick the old notice art used).
  Arts kept: landscape (banded sky + horizon glow), portrait (matte margin),
  notice, abstract (wash + baseline). New arts: "certificate" (parchment,
  double rules, title, lines, seal+ribbon), "sign" (dark field, pale band,
  7 block glyphs).
- vase: new branch - 2 tapered DrawCylinderEx (belly+neck), 3 thin stems,
  3 Sph blooms (dusty rose/pale yellow). ~250 verts.
- ceiling_light: troffer - bezel plate behind + glowing diffuser + 4 rim
  bars wrapping the edge (rim lip 35mm below diffuser face).
- DrawRooms: baseboards (0.09 high, 0.02 proud, darker 50,46,50) on every wall
  face both rooms, broken at door x -0.65..0.65 and hall door x 5.0..6.2;
  crown molding full runs at ceiling line (bridges openings, correct look);
  window frame (jambs+head+sill, same trim as door frame) + sill ledge
  (0.24 deep at y=windowY.x, pokes 0.13 into office).
- Eyes/blob/figure code untouched.

## visitor.cpp changes
- DrawHumanoid seated legs: kneeY = max(feet.y + 0.28*height, hipY - 0.10) -
  on a 0.5 seat the knee no longer folds to the floor. Added flat shoe box
  (DrawCubeV under rlRotatef(yawDeg)) at each seated ankle, pointing +f.
  visitor.cpp has no CBox helper - used raw DrawCubeV (center-aligned).

## anomaly_parts.json
- "vase" added to spawnable_types AND existing_targets.

## Verify status
- python/node exec denied in this session -> JSON validated by inspection only.
  Re-run `python -c "import json; json.load(open('assets/office.json'));
  json.load(open('assets/anomaly_parts.json')); print('ok')"` from foreground.
- Not compiled (other files mid-edit). Things to sanity-check visually after
  build: vase vs monitor edge, footrest overhang vs player path, recliner back
  vs west wall gap, baseboard runs at the door gap, wr_rug under NPC feet.
