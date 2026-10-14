# Task: menus/settings + world fixes

## Geometry facts (verified)
- desk: pos (0,0,1.15) size (3.4,.86,1.5) -> z span [0.40,1.90]. modesty panel z[0.40,0.48], legs x|>1.54, drawer pedestal x[1.04,1.54].
- pc_tower (-1.15,0,0.62) sz .55 -> back 0.345 pokes past desk edge 0.40 into modesty panel. MOVE to z=0.80 -> back 0.525.
- outlet_floor (-1.95,.,1.05): cord from pc crosses left leg x[-1.7,-1.54]. MOVE to (-1.35,.,2.06) (chair side, clear of chair z>=2.39).
- hall: door (5.6,-10.9) opening x[5,6.2]; nothing behind -> walkers erased in view. BUILD L-corridor: legA x[5,6.2] z[-12.9,-10.9], legB x[6.2,10.2] z[-12.9,-12.0]; end cap x=10.2. paths: ...->(5.6,-12.55)->(8.9,-12.55).
- PointBlocked: rooms only (z>-5 office / z<-5 waiting), wall band except door gap.
- PickStandSpot bug: ring spots can land wrong side of doorZ wall. FIX: same-room filter + fallback.
- crew spawn (5.6,-12.05) -> (8.9,-12.55); pathIn prepend [(5.6,-12.55),(5.6,-11.4),HALL]; pathOut tail ->(5.6,-11.4),(5.6,-12.55),(8.9,-12.55).
- closeToHall widen: x(4.2,9.6) z(-13.4,-9.2).
- visitor AtDoor: auto InviteIn when door rotY<-25 (open) after 1.6s.
- breach: add doorThreat (doorOpen||doorAnim>.45) && (figure_at_door|banger door); need=breach_sec*0.3 door, *0.35 broken window; warn at min(2.5,need*.45); breachDoor flag -> deathTitle.
- intruderT: drain mul = 1 + 1.2*windowBroken + 1.2*doorOpen(rotY<-25).

## Menus
- menuPage 0/1 shared for Title+Pause; settingsTab 0..3 (GENERAL AUDIO CONTROLS LLM); tooltips bottom area.
- KeyMap in player.h (fwd/back/left/right/sprint/crouch/interact/swap), persisted in settings.json "keys". KeyName() helper; swap-on-conflict; ESC cancels.
- Audio: volMaster(SetMasterVolume), audio.sfxMul, audio.ambMul; sliders.
- LLM: provider cycle, apikey field, model/endpoint text fields (settings override), ANOM toggle, PING test (testReq/testResult in PollLLM).
- persist: keys{}, vol_master/sfx/ambience, llm_endpoint, llm_model.

Build: cmake --build build --config Release; exe needs assets next to it.
