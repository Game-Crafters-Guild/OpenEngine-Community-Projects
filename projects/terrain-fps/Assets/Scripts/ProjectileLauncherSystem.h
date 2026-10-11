#pragma once

#include <GameSDK/System.h>

#include <vector>

// Holding the left mouse button fires physical balls from the "Main Camera" along
// its view: each is a small dynamic Jolt sphere that falls, bounces and rolls on the
// terrain and knocks the crates about, and disappears after a few seconds.
struct ProjectileLauncherSystem : ECS::SystemBase
{
    void OnStart(ECS::World& world);
    void OnUpdate(ECS::World& world, float dt);

  private:
    void Launch(ECS::World& world);
    void Expire(ECS::World& world, float dt);

    ECS::EntityHandle m_Camera{};
    float m_Cooldown = 0.0f;
    std::vector<ECS::EntityHandle> m_Expired;
};
