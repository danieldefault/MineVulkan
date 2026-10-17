#pragma once
#include <array>

#include "game_input.h"
#include "ui.h"

// Posições dos elementos da HUD em pixels lógicos.
struct HudLayout {
    float slot = 0, hotbarX = 0, hotbarY = 0;  // hotbar centralizada embaixo
    float jumpX = 0, jumpY = 0, jumpR = 0;     // botão de pulo (direita)
    float stickX = 0, stickY = 0, stickR = 0;  // posição de repouso do joystick (esquerda)
};
HudLayout computeHudLayout(float w, float h, int hotbarSize);

// Controles touch estilo mobile:
//  - metade esquerda: joystick virtual (nasce onde o dedo toca)
//  - resto da tela: arrastar = olhar; toque curto = colocar bloco; segurar = quebrar
//  - botão de pulo à direita, hotbar embaixo (toque seleciona)
class TouchControls {
public:
    void setScreen(float w, float h, int hotbarSize);
    void onDown(int id, float x, float y, double t);
    void onMove(int id, float x, float y);
    void onUp(int id, double t);
    void onCancelAll();

    void poll(double t, FrameInput& out);  // chamado 1x por frame
    void buildUi(UiBuilder& ui) const;

private:
    enum class Role { Idle, Move, Look, Jump, Hotbar };
    struct Touch {
        int id = -1;
        Role role = Role::Idle;
        float x0 = 0, y0 = 0, x = 0, y = 0, lastX = 0, lastY = 0;
        double t0 = 0, lastBreak = 0;
        bool moved = false, breaking = false;
        int slot = -1;
    };
    Touch* find(int id);

    std::array<Touch, 10> touches_{};
    float w_ = 1, h_ = 1;
    int hotbarSize_ = 1;
    HudLayout hud_;
    float pendingYaw_ = 0, pendingPitch_ = 0;
    bool pendingPlace_ = false;
    int pendingSlot_ = -1;
};
