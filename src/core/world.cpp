#include "world.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kMountainHeight = 86;  // acima disto: montanha (pedra exposta)
constexpr int kSnowLine = 98;        // acima disto: neve em qualquer bioma
constexpr int kCliffDrop = 3;        // desnível p/ algum vizinho a partir do qual a encosta é pedra
}  // namespace

World::World(uint32_t seed)
    : seed_(seed),
      continental_(seed * 31u + 1u),
      hills_(seed * 31u + 2u),
      mountains_(seed * 31u + 3u),
      temperature_(seed * 31u + 4u),
      humidity_(seed * 31u + 5u) {}

const char* biomeName(Biome b) {
    switch (b) {
        case Biome::Plains: return "planicie";
        case Biome::Forest: return "floresta";
        case Biome::Desert: return "deserto";
        case Biome::Tundra: return "tundra";
        case Biome::Mountains: return "montanha";
    }
    return "?";
}

int World::terrainHeight(int x, int z) const {
    float fx = static_cast<float>(x), fz = static_cast<float>(z);
    float c = continental_.fbm(fx * 0.0025f, fz * 0.0025f, 4);
    float h = hills_.fbm(fx * 0.012f, fz * 0.012f, 4);
    // O fBm fica quase sempre em [-0.2, 0.2]: começa a subir a partir do percentil ~70
    // (0.05) para que ~5% das colunas passem de kMountainHeight.
    float m = std::max(0.0f, (mountains_.fbm(fx * 0.005f, fz * 0.005f, 4) - 0.05f) / 0.16f);
    float height = 46.0f + c * 12.0f + h * 7.0f + m * m * 55.0f;
    // Comprime suavemente acima de 90 rumo ao teto (assíntota): picos continuam pontudos
    // em vez de virarem platôs cortados em CHUNK_HEIGHT - 12.
    const float knee = 90.0f, room = static_cast<float>(CHUNK_HEIGHT - 12) - knee;
    if (height > knee) height = knee + room * (1.0f - std::exp(-(height - knee) / room));
    return std::clamp(static_cast<int>(height), 4, CHUNK_HEIGHT - 12);
}

Biome World::biomeAt(int x, int z) const { return biomeAt(x, z, terrainHeight(x, z)); }

Biome World::biomeAt(int x, int z, int height) const {
    if (height > kMountainHeight) return Biome::Mountains;
    float fx = static_cast<float>(x) * 0.0016f, fz = static_cast<float>(z) * 0.0016f;
    // Limiares escolhidos pelos percentis do fBm (~[-0.17, 0.17] em 80% das colunas):
    // ~20% tundra, ~15% deserto, ~35% floresta, resto planície.
    float t = temperature_.fbm(fx, fz, 3);
    if (t < -0.10f) return Biome::Tundra;
    float h = humidity_.fbm(fx + 100.0f, fz, 3);
    if (t > 0.07f && h < 0.03f) return Biome::Desert;
    return h > 0.0f ? Biome::Forest : Biome::Plains;
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
    // Alturas com borda de 1 coluna: o desnível para os vizinhos define os penhascos.
    constexpr int HP = CHUNK_SIZE + 2;
    int heights[HP * HP];
    for (int z = 0; z < HP; ++z)
        for (int x = 0; x < HP; ++x)
            heights[z * HP + x] = terrainHeight(cx * CHUNK_SIZE + x - 1, cz * CHUNK_SIZE + z - 1);
    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            int wx = cx * CHUNK_SIZE + lx, wz = cz * CHUNK_SIZE + lz;
            const int* hp = &heights[(lz + 1) * HP + lx + 1];
            int h = *hp;
            int lowest = std::min(std::min(hp[-1], hp[1]), std::min(hp[-HP], hp[HP]));
            bool cliff = h - lowest >= kCliffDrop;
            Biome biome = biomeAt(wx, wz, h);
            uint8_t top = BLOCK_GRASS, sub = BLOCK_DIRT, deep = BLOCK_STONE;
            switch (biome) {
                case Biome::Desert: top = sub = BLOCK_SAND; deep = BLOCK_SANDSTONE; break;
                case Biome::Tundra: top = BLOCK_SNOW; break;
                case Biome::Mountains: top = h > kSnowLine ? BLOCK_SNOW_BLOCK : BLOCK_STONE; sub = BLOCK_STONE; break;
                default: break;
            }
            // Encosta íngreme: rocha exposta (sem degraus de grama/terra), exceto no deserto.
            if (cliff && biome != Biome::Desert) top = sub = BLOCK_STONE;
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                uint8_t b;
                if (y == 0) b = BLOCK_BEDROCK;
                else if (y < h - 8) b = BLOCK_STONE;
                else if (y < h - 4) b = deep;  // arenito sob a areia do deserto
                else if (y < h) b = sub;
                else if (y == h) b = top;
                else b = BLOCK_AIR;
                c.set(lx, y, lz, b);
            }
        }
    }
    placeVegetationInto(c);
    auto edits = edits_.find(chunkKey(cx, cz));
    if (edits != edits_.end())
        for (const auto& e : edits->second) c.blocks[e.first] = e.second;
    chunks_[chunkKey(cx, cz)] = std::move(chunk);
    // Vizinhos já existentes precisam remalhar a borda (faces/AO agora conhecidas).
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dz) markDirty(cx + dx, cz + dz);
}

