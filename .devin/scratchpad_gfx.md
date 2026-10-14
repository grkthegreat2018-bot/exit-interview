# GFX pass notes (fx.h / office.* / visitor.*)

## What changed
- fx.h POST_FS: added compact FXAA (Lottes reduced, 9 taps on texture0,
  offsets = 1.0/res i.e. RT texels) applied to the warped uv, then the
  existing CA r/b taps. Added `grade` uniform + host member (default 0.22,
  EI_GRADE env override). Grade = clamped smoothstep S-curve mix (~20%)
  + luma-driven cool-shadow/warm-highlight multiplicative tint; applied
  after CA, before grain/vignette. Blacks stay black (multiplicative only,
  S-curve fixes 0->0).
- office.h: new free fn `DrawBlobShadow(center, rx, rz, alpha, yawDeg)`.
- office.cpp: `BlobTex()` - one-time 64x64 radial-alpha texture (rgb=0,
  alpha smoothstepped to 0 at rim). DrawBlobShadow emits a flat RL_QUADS
  quad (both windings) at the given y using the proven
  rlBegin->rlSetTexture->verts->rlEnd order from DrawMonitorPoly; wraps
  verts in rlDisableDepthMask/rlEnableDepthMask so coplanar blobs blend
  instead of z-fighting.
- Office::Draw: blob loop after the item loop - any item with pos.y<=0.05
  and footprint min-axis >= 0.12m gets one (excludes door/rug, desk items
  at y=0.86, wall posters/clock/blinds/switch, ceiling lights). rx/rz =
  size/2 + 0.16 halo, alpha 0.40, yaw = f.rotY.
- Office::DrawShadowFigure: blob at feet (rx=rz=0.5*hs, alpha 0.45) -
  covers intruder, breach figure, exterior window/door figures.
- visitor.cpp DrawHumanoid: blob at {feet.x, 0.015, feet.z} (fixed floor y -
  feet.y rides the crew ladder lift), r=0.40*height, alpha 0.40. Covers
  visitor, ambient NPCs, banger, AND game.cpp's crew DrawCrew men since
  they all funnel through DrawHumanoid.

## Deliberate choices / caveats
- Blobs are emitted INSIDE the lit pass (no post-LitOff hook exists in
  Office/VisitorSystem and game.cpp is off-limits). Safe: texel rgb is 0,
  so base*acc stays 0 -> only fog pulls it toward fogColor at distance,
  which matches the floor. rlgl resets colDiffuse to WHITE per element
  (rlgl.h:3037) and rlBegin resets element textureId to the default white
  (rlgl.h:1477), so the blob texture never leaks onto later geometry.
- Blob y is hardcoded 0.015 (all floors are y=0 / 0.001). If a future
  room gets a raised floor, pass floor height via center.y instead.
- Blob texture is a static leak (16KB) - no UnloadTexture at shutdown;
  consistent with "make once at init" and harmless.
- Fxaa replaces only the base rgb sample; the CA r/b taps stay single-tap
  on purpose (cheap; the fringe is meant to be edgy). Alpha for finalColor
  still comes from the un-AA'd center tap `t` - alpha is 1.0 everywhere
  anyway.
- Grade S-curve input is clamped to [0,1] (cc) so HDR-ish values >1 can't
  invert through x*x*(3-2x); unclamped headroom survives via the mix.
- Not built here (other agents own game.cpp etc.) - syntax checked by
  re-reading only. Verify once with EI_SHOT + EI_GRADE=0 vs 0.5.
