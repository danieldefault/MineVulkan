// Testes do núcleo (sem GPU). Executar: ./build/core_tests
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "core/mesher.h"
#include "core/player.h"
#include "core/texture_gen.h"
#include "core/world.h"

static int g_failed = 0;
#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ++g_failed;                                                         \
        }                                                                       \
    } while (0)

static void generateAround(World& w, int r) {
    for (int z = -r; z <= r; ++z)
        for (int x = -r; x <= r; ++x) w.generateChunk(x, z);
}

static void testTerrain() {
    World a(1234), b(1234), c(999);
    CHECK(a.terrainHeight(10, -37) == b.terrainHeight(10, -37));
    bool anyDiff = false;
    for (int i = 0; i < 64; ++i) anyDiff |= a.terrainHeight(i * 13, i * 7) != c.terrainHeight(i * 13, i * 7);
    CHECK(anyDiff);
    generateAround(a, 1);
    int h = a.terrainHeight(5, 5);
    CHECK(a.getBlock(5, 0, 5) == BLOCK_BEDROCK);
    CHECK(blockSolid(a.getBlock(5, h, 5)));
    CHECK(a.getBlock(-3, h > 3 ? 1 : 2, -3) != BLOCK_AIR);
    CHECK(a.getBlock(5, CHUNK_HEIGHT - 1, 5) == BLOCK_AIR);
}

static void testMesher() {
    World w(1);
    generateAround(w, 1);
    // Esvazia o chunk central e coloca um bloco isolado: deve gerar 6 quads.
    for (int y = 0; y < CHUNK_HEIGHT; ++y)
        for (int z = 0; z < CHUNK_SIZE; ++z)
            for (int x = 0; x < CHUNK_SIZE; ++x) w.getChunk(0, 0)->set(x, y, z, BLOCK_AIR);
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dz) continue;
            Chunk* c = w.getChunk(dx, dz);
            for (int i = 0; i < CHUNK_VOLUME; ++i) c->blocks[i] = BLOCK_AIR;
        }
    w.setBlock(8, 64, 8, BLOCK_STONE);
    std::vector<ChunkVertex> v;
    buildChunkMesh(w, 0, 0, v);
    CHECK(v.size() == 24);
    for (const auto& vx : v) CHECK(((vx.faceAo >> 3) & 3) == 3);  // sem oclusão
    // Bloco vizinho: face compartilhada some (2 faces a menos) e AO aparece.
    w.setBlock(9, 64, 8, BLOCK_STONE);
    buildChunkMesh(w, 0, 0, v);
    CHECK(v.size() == 40);
    // Bloco na borda do chunk: vizinho (1,0) deve ser marcado sujo.
    w.getChunk(1, 0)->meshDirty = false;
    w.setBlock(15, 70, 3, BLOCK_DIRT);
    CHECK(w.getChunk(1, 0)->meshDirty);
    // Coordenadas negativas
    w.setBlock(-1, 64, -1, BLOCK_SAND);
    CHECK(w.getBlock(-1, 64, -1) == BLOCK_SAND);
    CHECK(w.getChunk(-1, -1)->get(15, 64, 15) == BLOCK_SAND);
}

static void testRaycast() {
    World w(7);
    generateAround(w, 1);
    int h = w.terrainHeight(4, 4);
    int top = h;
    while (blockSolid(w.getBlock(4, top + 1, 4))) ++top;  // pode ter árvore
    RayHit r = raycastBlocks(w, Vec3(4.5f, static_cast<float>(top) + 3.5f, 4.5f), Vec3(0, -1, 0), 8.0f);
    CHECK(r.hit);
    CHECK(r.x == 4 && r.z == 4 && r.y == top);
    CHECK(r.ny == 1 && r.nx == 0 && r.nz == 0);
    RayHit miss = raycastBlocks(w, Vec3(4.5f, 120.5f, 4.5f), Vec3(0, 1, 0), 6.0f);
    CHECK(!miss.hit);
}

static void testPlayerPhysics() {
    World w(42);
    generateAround(w, 2);
    Player p;
    int sy = w.findSpawnY(8, 8);
    p.pos = Vec3(8.5f, static_cast<float>(sy) + 5.0f, 8.5f);
    PlayerInput none;
    for (int i = 0; i < 240; ++i) p.step(w, none, 1.0f / 60.0f);
    CHECK(p.onGround);
    CHECK(p.pos.y >= static_cast<float>(sy) - 0.01f && p.pos.y < static_cast<float>(sy) + 0.1f);
    // Parede de 3 blocos à frente (-Z): não atravessa.
    int fy = static_cast<int>(std::floor(p.pos.y));
    for (int x = 6; x <= 10; ++x)
        for (int y = fy; y < fy + 3; ++y) w.setBlock(x, y, 6, BLOCK_STONE);
    PlayerInput fwd;
    fwd.moveZ = 1.0f;
    p.yaw = 0;
    for (int i = 0; i < 180; ++i) p.step(w, fwd, 1.0f / 60.0f);
    CHECK(p.pos.z > 7.0f);
    // Pulo
    p.pos = Vec3(8.5f, static_cast<float>(w.findSpawnY(8, 12)) + 0.5f, 12.5f);
    for (int i = 0; i < 30; ++i) p.step(w, none, 1.0f / 60.0f);
    float y0 = p.pos.y;
    PlayerInput j;
    j.jump = true;
    p.step(w, j, 1.0f / 60.0f);
    float maxY = p.pos.y;
    for (int i = 0; i < 60; ++i) { p.step(w, none, 1.0f / 60.0f); maxY = std::max(maxY, p.pos.y); }
    CHECK(maxY - y0 > 1.0f && maxY - y0 < 1.6f);
}

static void testTextures() {
    auto tex = generateBlockTextures();
    CHECK(tex.size() == static_cast<size_t>(TILE_COUNT) * 256);
    MipChain mc = buildMipChain(tex, TILE_COUNT);
    CHECK(mc.levels == 5);
    CHECK(mc.data[4].size() == static_cast<size_t>(TILE_COUNT));
}

static void testStreaming() {
    World w(3);
    for (int i = 0; i < 400; ++i) w.update(0, 0, 4, 8);
    CHECK(w.chunks().size() == 81);
    CHECK(w.hasAllNeighbors(0, 0));
    for (int i = 0; i < 400; ++i) w.update(16.0f * 20, 0, 4, 8);
    auto un = w.takeUnloaded();
    CHECK(!un.empty());
    CHECK(w.getChunk(0, 0) == nullptr);
    CHECK(w.chunks().size() <= 121);
}

int main() {
    testTerrain();
    testMesher();
    testRaycast();
    testPlayerPhysics();
    testTextures();
    testStreaming();
    if (g_failed) {
        std::printf("%d verificação(ões) falharam\n", g_failed);
        return 1;
    }
    std::printf("Todos os testes do núcleo passaram\n");
    return 0;
}
