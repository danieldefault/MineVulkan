#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "block.h"
#include "noise.h"

constexpr int CHUNK_SIZE = 16;     // X e Z
constexpr int CHUNK_HEIGHT = 128;  // Y
constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_HEIGHT;
constexpr int SEA_LEVEL = 44;  // colunas abaixo disto ficam cobertas de água até aqui

struct Chunk {
    int cx = 0, cz = 0;
    bool meshDirty = true;  // precisa (re)gerar a malha
    uint8_t blocks[CHUNK_VOLUME];

    static int index(int x, int y, int z) { return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x; }
    uint8_t get(int x, int y, int z) const { return blocks[index(x, y, z)]; }
    void set(int x, int y, int z, uint8_t b) { blocks[index(x, y, z)] = b; }
};

enum class Biome : uint8_t { Plains, Forest, Desert, Tundra, Mountains, Ocean };
const char* biomeName(Biome b);

inline int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline int64_t chunkKey(int cx, int cz) {
    return (static_cast<int64_t>(cx) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(cz));
}

class World {
public:
    explicit World(uint32_t seed);

    uint8_t getBlock(int x, int y, int z) const;  // chunk ausente => AR
    bool isSolidForPhysics(int x, int y, int z) const;  // chunk ausente => sólido (não cai no vazio)
    bool setBlock(int x, int y, int z, uint8_t b);
    // Remove o bloco; se houver água ao lado ou em cima, o espaço vira água (sem simular
    // escoamento: só a célula aberta é preenchida).
    bool breakBlock(int x, int y, int z);

    Chunk* getChunk(int cx, int cz);
    const Chunk* getChunk(int cx, int cz) const;
    bool hasAllNeighbors(int cx, int cz) const;  // 8 vizinhos gerados (necessário para AO na borda)

    // Gera até `genBudget` chunks faltantes dentro de radius + 1 (mais próximos primeiro)
    // e descarrega os que passaram de radius + 2. O anel extra só existe como dado:
    // a malha exige os 8 vizinhos, então assim os chunks até `radius` podem ser desenhados.
    void update(float px, float pz, int radius, int genBudget);
    void generateChunk(int cx, int cz);

    // Chunks descarregados desde a última chamada (para liberar recursos de GPU).
    std::vector<int64_t> takeUnloaded();

    int terrainHeight(int x, int z) const;
    // Bioma da coluna: temperatura e umidade (ruídos de baixa frequência) + altitude.
    Biome biomeAt(int x, int z) const;
    Biome biomeAt(int x, int z, int height) const;  // evita recalcular a altura
    int findSpawnY(int x, int z) const;
    // Coluna de terra firme (acima do nível do mar) mais próxima de (x, z), em espiral.
    bool findLand(int& x, int& z, int maxRadius) const;

    const std::unordered_map<int64_t, std::unique_ptr<Chunk>>& chunks() const { return chunks_; }

private:
    void placeVegetationInto(Chunk& c) const;
    void markDirty(int cx, int cz);

    uint32_t seed_;
    Noise2D continental_, hills_, mountains_, temperature_, humidity_;
    std::unordered_map<int64_t, std::unique_ptr<Chunk>> chunks_;
    std::vector<int64_t> unloaded_;
    // Blocos alterados pelo jogador (chunk -> índice -> bloco), reaplicados quando o
    // chunk é gerado de novo. Sem isso, edições somem ao se afastar e voltar.
    std::unordered_map<int64_t, std::unordered_map<int, uint8_t>> edits_;
};
