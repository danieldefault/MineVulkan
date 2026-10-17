#include "touch_controls.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kTapMaxTime = 0.30;     // s
constexpr double kHoldBreakTime = 0.40;  // s até quebrar
constexpr double kBreakRepeat = 0.30;    // s entre quebras segurando
constexpr float kLookSensitivity = 3.0f; // radianos por altura de tela arrastada
constexpr float kDeadZone = 0.12f;
}  // namespace

HudLayout computeHudLayout(float w, float h, int hotbarSize) {
    HudLayout l;
    float margin = h * 0.04f;
    l.slot = std::min(h * 0.095f, (w * 0.5f) / static_cast<float>(hotbarSize));
    l.hotbarX = (w - l.slot * static_cast<float>(hotbarSize)) * 0.5f;
    l.hotbarY = h - l.slot - margin * 0.5f;
    l.jumpR = h * 0.085f;
    l.jumpX = w - l.jumpR - margin * 2.0f;
    l.jumpY = h - l.jumpR - margin * 2.5f;
    l.stickR = h * 0.13f;
    l.stickX = l.stickR + margin * 2.5f;
    l.stickY = h - l.stickR - margin * 2.5f;
    return l;
}

void TouchControls::setScreen(float w, float h, int hotbarSize) {
    w_ = std::max(w, 1.0f);
    h_ = std::max(h, 1.0f);
    hotbarSize_ = hotbarSize;
    hud_ = computeHudLayout(w_, h_, hotbarSize_);
}

TouchControls::Touch* TouchControls::find(int id) {
    for (auto& t : touches_)
        if (t.id == id) return &t;
    return nullptr;
}

void TouchControls::onDown(int id, float x, float y, double t) {
    Touch* slot = find(id);
    if (!slot) slot = find(-1);
    if (!slot) return;
    Touch& tc = *slot;
    tc = Touch{};
    tc.id = id;
    tc.x0 = tc.x = tc.lastX = x;
    tc.y0 = tc.y = tc.lastY = y;
    tc.t0 = t;

    float jx = x - hud_.jumpX, jy = y - hud_.jumpY;
    float jr = hud_.jumpR * 1.3f;
    bool inHotbar = y >= hud_.hotbarY && x >= hud_.hotbarX &&
                    x < hud_.hotbarX + hud_.slot * static_cast<float>(hotbarSize_);
    if (jx * jx + jy * jy <= jr * jr) {
        tc.role = Role::Jump;
    } else if (inHotbar) {
        tc.role = Role::Hotbar;
        tc.slot = static_cast<int>((x - hud_.hotbarX) / hud_.slot);
        pendingSlot_ = std::clamp(tc.slot, 0, hotbarSize_ - 1);
    } else if (x < w_ * 0.4f) {
        // Só um dedo controla o joystick por vez.
        bool hasMove = false;
        for (const auto& o : touches_) hasMove |= (o.id != id && o.id != -1 && o.role == Role::Move);
        tc.role = hasMove ? Role::Look : Role::Move;
    } else {
        tc.role = Role::Look;
    }
}

void TouchControls::onMove(int id, float x, float y) {
    Touch* tc = find(id);
    if (!tc) return;
    tc->x = x;
    tc->y = y;
    if (tc->role == Role::Look) {
        float dx = x - tc->lastX, dy = y - tc->lastY;
        pendingYaw_ += dx / h_ * kLookSensitivity;
        pendingPitch_ -= dy / h_ * kLookSensitivity;
        float mx = x - tc->x0, my = y - tc->y0;
        if (mx * mx + my * my > (h_ * 0.03f) * (h_ * 0.03f)) tc->moved = true;
    }
    tc->lastX = x;
    tc->lastY = y;
}

void TouchControls::onUp(int id, double t) {
    Touch* tc = find(id);
    if (!tc) return;
    if (tc->role == Role::Look && !tc->moved && !tc->breaking && (t - tc->t0) < kTapMaxTime) pendingPlace_ = true;
    *tc = Touch{};
}

