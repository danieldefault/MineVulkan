#include "mesher.h"

#include <vector>

#include "world.h"

namespace {

constexpr int PX = CHUNK_SIZE + 2;    // 18
constexpr int PZ = CHUNK_SIZE + 2;    // 18
constexpr int PY = CHUNK_HEIGHT + 2;  // 130

inline int pidx(int x, int y, int z) { return ((y + 1) * PZ + (z + 1)) * PX + (x + 1); }

struct FaceDef {
    int n[3];
    int c[4][3];
    uint8_t uv[4][2];
};

// Cantos em ordem anti-horária vista de fora (normal = (p1-p0) x (p2-p0)).
constexpr FaceDef kFaces[6] = {
    {{1, 0, 0}, {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, {{1, 1}, {1, 0}, {0, 0}, {0, 1}}},
    {{-1, 0, 0}, {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}}, {{1, 1}, {1, 0}, {0, 0}, {0, 1}}},
    {{0, 1, 0}, {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, {{0, 0}, {0, 1}, {1, 1}, {1, 0}}},
    {{0, -1, 0}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
    {{0, 0, 1}, {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}, {{1, 1}, {1, 0}, {0, 0}, {0, 1}}},
    {{0, 0, -1}, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}, {{1, 1}, {1, 0}, {0, 0}, {0, 1}}},
};

}  // namespace

uint32_t buildChunkMesh(const World& world, int cx, int cz, std::vector<ChunkVertex>& out) {
    out.clear();
    static thread_local std::vector<ChunkVertex> water;
    water.clear();
    // Volume com borda de 1 bloco copiado dos 9 chunks (evita lookups em hash por bloco).
    static thread_local std::vector<uint8_t> pad;
    pad.assign(static_cast<size_t>(PX) * PY * PZ, BLOCK_AIR);

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const Chunk* c = world.getChunk(cx + dx, cz + dz);
            if (!c) continue;
            int x0 = dx < 0 ? CHUNK_SIZE - 1 : 0, x1 = dx > 0 ? 1 : CHUNK_SIZE;
            int z0 = dz < 0 ? CHUNK_SIZE - 1 : 0, z1 = dz > 0 ? 1 : CHUNK_SIZE;
            for (int y = 0; y < CHUNK_HEIGHT; ++y)
                for (int z = z0; z < z1; ++z)
                    for (int x = x0; x < x1; ++x)
                        pad[pidx(x + dx * CHUNK_SIZE, y, z + dz * CHUNK_SIZE)] = c->get(x, y, z);
        }
    }
    // Abaixo de y=0 é sólido: não desenha a face de baixo do bedrock.
    for (int z = -1; z <= CHUNK_SIZE; ++z)
        for (int x = -1; x <= CHUNK_SIZE; ++x) pad[pidx(x, -1, z)] = BLOCK_BEDROCK;

    auto opaqueAt = [&](int x, int y, int z) -> int { return blockOpaque(pad[pidx(x, y, z)]) ? 1 : 0; };

    const Chunk* self = world.getChunk(cx, cz);
    if (!self) return 0;

    for (int y = 0; y < CHUNK_HEIGHT; ++y) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                uint8_t b = pad[pidx(x, y, z)];
                if (b == BLOCK_AIR) continue;
                const bool isWater = b == BLOCK_WATER;
                // Superfície da água: rebaixa o topo se não houver água em cima.
                const bool waterTop = isWater && pad[pidx(x, y + 1, z)] != BLOCK_WATER;
                for (int f = 0; f < 6; ++f) {
                    const FaceDef& fd = kFaces[f];
                    int nx = x + fd.n[0], ny = y + fd.n[1], nz = z + fd.n[2];
                    if (opaqueAt(nx, ny, nz)) continue;
                    const bool facesWater = pad[pidx(nx, ny, nz)] == BLOCK_WATER;
                    if (isWater && facesWater) continue;

                    int normalAxis = fd.n[0] != 0 ? 0 : (fd.n[1] != 0 ? 1 : 2);
                    int t1 = (normalAxis + 1) % 3, t2 = (normalAxis + 2) % 3;
                    uint8_t tile = blockTile(b, f);
                    int ao[4];
                    ChunkVertex v[4];
                    for (int k = 0; k < 4; ++k) {
                        if (isWater) {
                            ao[k] = 3;  // água não recebe oclusão
                        } else {
                            int d1[3] = {0, 0, 0}, d2[3] = {0, 0, 0};
                            d1[t1] = fd.c[k][t1] ? 1 : -1;
                            d2[t2] = fd.c[k][t2] ? 1 : -1;
                            int s1 = opaqueAt(nx + d1[0], ny + d1[1], nz + d1[2]);
                            int s2 = opaqueAt(nx + d2[0], ny + d2[1], nz + d2[2]);
                            int cc = opaqueAt(nx + d1[0] + d2[0], ny + d1[1] + d2[1], nz + d1[2] + d2[2]);
                            ao[k] = (s1 && s2) ? 0 : 3 - (s1 + s2 + cc);
                        }
                        v[k].x = static_cast<uint8_t>(x + fd.c[k][0]);
                        v[k].y = static_cast<uint8_t>(y + fd.c[k][1]);
                        v[k].z = static_cast<uint8_t>(z + fd.c[k][2]);
                        v[k].faceAo = static_cast<uint8_t>(f | (ao[k] << 3));
                        v[k].layer = tile;
                        v[k].u = fd.uv[k][0];
                        v[k].v = fd.uv[k][1];
                        v[k].flags = static_cast<uint8_t>(((waterTop && fd.c[k][1]) ? 1 : 0) |
                                                          ((!isWater && facesWater) ? 2 : 0));
                    }
                    std::vector<ChunkVertex>& dst = isWater ? water : out;
                    // Troca a diagonal do quad para AO sem anisotropia.
                    if (ao[0] + ao[2] < ao[1] + ao[3]) {
                        dst.push_back(v[1]); dst.push_back(v[2]); dst.push_back(v[3]); dst.push_back(v[0]);
                    } else {
                        dst.push_back(v[0]); dst.push_back(v[1]); dst.push_back(v[2]); dst.push_back(v[3]);
                    }
                }
            }
        }
    }
    const uint32_t opaqueQuads = static_cast<uint32_t>(out.size() / 4);
    out.insert(out.end(), water.begin(), water.end());
    return opaqueQuads;
}
