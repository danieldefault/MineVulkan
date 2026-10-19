#include "world.h"

#include <algorithm>
#include <cmath>

World::World(uint32_t seed)
    : seed_(seed),
      continental_(seed * 31u + 1u),
      hills_(seed * 31u + 2u),
      mountains_(seed * 31u + 3u),
      desert_(seed * 31u + 4u) {}

int World::terrainHeight(int x, int z) const {
    float fx = static_cast<float>(x), fz = static_cast<float>(z);
    float c = continental_.fbm(fx * 0.0025f, fz * 0.0025f, 4);
    float h = hills_.fbm(fx * 0.012f, fz * 0.012f, 4);
    float m = std::max(0.0f, mountains_.fbm(fx * 0.005f, fz * 0.005f, 4) * 1.8f - 0.15f);
    float height = 46.0f + c * 12.0f + h * 7.0f + m * m * 55.0f;
    return std::clamp(static_cast<int>(height), 4, CHUNK_HEIGHT - 12);
}

bool World::isDesert(int x, int z) const {
    return desert_.fbm(static_cast<float>(x) * 0.003f, static_cast<float>(z) * 0.003f, 3) > 0.28f;
}

Chunk* World::getChunk(int cx, int cz) {
    auto it = chunks_.find(chunkKey(cx, cz));
    return it == chunks_.end() ? nullptr : it->second.get();
}
const Chunk* World::getChunk(int cx, int cz) const {
    auto it = chunks_.find(chunkKey(cx, cz));
    return it == chunks_.end() ? nullptr : it->second.get();
}

uint8_t World::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return BLOCK_AIR;
    const Chunk* c = getChunk(floorDiv(x, CHUNK_SIZE), floorDiv(z, CHUNK_SIZE));
    if (!c) return BLOCK_AIR;
    return c->get(x - c->cx * CHUNK_SIZE, y, z - c->cz * CHUNK_SIZE);
}

bool World::isSolidForPhysics(int x, int y, int z) const {
    if (y < 0) return true;
    if (y >= CHUNK_HEIGHT) return false;
    const Chunk* c = getChunk(floorDiv(x, CHUNK_SIZE), floorDiv(z, CHUNK_SIZE));
    if (!c) return true;
    return blockSolid(c->get(x - c->cx * CHUNK_SIZE, y, z - c->cz * CHUNK_SIZE));
}

void World::markDirty(int cx, int cz) {
    if (Chunk* c = getChunk(cx, cz)) c->meshDirty = true;
}

bool World::setBlock(int x, int y, int z, uint8_t b) {
    if (y < 0 || y >= CHUNK_HEIGHT) return false;
    int cx = floorDiv(x, CHUNK_SIZE), cz = floorDiv(z, CHUNK_SIZE);
    Chunk* c = getChunk(cx, cz);
    if (!c) return false;
    int lx = x - cx * CHUNK_SIZE, lz = z - cz * CHUNK_SIZE;
    c->set(lx, y, lz, b);
    c->meshDirty = true;
    edits_[chunkKey(cx, cz)][Chunk::index(lx, y, lz)] = b;
    // Vizinhos que enxergam este bloco (faces e AO, incluindo diagonais).
    int dx = (lx == 0) ? -1 : (lx == CHUNK_SIZE - 1 ? 1 : 0);
    int dz = (lz == 0) ? -1 : (lz == CHUNK_SIZE - 1 ? 1 : 0);
    if (dx) markDirty(cx + dx, cz);
    if (dz) markDirty(cx, cz + dz);
    if (dx && dz) markDirty(cx + dx, cz + dz);
    return true;
}

bool World::hasAllNeighbors(int cx, int cz) const {
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            if (!getChunk(cx + dx, cz + dz)) return false;
    return true;
}

