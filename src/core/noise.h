#pragma once
// Ruído de Perlin 2D com seed + fBm. Determinístico, sem alocação por amostra.
#include <cstdint>

class Noise2D {
public:
    explicit Noise2D(uint32_t seed);
    float perlin(float x, float y) const;  // aprox. [-1, 1]
    float fbm(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;

private:
    uint8_t perm_[512];
};

// Hash inteiro rápido (para decisões por coluna: árvores, variações de textura).
inline uint32_t hash2i(int32_t x, int32_t z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 0x27d4eb2dU;
    h = (h ^ (h >> 15)) * 0x85ebca6bU;
    h ^= static_cast<uint32_t>(z) * 0x165667b1U;
    h = (h ^ (h >> 13)) * 0xc2b2ae35U;
    return h ^ (h >> 16);
}
