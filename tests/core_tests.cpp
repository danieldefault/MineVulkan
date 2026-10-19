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
    CHECK(w.chunks().size() == 121);  // raio 4 + anel extra de dados
    // Todo chunk dentro do raio pode ser malhado (inclusive a borda).
    for (int z = -4; z <= 4; ++z)
        for (int x = -4; x <= 4; ++x) CHECK(w.hasAllNeighbors(x, z));
    for (int i = 0; i < 400; ++i) w.update(16.0f * 20, 0, 4, 8);
    auto un = w.takeUnloaded();
    CHECK(!un.empty());
    CHECK(w.getChunk(0, 0) == nullptr);
    CHECK(w.chunks().size() <= 169);
}

static void testEditsSurviveUnload() {
    World w(5);
    for (int i = 0; i < 400; ++i) w.update(0, 0, 2, 8);
    int h = w.terrainHeight(3, 3);
    w.setBlock(3, h + 5, 3, BLOCK_PLANKS);
    w.setBlock(3, h, 3, BLOCK_AIR);
    for (int i = 0; i < 400; ++i) w.update(16.0f * 30, 0, 2, 8);
    CHECK(w.getChunk(0, 0) == nullptr);
    for (int i = 0; i < 400; ++i) w.update(0, 0, 2, 8);
    CHECK(w.getChunk(0, 0) != nullptr);
    CHECK(w.getBlock(3, h + 5, 3) == BLOCK_PLANKS);
    CHECK(w.getBlock(3, h, 3) == BLOCK_AIR);
}

// Coluna (x, z) de cada bioma perto da origem (varredura em grade).
static bool findBiome(const World& w, Biome b, int& ox, int& oz) {
    for (int r = 0; r < 4000; r += 8)
        for (int z = -r; z <= r; z += 8)
            for (int x = -r; x <= r; x += 8) {
                if (std::max(std::abs(x), std::abs(z)) != r) continue;
                if (w.biomeAt(x, z) == b) { ox = x; oz = z; return true; }
            }
    return false;
}

static bool biomeNear(const World& w, int x, int z, Biome b, int r) {
    for (int dz = -r; dz <= r; ++dz)
        for (int dx = -r; dx <= r; ++dx)
            if (w.biomeAt(x + dx, z + dz) == b) return true;
    return false;
}

static int surfaceY(const World& w, int x, int z) {
    int y = CHUNK_HEIGHT - 1;
    while (y > 0 && w.getBlock(x, y, z) == BLOCK_AIR) --y;
    return y;
}

static void testBiomes() {
    World w(1337);
    CHECK(World(1337).biomeAt(300, -700) == w.biomeAt(300, -700));  // determinístico
    const Biome all[] = {Biome::Plains, Biome::Forest, Biome::Desert, Biome::Tundra, Biome::Mountains};
    for (Biome b : all) {
        int x = 0, z = 0;
        bool found = findBiome(w, b, x, z);
        CHECK(found);
        if (!found) continue;
        int cx = floorDiv(x, CHUNK_SIZE), cz = floorDiv(z, CHUNK_SIZE);
        if (!w.getChunk(cx, cz)) w.generateChunk(cx, cz);
        int h = w.terrainHeight(x, z);
        uint8_t top = w.getBlock(x, h, z);
        switch (b) {
            case Biome::Desert:
                CHECK(top == BLOCK_SAND);
                CHECK(w.getBlock(x, h - 6, z) == BLOCK_SANDSTONE);
                break;
            case Biome::Tundra: CHECK(top == BLOCK_SNOW || top == BLOCK_STONE); break;
            case Biome::Mountains: CHECK(top == BLOCK_STONE || top == BLOCK_SNOW_BLOCK); break;
            default: CHECK(top == BLOCK_GRASS || top == BLOCK_STONE); break;
        }
        CHECK(h <= CHUNK_HEIGHT - 12);
    }
}