void World::generateChunk(int cx, int cz) {
    auto chunk = std::make_unique<Chunk>();
    chunk->cx = cx;
    chunk->cz = cz;
    Chunk& c = *chunk;
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            int wx = cx * CHUNK_SIZE + lx, wz = cz * CHUNK_SIZE + lz;
            int h = terrainHeight(wx, wz);
            bool desert = isDesert(wx, wz);
            uint8_t top = desert ? BLOCK_SAND : (h > 86 ? BLOCK_STONE : BLOCK_GRASS);
            uint8_t sub = desert ? BLOCK_SAND : (h > 86 ? BLOCK_STONE : BLOCK_DIRT);
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                uint8_t b;
                if (y == 0) b = BLOCK_BEDROCK;
                else if (y < h - 4) b = BLOCK_STONE;
                else if (y < h) b = sub;
                else if (y == h) b = top;
                else b = BLOCK_AIR;
                c.set(lx, y, lz, b);
            }
        }
    }
    placeTreesInto(c);
    auto edits = edits_.find(chunkKey(cx, cz));
    if (edits != edits_.end())
        for (const auto& e : edits->second) c.blocks[e.first] = e.second;
    chunks_[chunkKey(cx, cz)] = std::move(chunk);
    // Vizinhos já existentes precisam remalhar a borda (faces/AO agora conhecidas).
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dz) markDirty(cx + dx, cz + dz);
}

// Árvores são função pura da coluna: cada chunk insere as partes que caem dentro dele,
// inclusive copas de árvores com tronco no chunk vizinho. Não há escrita entre chunks.
void World::placeTreesInto(Chunk& c) const {
    const int x0 = c.cx * CHUNK_SIZE, z0 = c.cz * CHUNK_SIZE;
    auto put = [&](int wx, int y, int wz, uint8_t b, bool onlyAir) {
        int lx = wx - x0, lz = wz - z0;
        if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT) return;
        if (onlyAir && c.get(lx, y, lz) != BLOCK_AIR) return;
        c.set(lx, y, lz, b);
    };
    for (int tz = z0 - 2; tz < z0 + CHUNK_SIZE + 2; ++tz) {
        for (int tx = x0 - 2; tx < x0 + CHUNK_SIZE + 2; ++tx) {
            uint32_t h = hash2i(tx, tz, seed_);
            if (h % 151u != 0) continue;
            if (isDesert(tx, tz)) continue;
            int ground = terrainHeight(tx, tz);
            if (ground > 84) continue;
            int trunk = 4 + static_cast<int>((h >> 8) % 3u);
            int top = ground + trunk;
            for (int y = top - 2; y <= top + 1; ++y) {
                int r = (y >= top) ? 1 : 2;
                for (int dz = -r; dz <= r; ++dz)
                    for (int dx = -r; dx <= r; ++dx) {
                        bool corner = (std::abs(dx) == r && std::abs(dz) == r);
                        if (corner && (r == 1 || (hash2i(tx * 5 + dx, tz * 5 + dz + y, seed_ ^ 0x5bd1e995u) & 1u)))
                            continue;
                        put(tx + dx, y, tz + dz, BLOCK_LEAVES, true);
                    }
            }
            for (int y = ground + 1; y <= top; ++y) put(tx, y, tz, BLOCK_LOG, false);
        }
    }
}

void World::update(float px, float pz, int radius, int genBudget) {
    int pcx = floorDiv(static_cast<int>(std::floor(px)), CHUNK_SIZE);
    int pcz = floorDiv(static_cast<int>(std::floor(pz)), CHUNK_SIZE);

    // Anéis concêntricos: o mais próximo primeiro.
    for (int ring = 0; ring <= radius + 1 && genBudget > 0; ++ring) {
        for (int dz = -ring; dz <= ring && genBudget > 0; ++dz) {
            for (int dx = -ring; dx <= ring && genBudget > 0; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                if (!getChunk(pcx + dx, pcz + dz)) {
                    generateChunk(pcx + dx, pcz + dz);
                    --genBudget;
                }
            }
        }
    }

    for (auto it = chunks_.begin(); it != chunks_.end();) {
        const Chunk& c = *it->second;
        if (std::max(std::abs(c.cx - pcx), std::abs(c.cz - pcz)) > radius + 2) {
            unloaded_.push_back(it->first);
            it = chunks_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<int64_t> World::takeUnloaded() {
    std::vector<int64_t> out;
    out.swap(unloaded_);
    return out;
}

int World::findSpawnY(int x, int z) const {
    for (int y = terrainHeight(x, z) + 1; y < CHUNK_HEIGHT - 2; ++y)
        if (!blockSolid(getBlock(x, y, z)) && !blockSolid(getBlock(x, y + 1, z))) return y;
    return CHUNK_HEIGHT - 2;
}
