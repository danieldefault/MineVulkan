#include "player.h"

#include <algorithm>
#include <cmath>

#include "world.h"

namespace {
constexpr float kEps = 0.001f;
constexpr float kPitchLimit = 1.55f;
float& comp(Vec3& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }
}  // namespace

void Player::applyLook(float dYaw, float dPitch) {
    yaw += dYaw;
    pitch = std::clamp(pitch + dPitch, -kPitchLimit, kPitchLimit);
    const float twoPi = 6.2831853f;
    if (yaw > twoPi) yaw -= twoPi;
    if (yaw < 0) yaw += twoPi;
}

Vec3 Player::forward() const {
    float cp = std::cos(pitch);
    return {std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}

bool Player::overlapsBlock(int x, int y, int z) const {
    return pos.x + kHalfWidth > x && pos.x - kHalfWidth < x + 1 &&
           pos.y + kHeight > y && pos.y < y + 1 &&
           pos.z + kHalfWidth > z && pos.z - kHalfWidth < z + 1;
}

bool Player::moveAxis(const World& world, int axis, float delta) {
    if (delta == 0.0f) return false;
    comp(pos, axis) += delta;
    int x0 = static_cast<int>(std::floor(pos.x - kHalfWidth)), x1 = static_cast<int>(std::floor(pos.x + kHalfWidth));
    int y0 = static_cast<int>(std::floor(pos.y)), y1 = static_cast<int>(std::floor(pos.y + kHeight));
    int z0 = static_cast<int>(std::floor(pos.z - kHalfWidth)), z1 = static_cast<int>(std::floor(pos.z + kHalfWidth));
    bool hit = false;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                if (!world.isSolidForPhysics(x, y, z) || !overlapsBlock(x, y, z)) continue;
                hit = true;
                int b = axis == 0 ? x : (axis == 1 ? y : z);
                float ext = axis == 1 ? 0.0f : kHalfWidth;
                if (axis == 1) {
                    if (delta > 0) pos.y = static_cast<float>(b) - kHeight - kEps;
                    else pos.y = static_cast<float>(b + 1) + kEps;
                } else {
                    if (delta > 0) comp(pos, axis) = static_cast<float>(b) - ext - kEps;
                    else comp(pos, axis) = static_cast<float>(b + 1) + ext + kEps;
                }
            }
    if (hit) comp(vel, axis) = 0.0f;
    return hit;
}

void Player::step(const World& world, const PlayerInput& in, float dt) {
    // Direção de movimento no plano XZ, relativa ao yaw.
    float fx = std::sin(yaw), fz = -std::cos(yaw);
    float rx = std::cos(yaw), rz = std::sin(yaw);
    float wx = fx * in.moveZ + rx * in.moveX;
    float wz = fz * in.moveZ + rz * in.moveX;
    float len = std::sqrt(wx * wx + wz * wz);
    if (len > 1.0f) { wx /= len; wz /= len; }

    // Na água se a altura da cintura estiver submersa.
    inWater = world.getBlock(static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y + 0.4f)),
                             static_cast<int>(std::floor(pos.z))) == BLOCK_WATER;

    float control = inWater ? 0.25f : (onGround ? 1.0f : 0.12f);
    float speed = inWater ? kSwimSpeed : kWalkSpeed;
    vel.x += (wx * speed - vel.x) * control;
    vel.z += (wz * speed - vel.z) * control;

    if (inWater) {
        // Arrasto forte: a velocidade vertical tende a -kSinkSpeed (afundar) ou sobe nadando.
        // (um impulso acima do limite, p.ex. ao sair pela margem, só perde força pela gravidade)
        if (in.jump && vel.y < kSwimMaxUp) vel.y = std::min(vel.y + kSwimUpAccel * dt, kSwimMaxUp);
        else if (in.jump) vel.y -= kGravity * dt;
        else vel.y += (-kSinkSpeed - vel.y) * std::min(1.0f, 4.0f * dt);
    } else {
        if (in.jump && onGround) vel.y = kJumpSpeed;
        vel.y = std::max(vel.y - kGravity * dt, -kMaxFall);
    }

    bool wasOnGround = onGround;
    onGround = false;
    bool hitX = moveAxis(world, 0, vel.x * dt);
    bool hitZ = moveAxis(world, 2, vel.z * dt);
    float dy = vel.y * dt;
    if (moveAxis(world, 1, dy) && dy < 0) onGround = true;

    // Saindo da água: nadando na superfície contra a margem, pula para cima dela.
    bool headOut = world.getBlock(static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y + 1.5f)),
                                  static_cast<int>(std::floor(pos.z))) != BLOCK_WATER;
    if ((hitX || hitZ) && inWater && headOut && len > 0.1f) vel.y = std::max(vel.y, kWaterExitSpeed);

    // Pulo automático (essencial no touch): bateu numa parede andando no chão.
    if ((hitX || hitZ) && wasOnGround && len > 0.1f) {
        int fx0 = static_cast<int>(std::floor(pos.x + wx / std::max(len, 1e-4f) * 0.5f));
        int fz0 = static_cast<int>(std::floor(pos.z + wz / std::max(len, 1e-4f) * 0.5f));
        int fy = static_cast<int>(std::floor(pos.y));
        bool stepBlocked = world.isSolidForPhysics(fx0, fy, fz0);
        bool headFree = !world.isSolidForPhysics(fx0, fy + 1, fz0) && !world.isSolidForPhysics(fx0, fy + 2, fz0);
        if (stepBlocked && headFree) vel.y = kJumpSpeed;
    }
}

RayHit raycastBlocks(const World& world, const Vec3& origin, const Vec3& dir, float maxDist) {
    RayHit r;
    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));
    int sx = dir.x > 0 ? 1 : -1, sy = dir.y > 0 ? 1 : -1, sz = dir.z > 0 ? 1 : -1;
    auto inv = [](float d) { return d != 0.0f ? std::fabs(1.0f / d) : 1e30f; };
    float tdx = inv(dir.x), tdy = inv(dir.y), tdz = inv(dir.z);
    auto firstT = [](float o, float d, int cell) {
        if (d > 0) return (static_cast<float>(cell + 1) - o) / d;
        if (d < 0) return (o - static_cast<float>(cell)) / -d;
        return 1e30f;
    };
    float tmx = firstT(origin.x, dir.x, x), tmy = firstT(origin.y, dir.y, y), tmz = firstT(origin.z, dir.z, z);
    int nx = 0, ny = 0, nz = 0;
    float t = 0;
    while (t <= maxDist) {
        if (blockSolid(world.getBlock(x, y, z))) {
            r.hit = true;
            r.x = x; r.y = y; r.z = z;
            r.nx = nx; r.ny = ny; r.nz = nz;
            return r;
        }
        if (tmx < tmy && tmx < tmz) { x += sx; t = tmx; tmx += tdx; nx = -sx; ny = 0; nz = 0; }
        else if (tmy < tmz) { y += sy; t = tmy; tmy += tdy; nx = 0; ny = -sy; nz = 0; }
        else { z += sz; t = tmz; tmz += tdz; nx = 0; ny = 0; nz = -sz; }
    }
    return r;
}
