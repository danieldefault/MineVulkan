#pragma once
// Matemática mínima: vetores, matrizes 4x4 (column-major, convenção Vulkan).
#include <cmath>
#include <cstring>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    float l = length(v);
    return l > 0.0f ? v * (1.0f / l) : v;
}

// m[col * 4 + row]
struct Mat4 {
    float m[16];
    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r{};
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
};

// Perspectiva RH, profundidade [0,1], Y invertido para o espaço de clip do Vulkan.
inline Mat4 perspectiveVk(float fovyRad, float aspect, float zNear, float zFar) {
    Mat4 r{};
    float f = 1.0f / std::tan(fovyRad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = -f;
    r.m[10] = zFar / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = zNear * zFar / (zNear - zFar);
    return r;
}

inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r = Mat4::identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

// Rotação 2D aplicada no espaço de clip (pré-rotação do swapchain no Android).
// x' = a*x + b*y ; y' = c*x + d*y
struct Rot2 {
    float a = 1, b = 0, c = 0, d = 1;
};

inline Mat4 rot2ToMat4(const Rot2& r) {
    Mat4 m = Mat4::identity();
    m.m[0] = r.a; m.m[4] = r.b;
    m.m[1] = r.c; m.m[5] = r.d;
    return m;
}

// Plano de frustum: dot(n, p) + d >= 0 significa "dentro".
struct Frustum {
    float planes[6][4];
    static Frustum fromMatrix(const Mat4& vp) {
        // Gribb/Hartmann, adaptado para profundidade [0,1].
        auto row = [&](int r, int c) { return vp.m[c * 4 + r]; };
        Frustum f{};
        for (int c = 0; c < 4; ++c) {
            f.planes[0][c] = row(3, c) + row(0, c);  // esquerda
            f.planes[1][c] = row(3, c) - row(0, c);  // direita
            f.planes[2][c] = row(3, c) + row(1, c);  // baixo/cima
            f.planes[3][c] = row(3, c) - row(1, c);
            f.planes[4][c] = row(2, c);              // perto (z >= 0)
            f.planes[5][c] = row(3, c) - row(2, c);  // longe
        }
        return f;
    }
    bool aabbVisible(const Vec3& mn, const Vec3& mx) const {
        for (const auto& p : planes) {
            Vec3 v{p[0] >= 0 ? mx.x : mn.x, p[1] >= 0 ? mx.y : mn.y, p[2] >= 0 ? mx.z : mn.z};
            if (p[0] * v.x + p[1] * v.y + p[2] * v.z + p[3] < 0) return false;
        }
        return true;
    }
};
