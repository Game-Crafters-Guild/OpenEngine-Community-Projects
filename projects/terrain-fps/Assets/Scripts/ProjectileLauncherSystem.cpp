#include "ProjectileLauncherSystem.h"

#include "Projectile.h"

#include "Assets/AssetManager.h"
#include "Components/Name.h"
#include "Components/RuntimeOnlyEntity.h"
#include "Components/Transform.h"
#include "Core/Engine.h"
#include "Engine/Rendering/PrimitiveGenerator.h"
#include "Input/InputSystem.h"
#include "Input/KeyCodes.h"
#include "PhysicsECS/Components/PhysicsBody.h"
#include "PhysicsECS/Components/PhysicsCollider.h"
#include "PhysicsECS/Components/SphereColliderShape.h"

#include <cstring>

namespace
{
using namespace GameEngine;
using Mathematics::Quaternion;
using Mathematics::Vector3;
using Primitives = Engine::Renderer::PrimitiveGenerator;

constexpr float kSecondsBetweenShots = 0.25f; // four balls a second while the button is held
constexpr float kLaunchSpeed = 20.0f;         // m/s along the view
constexpr float kMuzzleDistance = 0.9f;       // the ball appears this far in front of the eye
constexpr float kBallDiameter = 0.45f;        // m, large enough to show above the grass
constexpr float kBallMass = 2.0f;             // kg
constexpr float kBallLifetime = 6.0f;         // s
constexpr float kBallRollingDamping = 1.5f;   // the ball stops rolling within a few seconds on flat ground
constexpr const char* kBallMaterial = "Materials/Projectile.material";

GUID BallMaterial()
{
    AssetManager* assets = EngineCore::GetInstance().TryGetAssetManager();
    const GUID guid = assets ? assets->ResolveAssetGuid(kBallMaterial) : GUID{};
    return guid.IsNull() ? Primitives::DefaultMaterialGuid() : guid;
}
} // namespace

void ProjectileLauncherSystem::OnStart(ECS::World& world)
{
    m_Cooldown = 0.0f;
    m_Camera = {};
    world.Query<ECS::Read<Components::Name>>().Each(
        [this](ECS::EntityHandle entity, const Components::Name& name)
        {
            if (name.View() == "Main Camera")
                m_Camera = entity;
        });
}

void ProjectileLauncherSystem::OnUpdate(ECS::World& world, float dt)
{
    Expire(world, dt);
    m_Cooldown -= dt;
    Input::InputSystem* input = EngineCore::GetInstance().GetRuntimeInput();
    if (!input || !m_Camera.IsValid() || !input->IsMouseButtonDown(Input::kMouseButton_Left) || m_Cooldown > 0.0f)
        return;
    m_Cooldown = kSecondsBetweenShots;
    Launch(world);
}

// A new entity with a sphere mesh and a dynamic Jolt body whose initial velocity
// carries it along the view; the physics systems create the body on the next step.
void ProjectileLauncherSystem::Launch(ECS::World& world)
{
    const auto* camera = world.GetComponent<Components::Transform>(m_Camera);
    if (!camera)
        return;
    const Vector3 direction = camera->GetRotation().Rotate(Vector3(0.0f, 0.0f, 1.0f));
    const Vector3 start = camera->GetPosition() + direction * kMuzzleDistance;

    Components::PhysicsBody body;
    body.motionType = Physics::MotionType::Dynamic;
    body.mass = kBallMass;
    body.material.friction = 0.6f;
    body.material.restitution = 0.4f;
    body.angularDamping = kBallRollingDamping;
    body.continuousCollision = true;
    const Vector3 velocity = direction * kLaunchSpeed;
    body.linearVelocityX = velocity.x;
    body.linearVelocityY = velocity.y;
    body.linearVelocityZ = velocity.z;

    Components::PhysicsCollider collider;
    collider.layer = Physics::Layers::Dynamic;

    Components::SphereColliderShape shape; // radius 0.5 on the unit sphere, scaled with the entity

    Components::Name name{};
    std::strncpy(name.value, "Projectile", sizeof(name.value) - 1);

    auto* render = EngineCore::GetInstance().GetRenderServices();
    const ECS::EntityHandle ball = world.CreateEntity();
    world.AddComponentImmediate(ball, Components::RuntimeOnlyEntity{});
    world.AddComponentImmediate(ball, name);
    world.AddComponentImmediate(
        ball, Components::Transform::FromTRS(start, Quaternion::Identity(),
                                             Vector3(kBallDiameter, kBallDiameter, kBallDiameter)));
    world.AddComponentImmediate(ball,
                                Primitives::MakePrimitiveMeshRenderer(render, Primitives::SphereGuid(), BallMaterial()));
    world.AddComponentImmediate(ball, Primitives::MakePrimitiveLocalBounds(render, Primitives::SphereGuid()));
    world.AddComponentImmediate(ball, body);
    world.AddComponentImmediate(ball, collider);
    world.AddComponentImmediate(ball, shape);
    Projectile projectile;
    projectile.SecondsLeft = kBallLifetime;
    world.AddComponentImmediate(ball, projectile);
}

void ProjectileLauncherSystem::Expire(ECS::World& world, float dt)
{
    m_Expired.clear();
    world.Query<ECS::Write<Projectile>>().Each(
        [this, dt](ECS::EntityHandle entity, Projectile& projectile)
        {
            projectile.SecondsLeft -= dt;
            if (projectile.SecondsLeft <= 0.0f)
                m_Expired.push_back(entity);
        });
    for (ECS::EntityHandle entity : m_Expired)
        world.DestroyEntity(entity);
}
