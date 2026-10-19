#pragma once
#include <cstdint>

enum Block : uint8_t {
    BLOCK_AIR = 0,
    BLOCK_GRASS,
    BLOCK_DIRT,
    BLOCK_STONE,
    BLOCK_SAND,
    BLOCK_LOG,
    BLOCK_LEAVES,
    BLOCK_PLANKS,
    BLOCK_COBBLE,
    BLOCK_BEDROCK,
    BLOCK_SNOW,           // terra com neve por cima (tundra)
    BLOCK_SNOW_BLOCK,     // neve compacta (picos acima da linha de neve)
    BLOCK_SANDSTONE,
    BLOCK_CACTUS,
    BLOCK_SPRUCE_LEAVES,
    BLOCK_WATER,          // translúcida, não sólida; desenhada num passe separado
    BLOCK_COUNT
};

// Camadas do texture array (uma textura 16x16 por camada).
enum Tile : uint8_t {
    TILE_GRASS_TOP = 0,
    TILE_GRASS_SIDE,
    TILE_DIRT,
    TILE_STONE,
    TILE_SAND,
    TILE_LOG_SIDE,
    TILE_LOG_TOP,
    TILE_LEAVES,
    TILE_PLANKS,
    TILE_COBBLE,
    TILE_BEDROCK,
    TILE_SNOW,
    TILE_SNOW_SIDE,
    TILE_SANDSTONE,
    TILE_CACTUS_SIDE,
    TILE_CACTUS_TOP,
    TILE_SPRUCE_LEAVES,
    TILE_WATER,
    TILE_COUNT
};

// Faces: 0:+X 1:-X 2:+Y 3:-Y 4:+Z 5:-Z
inline uint8_t blockTile(uint8_t block, int face) {
    switch (block) {
        case BLOCK_GRASS: return face == 2 ? TILE_GRASS_TOP : (face == 3 ? TILE_DIRT : TILE_GRASS_SIDE);
        case BLOCK_DIRT: return TILE_DIRT;
        case BLOCK_STONE: return TILE_STONE;
        case BLOCK_SAND: return TILE_SAND;
        case BLOCK_LOG: return (face == 2 || face == 3) ? TILE_LOG_TOP : TILE_LOG_SIDE;
        case BLOCK_LEAVES: return TILE_LEAVES;
        case BLOCK_PLANKS: return TILE_PLANKS;
        case BLOCK_COBBLE: return TILE_COBBLE;
        case BLOCK_BEDROCK: return TILE_BEDROCK;
        case BLOCK_SNOW: return face == 2 ? TILE_SNOW : (face == 3 ? TILE_DIRT : TILE_SNOW_SIDE);
        case BLOCK_SNOW_BLOCK: return TILE_SNOW;
        case BLOCK_SANDSTONE: return TILE_SANDSTONE;
        case BLOCK_CACTUS: return (face == 2 || face == 3) ? TILE_CACTUS_TOP : TILE_CACTUS_SIDE;
        case BLOCK_SPRUCE_LEAVES: return TILE_SPRUCE_LEAVES;
        case BLOCK_WATER: return TILE_WATER;
        default: return TILE_STONE;
    }
}

inline bool blockSolid(uint8_t b) { return b != BLOCK_AIR && b != BLOCK_WATER; }
inline bool blockOpaque(uint8_t b) { return b != BLOCK_AIR && b != BLOCK_WATER; }
inline bool blockBreakable(uint8_t b) { return blockSolid(b) && b != BLOCK_BEDROCK; }
inline bool blockReplaceable(uint8_t b) { return !blockSolid(b); }  // dá para colocar bloco aqui

// Blocos disponíveis na hotbar (desktop: teclas 1-9 e 0).
constexpr uint8_t kHotbar[] = {BLOCK_GRASS, BLOCK_DIRT,   BLOCK_STONE, BLOCK_COBBLE,    BLOCK_PLANKS,
                               BLOCK_LOG,   BLOCK_LEAVES, BLOCK_SAND,  BLOCK_SANDSTONE, BLOCK_SNOW_BLOCK};
constexpr int kHotbarSize = static_cast<int>(sizeof(kHotbar));