// Vegetação conforme o bioma: cacto só no deserto, pinheiro só na tundra,
// floresta bem mais densa que planície.
static void testVegetation() {
    World w(1337);
    for (int z = -12; z <= 12; ++z)
        for (int x = -12; x <= 12; ++x) w.generateChunk(x, z);
    int forestCols = 0, forestTrees = 0, plainsCols = 0, plainsTrees = 0;
    for (int z = -12 * CHUNK_SIZE; z < 13 * CHUNK_SIZE; ++z)
        for (int x = -12 * CHUNK_SIZE; x < 13 * CHUNK_SIZE; ++x) {
            Biome b = w.biomeAt(x, z);
            int h = w.terrainHeight(x, z);
            uint8_t above = w.getBlock(x, h + 1, z);
            if (above == BLOCK_CACTUS) CHECK(b == Biome::Desert);
            const bool land = h > SEA_LEVEL + 1;  // praia e água não têm árvores
            if (land && b == Biome::Forest) { ++forestCols; forestTrees += above == BLOCK_LOG; }
            if (land && b == Biome::Plains) { ++plainsCols; plainsTrees += above == BLOCK_LOG; }
            int top = surfaceY(w, x, z);
            // A copa pode cair até 2 colunas fora do tronco (e do bioma dele).
            if (w.getBlock(x, top, z) == BLOCK_SPRUCE_LEAVES) CHECK(biomeNear(w, x, z, Biome::Tundra, 2));
        }
    CHECK(forestCols > 0 && plainsCols > 0);
    if (forestCols > 0 && plainsCols > 0) {
        double fd = static_cast<double>(forestTrees) / forestCols, pd = static_cast<double>(plainsTrees) / plainsCols;
        CHECK(fd > pd * 3.0);
    }
}

static void testWater() {
    World w(1337);
    int ox = 0, oz = 0;
    CHECK(findBiome(w, Biome::Ocean, ox, oz));
    int ocx = floorDiv(ox, CHUNK_SIZE), ocz = floorDiv(oz, CHUNK_SIZE);
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) w.generateChunk(ocx + dx, ocz + dz);
    int h = w.terrainHeight(ox, oz);
    CHECK(h < SEA_LEVEL - 2);
    CHECK(w.getBlock(ox, h, oz) == BLOCK_SAND || w.getBlock(ox, h, oz) == BLOCK_STONE);
    CHECK(w.getBlock(ox, h + 1, oz) == BLOCK_WATER);
    CHECK(w.getBlock(ox, SEA_LEVEL, oz) == BLOCK_WATER);
    CHECK(w.getBlock(ox, SEA_LEVEL + 1, oz) == BLOCK_AIR);
    CHECK(!blockSolid(BLOCK_WATER) && !blockOpaque(BLOCK_WATER) && !blockBreakable(BLOCK_WATER));

    // Raycast atravessa a água e acerta o fundo.
    RayHit r = raycastBlocks(w, Vec3(ox + 0.5f, SEA_LEVEL + 0.5f, oz + 0.5f), Vec3(0, -1, 0), 64.0f);
    CHECK(r.hit && r.y == h);

    // Malha: quads de água vêm depois dos opacos, só na superfície/bordas, e o topo é rebaixado.
    std::vector<ChunkVertex> v;
    uint32_t opaque = buildChunkMesh(w, ocx, ocz, v);
    CHECK(opaque * 4 <= v.size());
    size_t waterQuads = v.size() / 4 - opaque;
    CHECK(waterQuads > 0);
    for (size_t i = 0; i < v.size(); ++i) {
        bool isWater = v[i].layer == TILE_WATER;
        CHECK(isWater == (i >= opaque * 4));
        if (isWater && (v[i].faceAo & 7) == 2) CHECK(v[i].flags == 1 && v[i].y == SEA_LEVEL + 1);
        if (!isWater) CHECK((v[i].flags & 1) == 0);
    }
    // O fundo do mar (face de cima do bloco do fundo) é marcado como submerso.
    bool seabedFlagged = false;
    for (size_t i = 0; i < opaque * 4; ++i) seabedFlagged |= (v[i].flags & 2) && (v[i].faceAo & 7) == 2;
    CHECK(seabedFlagged);
    // Sem faces água-água internas: no máximo ~1 quad de superfície por coluna + bordas.
    CHECK(waterQuads < static_cast<size_t>(CHUNK_SIZE * CHUNK_SIZE * 3));

    // Quebrar bloco ao lado da água enche o buraco; longe da água fica ar.
    w.setBlock(ox, SEA_LEVEL + 1, oz, BLOCK_STONE);
    w.setBlock(ox + 1, SEA_LEVEL + 1, oz, BLOCK_STONE);
    w.breakBlock(ox, h, oz);
    CHECK(w.getBlock(ox, h, oz) == BLOCK_WATER);
    w.breakBlock(ox + 1, SEA_LEVEL + 1, oz);  // vizinho de cima é ar; dos lados, pedra/ar
    CHECK(w.getBlock(ox + 1, SEA_LEVEL + 1, oz) == BLOCK_AIR);
}

