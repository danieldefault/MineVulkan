#pragma once
#include "math.h"

class World;

struct PlayerInput {
    float moveX = 0;    // strafe [-1, 1] (direita +)
    float moveZ = 0;    // frente [-1, 1] (frente +)
    float lookYaw = 0;    // delta em radianos
    float lookPitch = 0;  // delta em radianos (cima +)
    bool jump = false;
};

class Player {
public:
    static constexpr float kHalfWidth = 0.3f;
    static constexpr float kHeight = 1.8f;
    static constexpr float kEyeHeight = 1.62f;
    static constexpr float kWalkSpeed = 4.3f;
    static constexpr float kJumpSpeed = 8.4f;
    static constexpr float kGravity = 28.0f;
    static constexpr float kMaxFall = 30.0f;

    Vec3 pos;  // centro dos pés
    Vec3 vel;
    float yaw = 0;    // 0 = olhando para -Z
    float pitch = 0;
    bool onGround = false;

    void applyLook(float dYaw, float dPitch);
    void step(const World& world, const PlayerInput& in, float dt);  // passo fixo

    Vec3 eye() const { return {pos.x, pos.y + kEyeHeight, pos.z}; }
    Vec3 forward() const;
    bool overlapsBlock(int x, int y, int z) const;

private:
    bool moveAxis(const World& world, int axis, float delta);  // true se colidiu
};

struct RayHit {
    bool hit = false;
    int x = 0, y = 0, z = 0;     // bloco atingido
    int nx = 0, ny = 0, nz = 0;  // normal da face atingida (onde colocar bloco)
};

RayHit raycastBlocks(const World& world, const Vec3& origin, const Vec3& dir, float maxDist);
