#include "noise.h"

#include <cmath>

Noise2D::Noise2D(uint32_t seed) {
    uint8_t p[256];
    for (int i = 0; i < 256; ++i) p[i] = static_cast<uint8_t>(i);
    // Fisher-Yates com xorshift32
    uint32_t s = seed ? seed : 0x9E3779B9u;
    for (int i = 255; i > 0; --i) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        int j = static_cast<int>(s % static_cast<uint32_t>(i + 1));
        uint8_t t = p[i]; p[i] = p[j]; p[j] = t;
    }
    for (int i = 0; i < 512; ++i) perm_[i] = p[i & 255];
}

static inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float grad(uint8_t h, float x, float y) {
    switch (h & 7) {
        case 0: return x + y;
        case 1: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x;
        case 5: return -x;
        case 6: return y;
        default: return -y;
    }
}

float Noise2D::perlin(float x, float y) const {
    float fx = std::floor(x), fy = std::floor(y);
    int xi = static_cast<int>(fx) & 255, yi = static_cast<int>(fy) & 255;
    float xf = x - fx, yf = y - fy;
    float u = fade(xf), v = fade(yf);
    uint8_t aa = perm_[perm_[xi] + yi], ab = perm_[perm_[xi] + yi + 1];
    uint8_t ba = perm_[perm_[xi + 1] + yi], bb = perm_[perm_[xi + 1] + yi + 1];
    float x1 = lerpf(grad(aa, xf, yf), grad(ba, xf - 1, yf), u);
    float x2 = lerpf(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u);
    return lerpf(x1, x2, v) * 0.7071f;
}

float Noise2D::fbm(float x, float y, int octaves, float lacunarity, float gain) const {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int i = 0; i < octaves; ++i) {
        sum += perlin(x * freq, y * freq) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum / norm;
}
