#include "FirstPersonSystem.h"

#include "FpsTarget.h"

#include "Components/Name.h"
#include "Components/Terrain/TerrainModifierEffects.h"
#include "Components/Transform.h"
#include "Core/Engine.h"
#include "Engine/GameUI/GameplayUI.h"
#include "Input/InputSystem.h"
#include "Input/KeyCodes.h"
#include "Logger/Logger.h"
#include "Mathematics/Quaternion.h"
#include "PhysicsECS/Components/CharacterController.h"
#include "PhysicsECS/Components/PhysicsBody.h"
#include "PhysicsECS/PhysicsWorldService.h"
#include "Types/StringId.h"
#include "UI/Controls/Label.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace
{
using namespace GameEngine;
using Mathematics::Quaternion;
using Mathematics::Vector3;

constexpr float kWalkSpeed = 5.0f;           // m/s
constexpr float kRunSpeed = 9.0f;            // m/s
constexpr float kJumpSpeed = 5.5f;           // m/s upward
constexpr float kMouseLookRadiansPerPixel = 0.0035f;
constexpr float kKeyLookRadiansPerSecond = 2.0f;
constexpr float kMaxPitch = 1.5f;            // just short of straight up or down
constexpr float kGunRange = 150.0f;          // m
constexpr float kGunImpulse = 70.0f;         // N*s at the hit point
constexpr float kMuzzleOffset = 0.5f;        // the ray starts this far in front of the eye
constexpr float kFallResetHeight = -60.0f;   // below this the player is put back at the start
constexpr float kEyeHeight = 1.6f;           // eye above the player's position (the capsule's feet)

Input::InputSystem* RuntimeInput()
{
    return EngineCore::GetInstance().GetRuntimeInput();
}

bool KeyDown(Input::InputSystem* input, int key)
{
    return input && input->IsKeyDown(key);
}

bool KeyPressed(Input::InputSystem* input, int key)
{
    return input && input->WasKeyPressed(key);
}

void SetLabelText(const char* id, const std::string& text)
{
    if (auto* label = dynamic_cast<Label*>(GameUI::FindElementByIdAny(HashStringId(id))))
        label->SetText(text);
}

Vector3 PositionOf(ECS::World& world, ECS::EntityHandle entity)
{
    const auto* transform = world.GetComponent<Components::WorldTransform>(entity);
    return transform ? Vector3(transform->matrix[12], transform->matrix[13], transform->matrix[14])
                     : Vector3(0.0f, 0.0f, 0.0f);
}

Quaternion LookOf(ECS::World& world, ECS::EntityHandle entity)
{
    const auto* transform = world.GetComponent<Components::Transform>(entity);
    return transform ? transform->GetRotation() : Quaternion::Identity();
}

// Yaw about world +Y, then pitch about the turned +X (left-handed: positive yaw
// turns +Z toward +X, positive pitch looks down).
Quaternion LookRotation(float yaw, float pitch)
{
    return Quaternion::FromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), yaw) *
           Quaternion::FromAxisAngle(Vector3(1.0f, 0.0f, 0.0f), pitch);
}
} // namespace

void FirstPersonSystem::OnStart(ECS::World& world)
{
    FindEntities(world);
    m_HitBodies.clear();
    m_Score = 0;
    // Start looking where the scene's camera looks, from where the player stands.
    const Vector3 forward = LookOf(world, m_Camera).Rotate(Vector3(0.0f, 0.0f, 1.0f));
    m_Yaw = std::atan2(forward.x, forward.z);
    m_Pitch = std::asin(std::clamp(-forward.y, -1.0f, 1.0f));
    m_StartPosition = PositionOf(world, m_Player);
    m_Dragging = false;
    m_ShownHud.clear();
    Reseed(world);
}

