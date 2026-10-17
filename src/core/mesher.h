#pragma once
#include <cstdint>
#include <vector>

class World;

// 8 bytes por vértice (2x R8G8B8A8_UINT). Coordenadas locais ao chunk.
struct ChunkVertex {
    uint8_t x, y, z;
    uint8_t faceAo;  // bits 0-2: face, bits 3-4: AO (0 = mais escuro, 3 = sem oclusão)
    uint8_t layer;   // camada do texture array
    uint8_t u, v;    // 0 ou 1
    uint8_t pad;
};
static_assert(sizeof(ChunkVertex) == 8, "ChunkVertex deve ter 8 bytes");

// Gera a malha do chunk (cx, cz). Requer os 8 vizinhos carregados (World::hasAllNeighbors).
// Saída: 4 vértices por quad, na ordem do índice compartilhado (0,1,2, 2,3,0).
void buildChunkMesh(const World& world, int cx, int cz, std::vector<ChunkVertex>& out);
