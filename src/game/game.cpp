#include "game.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "core/texture_gen.h"

namespace {
constexpr double kFixedStep = 1.0 / 60.0;
constexpr float kReach = 6.0f;
}  // namespace

Game::Game(Renderer& renderer, const GameConfig& cfg) : renderer_(renderer), cfg_(cfg), world_(cfg.seed) {
    // Área inicial gerada de forma síncrona para posicionar o jogador.
    const int sx = 8, sz = 8;
    for (int dz = -2; dz <= 2; ++dz)
        for (int dx = -2; dx <= 2; ++dx) world_.generateChunk(dx, dz);
    player_.pos = Vec3(sx + 0.5f, static_cast<float>(world_.findSpawnY(sx, sz)), sz + 0.5f);
    player_.yaw = 0.6f;
    player_.pitch = -0.25f;
}

void Game::onRendererReady() {
    if (texturesUploaded_) return;
    auto tex = generateBlockTextures();
    MipChain mips = buildMipChain(tex, TILE_COUNT);
    renderer_.uploadBlockTextures(mips.data, TILE_COUNT);
    blockColors_.clear();
    for (int i = 0; i < kHotbarSize; ++i) blockColors_.push_back(blockAverageColor(tex, kHotbar[i]));
    texturesUploaded_ = true;
}

void Game::applyActions(const FrameInput& in) {
    if (!in.breakBlock && !in.placeBlock) return;
    RayHit hit = raycastBlocks(world_, player_.eye(), player_.forward(), kReach);
    if (!hit.hit) return;
    if (in.breakBlock) {
        if (blockBreakable(world_.getBlock(hit.x, hit.y, hit.z))) world_.setBlock(hit.x, hit.y, hit.z, BLOCK_AIR);
    } else if (in.placeBlock) {
        int px = hit.x + hit.nx, py = hit.y + hit.ny, pz = hit.z + hit.nz;
        if (py < 0 || py >= CHUNK_HEIGHT) return;
        if (player_.overlapsBlock(px, py, pz)) return;
        if (world_.getBlock(px, py, pz) != BLOCK_AIR) return;
        world_.setBlock(px, py, pz, kHotbar[selected_]);
    }
}

void Game::remeshDirtyChunks() {
    struct Cand { float d; Chunk* c; };
    std::vector<Cand> cands;
    for (const auto& kv : world_.chunks()) {
        Chunk* c = kv.second.get();
        if (!c->meshDirty || !world_.hasAllNeighbors(c->cx, c->cz)) continue;
        float dx = (c->cx + 0.5f) * CHUNK_SIZE - player_.pos.x;
        float dz = (c->cz + 0.5f) * CHUNK_SIZE - player_.pos.z;
        cands.push_back({dx * dx + dz * dz, c});
    }
    int n = std::min<int>(cfg_.meshPerFrame, static_cast<int>(cands.size()));
    std::partial_sort(cands.begin(), cands.begin() + n, cands.end(),
                      [](const Cand& a, const Cand& b) { return a.d < b.d; });
    for (int i = 0; i < n; ++i) {
        Chunk* c = cands[i].c;
        buildChunkMesh(world_, c->cx, c->cz, meshScratch_);
        renderer_.uploadChunkMesh(chunkKey(c->cx, c->cz), c->cx, c->cz, meshScratch_);
        c->meshDirty = false;
    }
}