void FirstPersonSystem::OnUpdate(ECS::World& world, float dt)
{
    if (!m_Player.IsValid() || !m_Camera.IsValid())
        return;
    Input::InputSystem* input = RuntimeInput();
    Look(dt);
    Walk(world);
    AimCamera(world);
    if (KeyPressed(input, Input::kKeyCode_F))
        Fire(world);
    if (KeyPressed(input, Input::kKeyCode_N))
        Reseed(world);
    UpdateHud();
}

void FirstPersonSystem::FindEntities(ECS::World& world)
{
    world.Query<ECS::Read<Components::Name>>().Each(
        [this](ECS::EntityHandle entity, const Components::Name& name)
        {
            const std::string_view view = name.View();
            if (view == "Player")
                m_Player = entity;
            else if (view == "Main Camera")
                m_Camera = entity;
            else if (view == "Hills")
                m_Hills = entity;
        });
    m_TargetCount = 0;
    world.Query<ECS::Read<FpsTarget>>().Each([this](ECS::EntityHandle, const FpsTarget&) { ++m_TargetCount; });
    if (!m_Player.IsValid() || !m_Camera.IsValid())
        Logger::Log::Error("Terrain FPS: the scene needs a \"Player\" with a CharacterController and a "
                           "\"Main Camera\".");
}

void FirstPersonSystem::Look(float dt)
{
    Input::InputSystem* input = RuntimeInput();
    if (!input)
        return;
    // Mouse look while the right button is held: the pointer is not captured, so
    // the look follows the drag.
    const Mathematics::Vector2 mouse = input->GetMousePosition();
    const bool dragging = input->IsMouseButtonDown(Input::kMouseButton_Right);
    if (dragging && m_Dragging)
    {
        m_Yaw += (mouse.x - m_LastMouseX) * kMouseLookRadiansPerPixel;
        m_Pitch += (mouse.y - m_LastMouseY) * kMouseLookRadiansPerPixel;
    }
    m_Dragging = dragging;
    m_LastMouseX = mouse.x;
    m_LastMouseY = mouse.y;

    const float keyTurn = kKeyLookRadiansPerSecond * dt;
    if (KeyDown(input, Input::kKeyCode_Left))
        m_Yaw -= keyTurn;
    if (KeyDown(input, Input::kKeyCode_Right))
        m_Yaw += keyTurn;
    if (KeyDown(input, Input::kKeyCode_Up))
        m_Pitch -= keyTurn;
    if (KeyDown(input, Input::kKeyCode_Down))
        m_Pitch += keyTurn;
    m_Pitch = std::clamp(m_Pitch, -kMaxPitch, kMaxPitch);
}

// The CharacterController moves the capsule: the system only states the wish
// (horizontal velocity every frame, an upward velocity once for a jump).
void FirstPersonSystem::Walk(ECS::World& world)
{
    auto* controller = world.GetComponentForWrite<Components::CharacterController>(m_Player);
    if (!controller)
        return;
    Input::InputSystem* input = RuntimeInput();
    const float forward = (KeyDown(input, Input::kKeyCode_W) ? 1.0f : 0.0f) - (KeyDown(input, Input::kKeyCode_S) ? 1.0f : 0.0f);
    const float strafe = (KeyDown(input, Input::kKeyCode_D) ? 1.0f : 0.0f) - (KeyDown(input, Input::kKeyCode_A) ? 1.0f : 0.0f);
    const Vector3 facing(std::sin(m_Yaw), 0.0f, std::cos(m_Yaw));
    const Vector3 right(std::cos(m_Yaw), 0.0f, -std::sin(m_Yaw));
    const float speed = KeyDown(input, Input::kKeyCode_LeftShift) ? kRunSpeed : kWalkSpeed;
    const Vector3 velocity = (facing * forward + right * strafe).NormalizeOrZero() * speed;
    controller->desiredVelocityX = velocity.x;
    controller->desiredVelocityZ = velocity.z;
    if (controller->grounded && KeyPressed(input, Input::kKeyCode_Space))
        controller->desiredVelocityY = kJumpSpeed;

    if (PositionOf(world, m_Player).y < kFallResetHeight)
    {
        Physics::PhysicsWorld* physics = PhysicsECS::PhysicsWorldService::TryGet();
        if (physics && controller->initialized)
            physics->SetCharacterPosition(controller->character, m_StartPosition + Vector3(0.0f, 2.0f, 0.0f));
    }
}

