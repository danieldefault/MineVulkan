#pragma once
// Geometria 2D da interface (em pixels lógicos, origem no canto superior esquerdo).
#include <cmath>
#include <cstdint>
#include <vector>

struct UiVertex {
    float x, y;
    uint32_t color;  // RGBA8 (R no byte menos significativo)
};

inline uint32_t uiColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(a) << 24);
}

inline uint32_t withAlpha(uint32_t rgba, uint8_t a) { return (rgba & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24); }

class UiBuilder {
public:
    std::vector<UiVertex> verts;

    void clear() { verts.clear(); }

    void rect(float x, float y, float w, float h, uint32_t c) {
        UiVertex a{x, y, c}, b{x + w, y, c}, d{x, y + h, c}, e{x + w, y + h, c};
        verts.insert(verts.end(), {a, b, e, e, d, a});
    }

    void frame(float x, float y, float w, float h, float t, uint32_t c) {
        rect(x, y, w, t, c);
        rect(x, y + h - t, w, t, c);
        rect(x, y + t, t, h - 2 * t, c);
        rect(x + w - t, y + t, t, h - 2 * t, c);
    }

    void circle(float cx, float cy, float r, uint32_t c, int segments = 28) {
        const float step = 6.2831853f / static_cast<float>(segments);
        for (int i = 0; i < segments; ++i) {
            float a0 = step * static_cast<float>(i), a1 = step * static_cast<float>(i + 1);
            verts.push_back({cx, cy, c});
            verts.push_back({cx + std::cos(a0) * r, cy + std::sin(a0) * r, c});
            verts.push_back({cx + std::cos(a1) * r, cy + std::sin(a1) * r, c});
        }
    }

    void ring(float cx, float cy, float r, float thickness, uint32_t c, int segments = 32) {
        const float step = 6.2831853f / static_cast<float>(segments);
        float ri = r - thickness;
        for (int i = 0; i < segments; ++i) {
            float a0 = step * static_cast<float>(i), a1 = step * static_cast<float>(i + 1);
            UiVertex o0{cx + std::cos(a0) * r, cy + std::sin(a0) * r, c};
            UiVertex o1{cx + std::cos(a1) * r, cy + std::sin(a1) * r, c};
            UiVertex i0{cx + std::cos(a0) * ri, cy + std::sin(a0) * ri, c};
            UiVertex i1{cx + std::cos(a1) * ri, cy + std::sin(a1) * ri, c};
            verts.insert(verts.end(), {o0, o1, i1, i1, i0, o0});
        }
    }
};