void Game::buildUi() {
    ui_.clear();
    float w = renderer_.logicalWidth(), h = renderer_.logicalHeight();

    // Mira
    float cs = h * 0.022f, ct = std::max(2.0f, h * 0.004f);
    ui_.rect(w * 0.5f - cs - 1, h * 0.5f - ct * 0.5f - 1, cs * 2 + 2, ct + 2, uiColor(0, 0, 0, 110));
    ui_.rect(w * 0.5f - ct * 0.5f - 1, h * 0.5f - cs - 1, ct + 2, cs * 2 + 2, uiColor(0, 0, 0, 110));
    ui_.rect(w * 0.5f - cs, h * 0.5f - ct * 0.5f, cs * 2, ct, uiColor(255, 255, 255, 220));
    ui_.rect(w * 0.5f - ct * 0.5f, h * 0.5f - cs, ct, cs * 2, uiColor(255, 255, 255, 220));

    // Hotbar
    HudLayout hud = computeHudLayout(w, h, kHotbarSize);
    float s = hud.slot;
    ui_.rect(hud.hotbarX - 3, hud.hotbarY - 3, s * kHotbarSize + 6, s + 6, uiColor(0, 0, 0, 120));
    for (int i = 0; i < kHotbarSize; ++i) {
        float x = hud.hotbarX + s * static_cast<float>(i);
        ui_.frame(x, hud.hotbarY, s, s, 2.0f, uiColor(90, 90, 90, 200));
        float pad = s * 0.2f;
        uint32_t col = i < static_cast<int>(blockColors_.size()) ? blockColors_[i] : uiColor(255, 0, 255, 255);
        ui_.rect(x + pad, hud.hotbarY + pad, s - 2 * pad, s - 2 * pad, col);
        ui_.rect(x + pad, hud.hotbarY + pad, s - 2 * pad, (s - 2 * pad) * 0.25f, uiColor(255, 255, 255, 40));
    }
    ui_.frame(hud.hotbarX + s * static_cast<float>(selected_) - 2, hud.hotbarY - 2, s + 4, s + 4,
              std::max(3.0f, s * 0.07f), uiColor(255, 255, 255, 240));

    if (cfg_.touchUi) touch_.buildUi(ui_);
}

void Game::frame(double now, FrameInput in) {
    if (!renderer_.beginFrame()) return;  // sem swapchain válido neste momento

    double dt = lastTime_ < 0 ? kFixedStep : std::clamp(now - lastTime_, 0.0, 0.1);
    lastTime_ = now;

    float w = renderer_.logicalWidth(), h = renderer_.logicalHeight();
    touch_.setScreen(w, h, kHotbarSize);
    if (cfg_.touchUi) touch_.poll(now, in);

    if (in.selectSlot >= 0) selected_ = std::clamp(in.selectSlot, 0, kHotbarSize - 1);
    if (in.slotDelta) selected_ = ((selected_ + in.slotDelta) % kHotbarSize + kHotbarSize) % kHotbarSize;

    player_.applyLook(in.player.lookYaw, in.player.lookPitch);
    accumulator_ += dt;
    while (accumulator_ >= kFixedStep) {
        player_.step(world_, in.player, static_cast<float>(kFixedStep));
        accumulator_ -= kFixedStep;
    }
    applyActions(in);

    world_.update(player_.pos.x, player_.pos.z, cfg_.renderRadius, cfg_.genPerFrame);
    for (int64_t key : world_.takeUnloaded()) renderer_.freeChunkMesh(key);
    remeshDirtyChunks();

    CameraParams cam;
    Vec3 eye = player_.eye();
    float aspect = w / std::max(h, 1.0f);
    float fogEnd = static_cast<float>(cfg_.renderRadius) * CHUNK_SIZE;
    cam.viewProj = perspectiveVk(1.2f, aspect, 0.1f, fogEnd + 32.0f) * lookAt(eye, eye + player_.forward(), Vec3(0, 1, 0));
    cam.position = eye;
    cam.fogEnd = fogEnd;
    buildUi();
    renderer_.endFrame(cam, ui_.verts);

    ++fpsFrames_;
    fpsTimer_ += dt;
    if (fpsTimer_ >= 2.0) {
        LOGI("FPS %.1f | chunks %zu | desenhados %u | pos %.1f %.1f %.1f", fpsFrames_ / fpsTimer_,
             world_.chunks().size(), renderer_.lastDrawnChunks(), player_.pos.x, player_.pos.y, player_.pos.z);
        fpsFrames_ = 0;
        fpsTimer_ = 0;
    }
}
