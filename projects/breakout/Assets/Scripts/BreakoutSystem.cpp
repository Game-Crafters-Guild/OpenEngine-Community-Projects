#include "BreakoutSystem.h"

#include "BreakoutBrick.h"

#include "Components/Name.h"
#include "Components/Transform.h"
#include "Core/Engine.h"
#include "Engine/GameUI/GameplayUI.h"
#include "Input/InputSystem.h"
#include "Input/KeyCodes.h"
#include "PhysicsECS/Components/PhysicsBody.h"
#include "PhysicsECS/PhysicsEntityEventCallbacks.h"
#include "PhysicsECS/PhysicsWorldService.h"
#include "Types/StringId.h"
#include "UI/Controls/Label.h"

#include <algorithm>
#include <cmath>

namespace
{
using namespace GameEngine;
using Mathematics::Vector3;

constexpr float kBallSpeed = 11.0f;          // m/s, kept constant while playing
constexpr float kMinForwardFraction = 0.35f; // the ball never travels flatter than this across the field
constexpr float kPaddleSpeed = 15.0f;        // m/s
constexpr float kPaddleHalfWidth = 1.2f;     // half the paddle's X scale
constexpr float kFieldHalfWidth = 7.0f;      // inner faces of the side walls
constexpr float kServeHeightAbovePaddle = 0.65f; // Z offset of the served ball from the paddle
constexpr float kServeDelaySeconds = 1.5f;
constexpr float kLostBelowPaddle = 2.0f; // the ball is lost this far behind the paddle
constexpr int kStartLives = 3;

Physics::PhysicsWorld* PhysicsWorldOrNull()
{
    return PhysicsECS::PhysicsWorldService::TryGet();
}

// The Jolt body behind an entity's PhysicsBody, or an invalid handle until the
// physics systems have created it.
Physics::BodyHandle BodyOf(ECS::World& world, ECS::EntityHandle entity)
{
    const auto* body = world.GetComponent<Components::PhysicsBody>(entity);
    return body && body->initialized ? body->body : Physics::BodyHandle{};
}

Vector3 PositionOf(ECS::World& world, ECS::EntityHandle entity)
{
    const auto* transform = world.GetComponent<Components::Transform>(entity);
    return transform ? transform->GetPosition() : Vector3(0.0f, 0.0f, 0.0f);
}

// Moves an entity and its body together; the body is teleported, so use it only
// for the paddle and the served ball.
void PlaceEntity(ECS::World& world, ECS::EntityHandle entity, const Vector3& position)
{
    if (auto* transform = world.GetComponentForWrite<Components::Transform>(entity))
    {
        transform->matrix[12] = position.x;
        transform->matrix[13] = position.y;
        transform->matrix[14] = position.z;
    }
    Physics::PhysicsWorld* physics = PhysicsWorldOrNull();
    const Physics::BodyHandle body = BodyOf(world, entity);
    if (physics && body.IsValid())
        physics->SetBodyTransform(body, Physics::Transform{position, Mathematics::Quaternion::Identity()});
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
} // namespace

void BreakoutSystem::OnStart(ECS::World& world)
{
    FindEntities(world);
    SubscribeBallContacts();
    Restart(world);
}

void BreakoutSystem::OnDestroy(ECS::World&)
{
    if (m_ContactSubscription != 0)
        PhysicsECS::PhysicsEntityEventCallbacks::Unsubscribe(m_ContactSubscription);
    m_ContactSubscription = 0;
}

void BreakoutSystem::OnUpdate(ECS::World& world, float dt)
{
    Input::InputSystem* input = EngineCore::GetInstance().GetRuntimeInput();
    MovePaddle(world, dt);
    ApplyHits(world);

    switch (m_Phase)
    {
    case Phase::Serving:
        HoldBallOnPaddle(world);
        m_ServeTimer += dt;
        if (m_ServeTimer >= kServeDelaySeconds || KeyPressed(input, Input::kKeyCode_Space))
            Launch(world);
        break;
    case Phase::Playing:
        KeepBallMoving(world);
        CheckBallLost(world);
        break;
    case Phase::Won:
    case Phase::Lost:
        if (KeyPressed(input, Input::kKeyCode_R) || KeyPressed(input, Input::kKeyCode_Space))
            Restart(world);
        break;
    }
    UpdateHud();
}

void BreakoutSystem::FindEntities(ECS::World& world)
{
    m_Bricks.clear();
    world.Query<ECS::Read<Components::Name>>().Each(
        [this](ECS::EntityHandle entity, const Components::Name& name)
        {
            if (name.View() == "Paddle")
                m_Paddle = entity;
            else if (name.View() == "Ball")
                m_Ball = entity;
        });
    world.Query<ECS::Read<BreakoutBrick>>().Each(
        [this](ECS::EntityHandle entity, const BreakoutBrick&) { m_Bricks.push_back(entity); });
    if (!m_Paddle.IsValid() || !m_Ball.IsValid())
        Logger::Log::Error("Breakout: the scene needs entities named \"Paddle\" and \"Ball\".");
}

void BreakoutSystem::SubscribeBallContacts()
{
    if (!m_Ball.IsValid())
        return;
    // Contacts arrive during the physics step; they are queued here and resolved
    // in the next OnUpdate, outside the step.
    m_ContactSubscription = PhysicsECS::PhysicsEntityEventCallbacks::SubscribeContactBegin(
        m_Ball,
        [this](const PhysicsECS::EntityContactEvent& contact)
        {
            if (contact.other == m_Paddle)
                m_PendingPaddleHit = true;
            else
                m_PendingBrickHits.push_back(contact.other);
        });
}

void BreakoutSystem::MovePaddle(ECS::World& world, float dt)
{
    if (!m_Paddle.IsValid() || m_Phase == Phase::Won || m_Phase == Phase::Lost)
        return;
    Input::InputSystem* input = EngineCore::GetInstance().GetRuntimeInput();
    float direction = 0.0f;
    if (KeyDown(input, Input::kKeyCode_A) || KeyDown(input, Input::kKeyCode_Left))
        direction -= 1.0f;
    if (KeyDown(input, Input::kKeyCode_D) || KeyDown(input, Input::kKeyCode_Right))
        direction += 1.0f;
    Vector3 position = PositionOf(world, m_Paddle);
    const float limit = kFieldHalfWidth - kPaddleHalfWidth;
    position.x = std::clamp(position.x + direction * kPaddleSpeed * dt, -limit, limit);
    PlaceEntity(world, m_Paddle, position);
}

void BreakoutSystem::HoldBallOnPaddle(ECS::World& world)
{
    const Vector3 paddle = PositionOf(world, m_Paddle);
    PlaceEntity(world, m_Ball, Vector3(paddle.x, 0.0f, paddle.z + kServeHeightAbovePaddle));
    Physics::PhysicsWorld* physics = PhysicsWorldOrNull();
    const Physics::BodyHandle ball = BodyOf(world, m_Ball);
    if (physics && ball.IsValid())
        physics->SetLinearVelocity(ball, Vector3(0.0f, 0.0f, 0.0f));
}

void BreakoutSystem::Launch(ECS::World& world)
{
    Physics::PhysicsWorld* physics = PhysicsWorldOrNull();
    const Physics::BodyHandle ball = BodyOf(world, m_Ball);
    if (!physics || !ball.IsValid())
        return; // the body is created on a later frame; try again then
    physics->SetLinearVelocity(ball, Vector3(0.45f, 0.0f, 1.0f).Normalize() * kBallSpeed);
    m_Phase = Phase::Playing;
}

// The ball bounces on Jolt contacts (restitution 1, no friction); this keeps it on
// the table plane at a constant speed and turns a paddle hit into the classic
// angle: the further from the paddle's center, the steeper the return.
void BreakoutSystem::KeepBallMoving(ECS::World& world)
{
    Physics::PhysicsWorld* physics = PhysicsWorldOrNull();
    const Physics::BodyHandle ball = BodyOf(world, m_Ball);
    if (!physics || !ball.IsValid())
        return;

    Vector3 velocity = physics->GetLinearVelocity(ball);
    if (m_PendingPaddleHit)
    {
        m_PendingPaddleHit = false;
        const float offset = std::clamp(
            (PositionOf(world, m_Ball).x - PositionOf(world, m_Paddle).x) / kPaddleHalfWidth, -1.0f, 1.0f);
        velocity = Vector3(offset * 1.4f, 0.0f, 1.0f);
    }
    velocity.y = 0.0f;
    Vector3 direction = velocity.NormalizeOrZero();
    if (std::fabs(direction.z) < kMinForwardFraction)
    {
        const float side = direction.x < 0.0f ? -1.0f : 1.0f;
        const float forward = direction.z < 0.0f ? -1.0f : 1.0f;
        direction = Vector3(side * std::sqrt(1.0f - kMinForwardFraction * kMinForwardFraction), 0.0f,
                            forward * kMinForwardFraction);
    }
    physics->SetLinearVelocity(ball, direction * kBallSpeed);

    const Vector3 position = PositionOf(world, m_Ball);
    if (std::fabs(position.y) > 0.01f)
        PlaceEntity(world, m_Ball, Vector3(position.x, 0.0f, position.z));
}

void BreakoutSystem::ApplyHits(ECS::World& world)
{
    for (ECS::EntityHandle hit : m_PendingBrickHits)
    {
        const auto* brick = world.GetComponent<BreakoutBrick>(hit);
        ECS::Entity entity(&world, hit);
        if (!brick || !entity.IsEnabled())
            continue;
        entity.SetEnabled(false);
        m_Score += brick->Points;
        --m_BricksLeft;
    }
    m_PendingBrickHits.clear();
    if (m_Phase == Phase::Playing && m_BricksLeft <= 0)
    {
        m_Phase = Phase::Won;
        HoldBallOnPaddle(world);
    }
}

void BreakoutSystem::CheckBallLost(ECS::World& world)
{
    if (PositionOf(world, m_Ball).z > PositionOf(world, m_Paddle).z - kLostBelowPaddle)
        return;
    --m_Lives;
    m_Phase = m_Lives > 0 ? Phase::Serving : Phase::Lost;
    m_ServeTimer = 0.0f;
    HoldBallOnPaddle(world);
}

void BreakoutSystem::Restart(ECS::World& world)
{
    for (ECS::EntityHandle brick : m_Bricks)
        ECS::Entity(&world, brick).SetEnabled(true);
    m_BricksLeft = static_cast<int>(m_Bricks.size());
    m_PendingBrickHits.clear();
    m_PendingPaddleHit = false;
    m_Score = 0;
    m_Lives = kStartLives;
    m_ServeTimer = 0.0f;
    m_Phase = Phase::Serving;
}

void BreakoutSystem::UpdateHud()
{
    const char* prompt = "";
    switch (m_Phase)
    {
    case Phase::Serving: prompt = "Get ready! (Space to launch)"; break;
    case Phase::Playing: prompt = ""; break;
    case Phase::Won: prompt = "Wall cleared! Press R to play again"; break;
    case Phase::Lost: prompt = "Game over. Press R to restart"; break;
    }
    const std::string score = "Score " + std::to_string(m_Score);
    const std::string lives = "Lives " + std::to_string(m_Lives);
    // Elements are rebuilt when the HUD (re)binds, so the cache keys on the bind generation.
    const std::string shown =
        std::to_string(GameUI::GetBindGeneration()) + "|" + score + "|" + lives + "|" + prompt;
    if (shown == m_ShownHud)
        return;
    m_ShownHud = shown;
    SetLabelText("score-label", score);
    SetLabelText("lives-label", lives);
    SetLabelText("prompt-label", prompt);
}
