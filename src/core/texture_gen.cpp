#include "texture_gen.h"

#include <algorithm>
#include <cmath>

#include "block.h"
#include "noise.h"

namespace {

uint32_t rgba(float r, float g, float b, float a = 255.0f) {
    auto c = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 255.0f)); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}

uint32_t shade(int r, int g, int b, float k) {
    return rgba(static_cast<float>(r) * k, static_cast<float>(g) * k, static_cast<float>(b) * k);
}

float rnd(int x, int y, int salt) {
    return static_cast<float>(hash2i(x, y, 0xA511E9B3u + static_cast<uint32_t>(salt)) & 0xFFFF) / 65535.0f;
}

uint32_t pixel(int tile, int x, int y) {
    float n = rnd(x, y, tile);
    switch (tile) {
        case TILE_GRASS_TOP: return shade(95, 159, 53, 0.78f + 0.32f * n);
        case TILE_DIRT: return shade(134, 96, 67, n < 0.12f ? 0.65f : 0.82f + 0.25f * n);
        case TILE_GRASS_SIDE: {
            int edge = 3 + static_cast<int>(rnd(x, 0, 99) * 2.5f);
            if (y < edge) return shade(95, 159, 53, 0.78f + 0.3f * n);
            return shade(134, 96, 67, n < 0.12f ? 0.65f : 0.82f + 0.25f * n);
        }
        case TILE_STONE: {
            float blob = rnd(x / 3, y / 2, 7);
            float k = 0.82f + 0.2f * n - (blob < 0.18f ? 0.12f : 0.0f);
            return shade(128, 128, 128, k);
        }
        case TILE_SAND: return shade(219, 207, 163, 0.9f + 0.13f * n);
        case TILE_LOG_SIDE: {
            float stripe = ((x % 4) == 0) ? 0.75f : 1.0f;
            return shade(104, 82, 50, stripe * (0.82f + 0.25f * rnd(x, y / 3, 5)));
        }
        case TILE_LOG_TOP: {
            float dx = static_cast<float>(x) - 7.5f, dy = static_cast<float>(y) - 7.5f;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d > 7.0f) return shade(104, 82, 50, 0.85f + 0.2f * n);
            float ring = (static_cast<int>(d) % 2 == 0) ? 1.0f : 0.84f;
            return shade(176, 144, 90, ring * (0.92f + 0.1f * n));
        }
        case TILE_LEAVES: {
            if (n < 0.14f) return shade(40, 82, 28, 0.7f);
            return shade(58, 122, 38, 0.7f + 0.5f * n);
        }
        case TILE_PLANKS: {
            int board = y / 4;
            int seam = (board * 7 + 3) % 16;
            float k = 0.86f + 0.16f * rnd(x / 2, y, 11);
            if (y % 4 == 3 || x == seam) k *= 0.72f;
            return shade(164, 132, 80, k);
        }
        case TILE_COBBLE: {
            int ox = (y / 4) % 2 ? 2 : 0;
            int cx = (x + ox) / 4, cy = y / 4;
            bool mortar = ((x + ox) % 4 == 0) || (y % 4 == 0);
            if (mortar && rnd(x, y, 13) < 0.8f) return shade(80, 80, 80, 0.9f + 0.15f * n);
            float tone = 0.8f + 0.3f * rnd(cx, cy, 17);
            return shade(130, 130, 130, tone * (0.92f + 0.12f * n));
        }
        case TILE_BEDROCK: {
            float k = n < 0.35f ? 0.35f : (n < 0.7f ? 0.6f : 0.9f);
            return shade(110, 110, 110, k);
        }
        default: return rgba(255, 0, 255);
    }
}

}  // namespace

std::vector<uint32_t> generateBlockTextures() {
    std::vector<uint32_t> out(static_cast<size_t>(TILE_COUNT) * kTileSize * kTileSize);
    for (int t = 0; t < TILE_COUNT; ++t)
        for (int y = 0; y < kTileSize; ++y)
            for (int x = 0; x < kTileSize; ++x)
                out[(static_cast<size_t>(t) * kTileSize + y) * kTileSize + x] = pixel(t, x, y);
    return out;
}

MipChain buildMipChain(const std::vector<uint32_t>& level0, int layers) {
    MipChain mc;
    mc.data.push_back(level0);
    int size = kTileSize;
    while (size > 1) {
        int ns = size / 2;
        const auto& src = mc.data.back();
        std::vector<uint32_t> dst(static_cast<size_t>(layers) * ns * ns);
        for (int l = 0; l < layers; ++l)
            for (int y = 0; y < ns; ++y)
                for (int x = 0; x < ns; ++x) {
                    uint32_t acc[4] = {0, 0, 0, 0};
                    for (int k = 0; k < 4; ++k) {
                        int sx = x * 2 + (k & 1), sy = y * 2 + (k >> 1);
                        uint32_t p = src[(static_cast<size_t>(l) * size + sy) * size + sx];
                        for (int c = 0; c < 4; ++c) acc[c] += (p >> (8 * c)) & 0xFF;
                    }
                    uint32_t r = 0;
                    for (int c = 0; c < 4; ++c) r |= ((acc[c] + 2) / 4) << (8 * c);
                    dst[(static_cast<size_t>(l) * ns + y) * ns + x] = r;
                }
        mc.data.push_back(std::move(dst));
        size = ns;
    }
    mc.levels = static_cast<int>(mc.data.size());
    return mc;
}

uint32_t blockAverageColor(const std::vector<uint32_t>& textures, uint8_t block) {
    int tile = blockTile(block, block == BLOCK_GRASS ? 2 : 0);  // grama: cor do topo
    uint32_t acc[3] = {0, 0, 0};
    for (int i = 0; i < kTileSize * kTileSize; ++i) {
        uint32_t p = textures[static_cast<size_t>(tile) * kTileSize * kTileSize + i];
        for (int c = 0; c < 3; ++c) acc[c] += (p >> (8 * c)) & 0xFF;
    }
    const uint32_t n = kTileSize * kTileSize;
    return (acc[0] / n) | ((acc[1] / n) << 8) | ((acc[2] / n) << 16) | (0xFFu << 24);
}