// The camera is a separate entity placed at the player's eye every frame. It is not
// parented to the player: a child of a CharacterController entity does not follow
// the capsule in play mode (a known engine issue, #3623). The player's world
// position is last frame's, as OnUpdate runs before the physics step.
void FirstPersonSystem::AimCamera(ECS::World& world)
{
    auto* transform = world.GetComponentForWrite<Components::Transform>(m_Camera);
    if (!transform)
        return;
    const Vector3 eye = PositionOf(world, m_Player) + Vector3(0.0f, kEyeHeight, 0.0f);
    *transform = Components::Transform::FromTRS(eye, LookRotation(m_Yaw, m_Pitch), Vector3(1.0f, 1.0f, 1.0f));
}

// A hitscan shot from the eye along the view: it pushes whatever dynamic body it
// hits, and the first hit on a target scores.
void FirstPersonSystem::Fire(ECS::World& world)
{
    Physics::PhysicsWorld* physics = PhysicsECS::PhysicsWorldService::TryGet();
    if (!physics)
        return;
    const Vector3 direction = LookRotation(m_Yaw, m_Pitch).Rotate(Vector3(0.0f, 0.0f, 1.0f));
    Physics::RayCastQuery query;
    query.ray.origin = PositionOf(world, m_Camera) + direction * kMuzzleOffset;
    query.ray.direction = direction;
    query.maxDistance = kGunRange;
    query.filter.ignoreSensors = true;
    Physics::RayCastResult hit;
    if (!physics->RayCast(query, hit) || !hit.HasHit())
        return;

    // Push only simulated bodies (the terrain and other static colliders do not move);
    // the first hit on a target scores.
    world.Query<ECS::Read<Components::PhysicsBody>>().Each(
        [this, &world, &hit, physics, direction](ECS::EntityHandle entity, const Components::PhysicsBody& body)
        {
            if (!body.initialized || body.body != hit.body || body.motionType != Physics::MotionType::Dynamic)
                return;
            physics->AddImpulseAtPosition(hit.body, direction * kGunImpulse, hit.hitPoint);
            const auto* target = world.GetComponent<FpsTarget>(entity);
            if (target && m_HitBodies.insert(hit.body.Value()).second)
                m_Score += target->Points;
        });
}

// A new seed for the "Hills" noise; the terrain rebuilds from it. The clearing
// around the start stays flat, so the player and the crates keep their footing.
void FirstPersonSystem::Reseed(ECS::World& world)
{
    if (!m_Hills.IsValid())
        return;
    auto* noise = world.GetComponentForWrite<Components::TerrainNoiseEffect>(m_Hills);
    if (!noise)
        return;
    m_Seed = std::random_device{}() % 100000 + 1; // 0 would mean the noise's fixed default seed
    noise->Seed = m_Seed;
}

void FirstPersonSystem::UpdateHud()
{
    const std::string score = "Score " + std::to_string(m_Score) + "    Targets hit " +
                              std::to_string(m_HitBodies.size()) + " / " + std::to_string(m_TargetCount);
    const std::string seed = "Landscape " + std::to_string(m_Seed);
    // Elements are rebuilt when the HUD (re)binds, so the cache keys on the bind generation.
    const std::string shown = std::to_string(GameUI::GetBindGeneration()) + "|" + score + "|" + seed;
    if (shown == m_ShownHud)
        return;
    m_ShownHud = shown;
    SetLabelText("score-label", score);
    SetLabelText("seed-label", seed);
}
