#pragma once
#include "common.h"

// rebindable controls - persisted in settings.json, edited on the CONTROLS tab
struct KeyMap {
    int fwd = KEY_W, back = KEY_S, left = KEY_A, right = KEY_D;
    int sprint = KEY_LEFT_SHIFT, crouch = KEY_Q;
    int interact = KEY_E, swap = KEY_TAB;
};

class Player {
public:
    Vector3 pos{0, 0, 3.6f};   // feet
    float yaw = 180.f;         // degrees, 180 = looking -z (toward door)
    float pitch = 0.f;
    float height = 1.62f;
    float radius = 0.32f;
    float speed = 3.1f;
    float bobT = 0.f;
    bool sprinting = false;         // Shift held while moving (set in Update)
    bool crouching = false;         // Q held (or forced while under a desk)
    bool forcedCrouch = false;      // under a desk - can't stand until clear
    bool hiding = false;            // crouched inside the desk's footprint
    float eyeH = 1.62f;             // live eye height - lerps when crouching
    bool moving = false;            // movement keys held this frame
    float strafe = 0.f;             // smoothed -1..1 lateral input (camera roll)
    float moveAmt = 0.f;            // smoothed 0..1 "is walking" (sway fades)
    float lastSpeed = 0.f;          // actual move speed applied this frame

    Vector3 Eye() const { return {pos.x, pos.y + eyeH, pos.z}; }

    Vector3 Forward() const {
        float yr = yaw * DEG2RAD, pr = pitch * DEG2RAD;
        return {std::sin(yr) * std::cos(pr), std::sin(pr), std::cos(yr) * std::cos(pr)};
    }

    void Update(float dt, const std::vector<AABB>& solids, const AABB& bounds,
                bool lookEnabled, bool moveEnabled, float sensitivity,
                float sprintMul = 1.6f, float crouchH = 0.78f,
                const KeyMap& km = KeyMap{}) {
        sprinting = false;
        // hold-to-crouch; standing back up under a desk is impossible, the
        // caller forces crouch while the player sits inside its footprint
        bool wantCrouch = moveEnabled && IsKeyDown(km.crouch);
        if (forcedCrouch) wantCrouch = true;
        crouching = wantCrouch;
        eyeH += ((crouching ? crouchH : height) - eyeH) * std::min(1.f, dt * 10.f);
        if (lookEnabled) {
            Vector2 md = GetMouseDelta();
            yaw -= md.x * sensitivity;
            pitch -= md.y * sensitivity;
            pitch = std::clamp(pitch, -80.f, 80.f);
        }
        if (moveEnabled) {
            Vector2 mv{0, 0};
            if (IsKeyDown(km.fwd)) mv.y += 1;
            if (IsKeyDown(km.back)) mv.y -= 1;
            if (IsKeyDown(km.left)) mv.x -= 1;
            if (IsKeyDown(km.right)) mv.x += 1;
            moving = (mv.x != 0.f || mv.y != 0.f);
            if (moving) {
                float len = std::sqrt(mv.x * mv.x + mv.y * mv.y);
                mv.x /= len; mv.y /= len;
                float yr = yaw * DEG2RAD;
                Vector3 fwd{std::sin(yr), 0, std::cos(yr)};
                Vector3 right{-std::cos(yr), 0, std::sin(yr)};
                // hold Shift to sprint; faster bob sells the speed
                float sp = speed;
                if (crouching) sp *= 0.45f;                     // slow and low
                else if (IsKeyDown(km.sprint) ||
                         (km.sprint == KEY_LEFT_SHIFT && IsKeyDown(KEY_RIGHT_SHIFT))) {
                    sp *= sprintMul; sprinting = true;
                }
                lastSpeed = sp;
                pos.x += (fwd.x * mv.y + right.x * mv.x) * sp * dt;
                pos.z += (fwd.z * mv.y + right.z * mv.x) * sp * dt;
                bobT += dt * (sprinting ? 12.5f : crouching ? 6.f : 9.f);
            } else { bobT = 0; lastSpeed = 0; }
            // smoothed channels for camera feel: strafe roll + walk weight
            strafe += ((moving ? mv.x : 0.f) - strafe) * std::min(1.f, dt * 9.f);
            moveAmt += ((moving ? 1.f : 0.f) - moveAmt) * std::min(1.f, dt * 7.f);
        } else {
            moving = false; lastSpeed = 0;
            strafe += (0.f - strafe) * std::min(1.f, dt * 9.f);
            moveAmt += (0.f - moveAmt) * std::min(1.f, dt * 7.f);
        }
        // collisions - a crouched player fits under things (the desk), so the
        // standing-height AABB is skipped while low enough to clear it
        Vector2 p{pos.x, pos.z};
        for (auto& s : solids) {
            if (pos.y + (crouching ? crouchH : height) < s.mn.y || pos.y > s.mx.y) continue;
            p = CircleVsAABB(p, radius, s);
        }
        p.x = std::clamp(p.x, bounds.mn.x + radius, bounds.mx.x - radius);
        p.y = std::clamp(p.y, bounds.mn.z + radius, bounds.mx.z - radius);
        pos.x = p.x; pos.z = p.y;
    }

    float BobOffset() const { return std::sin(bobT) * 0.028f; }
};
