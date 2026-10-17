#pragma once
#include "player.h"

// Entrada de um frame, já traduzida da plataforma (touch ou teclado/mouse).
struct FrameInput {
    PlayerInput player;
    bool breakBlock = false;
    bool placeBlock = false;
    int selectSlot = -1;  // -1 = sem mudança
    int slotDelta = 0;    // roda do mouse
};
