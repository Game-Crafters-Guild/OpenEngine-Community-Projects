#pragma once

#include <GameSDK/System.h>

#include "Mathematics/Vector3.h"

#include <cstdint>
#include <string>
#include <unordered_set>

// A first-person walker on the terrain with a hitscan gun.
//
// The "Player" entity carries a CharacterController (the capsule); the "Main Camera"
// entity follows its eye and looks around. WASD walks, Shift runs, Space jumps, the
// right mouse button held (or the arrow keys) looks, the left mouse button (or F)
// fires a ray that pushes any physics body it hits; the first hit on an FpsTarget
// scores. N grows a new landscape by reseeding the "Hills" noise. The look starts
// where the scene's camera looks.
struct FirstPersonSystem : ECS::SystemBase
{
    void OnStart(ECS::World& world);
    void OnUpdate(ECS::World& world, float dt);

  private:
    void FindEntities(ECS::World& world);
    void Look(float dt);
    void Walk(ECS::World& world);
    void AimCamera(ECS::World& world);
    void Fire(ECS::World& world);
    void Reseed(ECS::World& world);
    void UpdateHud();

    ECS::EntityHandle m_Player{};
    ECS::EntityHandle m_Camera{};
    ECS::EntityHandle m_Hills{};
    GameEngine::Mathematics::Vector3 m_StartPosition{0.0f, 0.0f, 0.0f};
    int m_TargetCount = 0;
    std::unordered_set<uint64_t> m_HitBodies; // bodies of targets already scored
    int m_Score = 0;
    float m_Yaw = 0.0f;   // radians about +Y; 0 looks down +Z
    float m_Pitch = 0.0f; // radians about the camera's +X; positive looks down
    float m_LastMouseX = 0.0f;
    float m_LastMouseY = 0.0f;
    bool m_Dragging = false;
    uint32_t m_Seed = 0;
    std::string m_ShownHud;
};
