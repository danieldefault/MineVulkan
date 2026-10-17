#pragma once
#include <cstdint>
#include <vector>

constexpr int kTileSize = 16;

// Gera as texturas dos blocos proceduralmente (sem arquivos de imagem).
// Retorna TILE_COUNT camadas de 16x16 pixels RGBA8 (bytes R,G,B,A na memória).
std::vector<uint32_t> generateBlockTextures();

// Mipmaps por box filter: retorna os níveis em sequência (nível 0 incluso), por camada.
// levels = log2(kTileSize) + 1.
struct MipChain {
    int levels = 0;
    std::vector<std::vector<uint32_t>> data;  // data[level] = todas as camadas daquele nível
};
MipChain buildMipChain(const std::vector<uint32_t>& level0, int layers);

// Cor média de um bloco (face lateral) para a hotbar.
uint32_t blockAverageColor(const std::vector<uint32_t>& textures, uint8_t block);