void TouchControls::onCancelAll() {
    for (auto& t : touches_) t = Touch{};
}

void TouchControls::poll(double t, FrameInput& out) {
    out.player.lookYaw += pendingYaw_;
    out.player.lookPitch += pendingPitch_;
    pendingYaw_ = pendingPitch_ = 0;
    if (pendingPlace_) out.placeBlock = true;
    pendingPlace_ = false;
    if (pendingSlot_ >= 0) out.selectSlot = pendingSlot_;
    pendingSlot_ = -1;

    for (auto& tc : touches_) {
        if (tc.id == -1) continue;
        switch (tc.role) {
            case Role::Move: {
                float dx = (tc.x - tc.x0) / hud_.stickR, dy = (tc.y - tc.y0) / hud_.stickR;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len > 1.0f) { dx /= len; dy /= len; len = 1.0f; }
                if (len > kDeadZone) {
                    out.player.moveX = dx;
                    out.player.moveZ = -dy;
                }
                break;
            }
            case Role::Jump: out.player.jump = true; break;
            case Role::Look:
                if (!tc.moved) {
                    double held = t - tc.t0;
                    if (!tc.breaking && held >= kHoldBreakTime) {
                        tc.breaking = true;
                        tc.lastBreak = t;
                        out.breakBlock = true;
                    } else if (tc.breaking && t - tc.lastBreak >= kBreakRepeat) {
                        tc.lastBreak = t;
                        out.breakBlock = true;
                    }
                }
                break;
            default: break;
        }
    }
}

void TouchControls::buildUi(UiBuilder& ui) const {
    const Touch* move = nullptr;
    bool jumpDown = false;
    const Touch* breaking = nullptr;
    for (const auto& tc : touches_) {
        if (tc.id == -1) continue;
        if (tc.role == Role::Move) move = &tc;
        if (tc.role == Role::Jump) jumpDown = true;
        if (tc.role == Role::Look && tc.breaking) breaking = &tc;
    }
    // Joystick
    float bx = move ? move->x0 : hud_.stickX, by = move ? move->y0 : hud_.stickY;
    ui.circle(bx, by, hud_.stickR, uiColor(0, 0, 0, move ? 70 : 40), 36);
    ui.ring(bx, by, hud_.stickR, hud_.stickR * 0.06f, uiColor(255, 255, 255, move ? 140 : 70), 36);
    float kx = bx, ky = by;
    if (move) {
        float dx = move->x - move->x0, dy = move->y - move->y0;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > hud_.stickR) { dx *= hud_.stickR / len; dy *= hud_.stickR / len; }
        kx += dx;
        ky += dy;
    }
    ui.circle(kx, ky, hud_.stickR * 0.42f, uiColor(255, 255, 255, move ? 150 : 80), 24);

    // Pulo
    ui.circle(hud_.jumpX, hud_.jumpY, hud_.jumpR, uiColor(255, 255, 255, jumpDown ? 120 : 55), 32);
    ui.ring(hud_.jumpX, hud_.jumpY, hud_.jumpR, hud_.jumpR * 0.07f, uiColor(255, 255, 255, 150), 32);
    float a = hud_.jumpR * 0.38f;
    UiVertex t0{hud_.jumpX, hud_.jumpY - a, uiColor(255, 255, 255, 200)};
    UiVertex t1{hud_.jumpX - a, hud_.jumpY + a * 0.6f, t0.color};
    UiVertex t2{hud_.jumpX + a, hud_.jumpY + a * 0.6f, t0.color};
    ui.verts.insert(ui.verts.end(), {t0, t2, t1});

    // Indicador de "segurando para quebrar"
    if (breaking) ui.ring(breaking->x, breaking->y, h_ * 0.05f, h_ * 0.008f, uiColor(255, 255, 255, 180), 24);
}
