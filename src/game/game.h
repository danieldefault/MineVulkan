#pragma once
#include <cstdint>
#include <vector>

#include "core/game_input.h"
#include "core/mesher.h"
#include "core/player.h"
#include "core/touch_controls.h"
#include "core/ui.h"
#include "core/world.h"
#include "render/renderer.h"

struct GameConfig {
    uint32_t seed = 1337;
    int renderRadius = 8;   // em chunks
    int genPerFrame = 4;    // chunks gerados por frame
    int meshPerFrame = 4;   // chunks remalhados por frame
    bool touchUi = false;   // desenha joystick/pulo
};

// Liga mundo, jogador e renderer. Independente de plataforma.
class Game {
public:
    Game(Renderer& renderer, const GameConfig& cfg);

    // Chamar depois que o renderer tiver um device (primeira surface).
    void onRendererReady();
    void frame(double now, FrameInput in);

    TouchControls& touch() { return touch_; }
    Player& player() { return player_; }
    World& world() { return world_; }

private:
    void remeshDirtyChunks();
    void applyActions(const FrameInput& in, const Vec3& eye);
    void buildUi();

    Renderer& renderer_;
    GameConfig cfg_;
    World world_;
    Player player_;
    Vec3 prevPos_;  // posição antes do último passo de física (para interpolar a câmera)
    TouchControls touch_;
    UiBuilder ui_;
    std::vector<ChunkVertex> meshScratch_;
    std::vector<uint32_t> blockColors_;
    int selected_ = 0;
    bool texturesUploaded_ = false;
    double lastTime_ = -1.0;
    double accumulator_ = 0.0;
    double fpsTimer_ = 0.0;
    int fpsFrames_ = 0;
};