// Vegetação é função pura da coluna: cada chunk insere as partes que caem dentro dele,
// inclusive copas de árvores com tronco no chunk vizinho. Não há escrita entre chunks.
void World::placeVegetationInto(Chunk& c) const {
    const int x0 = c.cx * CHUNK_SIZE, z0 = c.cz * CHUNK_SIZE;
    auto put = [&](int wx, int y, int wz, uint8_t b, bool onlyAir) {
        int lx = wx - x0, lz = wz - z0;
        if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE || y < 0 || y >= CHUNK_HEIGHT) return;
        if (onlyAir && c.get(lx, y, lz) != BLOCK_AIR) return;
        c.set(lx, y, lz, b);
    };
    // Copa redonda (carvalho): 2 camadas de raio 2 + 2 de raio 1, cantos aleatórios.
    auto oak = [&](int tx, int tz, int ground, uint32_t h) {
        int top = ground + 4 + static_cast<int>((h >> 8) % 3u);
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
    };
    // Pinheiro: tronco alto e copa cônica alternando raios 2/1, ponta no topo.
    auto spruce = [&](int tx, int tz, int ground, uint32_t h) {
        int top = ground + 6 + static_cast<int>((h >> 8) % 3u);
        put(tx, top + 1, tz, BLOCK_SPRUCE_LEAVES, true);
        for (int y = ground + 3; y <= top; ++y) {
            int r = (y >= top - 1) ? 1 : ((top - y) % 2 == 0 ? 2 : 1);
            for (int dz = -r; dz <= r; ++dz)
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::abs(dx) == r && std::abs(dz) == r) continue;  // sem cantos
                    put(tx + dx, y, tz + dz, BLOCK_SPRUCE_LEAVES, true);
                }
        }
        for (int y = ground + 1; y <= top; ++y) put(tx, y, tz, BLOCK_LOG, false);
    };
    for (int tz = z0 - 2; tz < z0 + CHUNK_SIZE + 2; ++tz) {
        for (int tx = x0 - 2; tx < x0 + CHUNK_SIZE + 2; ++tx) {
            uint32_t h = hash2i(tx, tz, seed_);
            if (h % 41u != 0) continue;  // filtro barato antes de calcular altura/bioma
            int ground = terrainHeight(tx, tz);
            int lowest = std::min(std::min(terrainHeight(tx - 1, tz), terrainHeight(tx + 1, tz)),
                                  std::min(terrainHeight(tx, tz - 1), terrainHeight(tx, tz + 1)));
            if (ground - lowest >= kCliffDrop) continue;  // nada cresce em rocha exposta
            uint32_t roll = (h >> 16) % 16u;  // densidade por bioma: chance = n/16 dos candidatos
            switch (biomeAt(tx, tz, ground)) {
                case Biome::Forest:
                    if (roll < 12) oak(tx, tz, ground, h);
                    break;
                case Biome::Plains:
                    if (roll < 2) oak(tx, tz, ground, h);
                    break;
                case Biome::Tundra:
                    if (roll < 6) spruce(tx, tz, ground, h);
                    break;
                case Biome::Desert:
                    if (roll < 3)  // cacto de 1 a 3 blocos
                        for (int y = ground + 1; y <= ground + 1 + static_cast<int>((h >> 8) % 3u); ++y)
                            put(tx, y, tz, BLOCK_CACTUS, true);
                    break;
                case Biome::Mountains: break;
            }
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