static void testSwimming() {
    World w(1337);
    int ox = 0, oz = 0;
    CHECK(findBiome(w, Biome::Ocean, ox, oz));
    int ocx = floorDiv(ox, CHUNK_SIZE), ocz = floorDiv(oz, CHUNK_SIZE);
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            w.generateChunk(ocx + dx, ocz + dz);
            Chunk* c = w.getChunk(ocx + dx, ocz + dz);
            // Piscina controlada: fundo em 30, água até o nível do mar.
            for (int z = 0; z < CHUNK_SIZE; ++z)
                for (int x = 0; x < CHUNK_SIZE; ++x)
                    for (int y = 1; y < CHUNK_HEIGHT; ++y)
                        c->set(x, y, z, y <= 30 ? BLOCK_STONE : (y <= SEA_LEVEL ? BLOCK_WATER : BLOCK_AIR));
        }
    const float x0 = ocx * CHUNK_SIZE + 8.5f, z0 = ocz * CHUNK_SIZE + 8.5f;
    Player p;
    p.pos = Vec3(x0, 40.0f, z0);
    PlayerInput none;
    float minVy = 0;
    for (int i = 0; i < 60; ++i) { p.step(w, none, 1.0f / 60.0f); minVy = std::min(minVy, p.vel.y); }
    CHECK(p.inWater);
    CHECK(minVy >= -Player::kSinkSpeed - 0.01f);  // afunda devagar, sem queda livre
    PlayerInput up;
    up.jump = true;
    float y0 = p.pos.y;
    for (int i = 0; i < 60; ++i) p.step(w, up, 1.0f / 60.0f);
    CHECK(p.pos.y > y0 + 1.0f);  // nadar sobe

    // Margem: degrau de 1 bloco acima da água à frente (-Z). Nadando contra ela, sai da água.
    int bz = static_cast<int>(std::floor(z0)) - 3;
    for (int z = bz - 14; z <= bz; ++z)  // margem longa: em 4 s andando não chega ao fim
        for (int x = static_cast<int>(x0) - 3; x <= static_cast<int>(x0) + 3; ++x)
            for (int y = 31; y <= SEA_LEVEL + 1; ++y) w.setBlock(x, y, z, BLOCK_STONE);
    p.pos = Vec3(x0, SEA_LEVEL - 0.6f, z0);
    p.vel = Vec3();
    p.yaw = 0;
    PlayerInput fwd;
    fwd.moveZ = 1.0f;
    fwd.jump = true;
    for (int i = 0; i < 240; ++i) p.step(w, fwd, 1.0f / 60.0f);
    CHECK(p.pos.z < static_cast<float>(bz) + 0.5f);  // subiu na margem
    CHECK(p.pos.y >= SEA_LEVEL + 2 - 0.01f);
    CHECK(!p.inWater);
}

static void testFindLand() {
    World w(1337);
    int ox = 0, oz = 0;
    CHECK(findBiome(w, Biome::Ocean, ox, oz));
    int x = ox, z = oz;
    CHECK(w.findLand(x, z, 2048));
    CHECK(w.terrainHeight(x, z) > SEA_LEVEL + 1);
}

int main() {
    testTerrain();
    testMesher();
    testRaycast();
    testPlayerPhysics();
    testTextures();
    testStreaming();
    testEditsSurviveUnload();
    testBiomes();
    testVegetation();
    testWater();
    testSwimming();
    testFindLand();
    if (g_failed) {
        std::printf("%d verificação(ões) falharam\n", g_failed);
        return 1;
    }
    std::printf("Todos os testes do núcleo passaram\n");
    return 0;
}
