#include "PongSystem.h"

#include "PongRules.h"

#include "Components/Name.h"
#include "Components/Transform.h"
#include "Core/Engine.h"
#include "Engine/GameUI/GameplayUI.h"
#include "Input/InputSystem.h"
#include "Input/KeyCodes.h"
#include "Logger/Logger.h"
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

constexpr float kMinAcrossFraction = 0.5f; // the ball always crosses the court at least this steeply
constexpr float kPlayerPaddleSpeed = 13.0f;
constexpr float kComputerDeadZone = 0.35f; // the computer ignores smaller misses, so it can be beaten
constexpr float kPaddleHalfLength = 1.2f;  // half the paddles' Z scale
constexpr float kCourtHalfDepth = 6.0f;    // inner faces of the top and bottom walls
constexpr float kServeDelaySeconds = 1.0f;

Physics::PhysicsWorld* PhysicsWorldOrNull()
{
    return PhysicsECS::PhysicsWorldService::TryGet();
}

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

// Moves an entity and teleports its body with it (paddles, and the ball on a serve).
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

void SetBallVelocity(ECS::World& world, ECS::EntityHandle ball, const Vector3& velocity)
{
    Physics::PhysicsWorld* physics = PhysicsWorldOrNull();
    const Physics::BodyHandle body = BodyOf(world, ball);
    if (physics && body.IsValid())
        physics->SetLinearVelocity(body, velocity);
}

float KeyAxis(Input::InputSystem* input, int positiveKey, int negativeKey)
{
    if (!input)
        return 0.0f;
    return (input->IsKeyDown(positiveKey) ? 1.0f : 0.0f) - (input->IsKeyDown(negativeKey) ? 1.0f : 0.0f);
}

bool KeyPressed(Input::InputSystem* input, int key)
{
    return input && input->WasKeyPressed(key);
}

void SetLabelText(StringId id, const std::string& text)
{
    if (auto* label = dynamic_cast<Label*>(GameUI::FindElementById(id)))
        label->SetText(text);
}

void MovePaddle(ECS::World& world, ECS::EntityHandle paddle, float velocity, float dt)
{
    Vector3 position = PositionOf(world, paddle);
    const float limit = kCourtHalfDepth - kPaddleHalfLength;
    position.z = std::clamp(position.z + velocity * dt, -limit, limit);
    PlaceEntity(world, paddle, position);
}
} // namespace

void PongSystem::OnStart(ECS::World& world)
{
    FindEntities(world);
    ReadRules(world);
    SubscribeBallEvents();
    NewMatch(world);
}

void PongSystem::OnDestroy(ECS::World&)
{
    if (m_ContactSubscription != 0)
        PhysicsECS::PhysicsEntityEventCallbacks::Unsubscribe(m_ContactSubscription);
    if (m_TriggerSubscription != 0)
        PhysicsECS::PhysicsEntityEventCallbacks::Unsubscribe(m_TriggerSubscription);
    m_ContactSubscription = 0;
    m_TriggerSubscription = 0;
}

void PongSystem::OnUpdate(ECS::World& world, float dt)
{
    Input::InputSystem* input = EngineCore::GetInstance().GetRuntimeInput();
    if (KeyPressed(input, Input::kKeyCode_Up) || KeyPressed(input, Input::kKeyCode_Down))
        m_RightIsHuman = true;

    MovePaddles(world, dt);
    switch (m_Phase)
    {
    case Phase::Serving:
        m_ServeTimer += dt;
        if (m_ServeTimer >= kServeDelaySeconds)
            Serve(world);
        break;
    case Phase::Playing:
        ApplyGoal(world);
        if (m_Phase == Phase::Playing)
            KeepBallMoving(world);
        break;
    case Phase::MatchOver:
        if (KeyPressed(input, Input::kKeyCode_R) || KeyPressed(input, Input::kKeyCode_Space))
            NewMatch(world);
        break;
    }
    UpdateHud();
}

void PongSystem::FindEntities(ECS::World& world)
{
    world.Query<ECS::Read<Components::Name>>().Each(
        [this](ECS::EntityHandle entity, const Components::Name& name)
        {
            const std::string_view view = name.View();
            if (view == "Left Paddle")
                m_LeftPaddle = entity;
            else if (view == "Right Paddle")
                m_RightPaddle = entity;
            else if (view == "Ball")
                m_Ball = entity;
            else if (view == "Left Goal")
                m_LeftGoal = entity;
            else if (view == "Right Goal")
                m_RightGoal = entity;
        });
    if (!m_LeftPaddle.IsValid() || !m_RightPaddle.IsValid() || !m_Ball.IsValid() || !m_LeftGoal.IsValid() ||
        !m_RightGoal.IsValid())
        Logger::Log::Error("Pong: the scene needs \"Left Paddle\", \"Right Paddle\", \"Ball\", \"Left Goal\" "
                           "and \"Right Goal\".");
}

// The scene's PongRules, or the defaults when the scene has none.
void PongSystem::ReadRules(ECS::World& world)
{
    m_Rules = PongRules{};
    world.Query<ECS::Read<PongRules>>().Each(
        [this](ECS::EntityHandle, const PongRules& rules) { m_Rules = rules; });
}

void PongSystem::SubscribeBallEvents()
{
    if (!m_Ball.IsValid())
        return;
    // Both events arrive during the physics step; they are queued here and resolved
    // in the next OnUpdate.
    m_ContactSubscription = PhysicsECS::PhysicsEntityEventCallbacks::SubscribeContactBegin(
        m_Ball,
        [this](const PhysicsECS::EntityContactEvent& contact)
        {
            if (contact.other == m_LeftPaddle || contact.other == m_RightPaddle)
                m_PendingPaddleHit = contact.other;
        });
    m_TriggerSubscription = PhysicsECS::PhysicsEntityEventCallbacks::SubscribeTriggerEnter(
        m_Ball,
        [this](const PhysicsECS::EntityTriggerEvent& trigger)
        {
            if (trigger.other == m_LeftGoal || trigger.other == m_RightGoal)
                m_PendingGoal = trigger.other;
        });
}

void PongSystem::MovePaddles(ECS::World& world, float dt)
{
    if (m_Phase == Phase::MatchOver)
        return;
    Input::InputSystem* input = EngineCore::GetInstance().GetRuntimeInput();
    MovePaddle(world, m_LeftPaddle, KeyAxis(input, Input::kKeyCode_W, Input::kKeyCode_S) * kPlayerPaddleSpeed, dt);

    if (m_RightIsHuman)
    {
        MovePaddle(world, m_RightPaddle, KeyAxis(input, Input::kKeyCode_Up, Input::kKeyCode_Down) * kPlayerPaddleSpeed,
                   dt);
        return;
    }
    // The computer tracks the ball while it comes toward its side and drifts back
    // to the middle otherwise.
    const Vector3 ball = PositionOf(world, m_Ball);
    const Vector3 paddle = PositionOf(world, m_RightPaddle);
    const bool incoming = m_Phase == Phase::Playing && m_BallHeading > 0.0f;
    const float target = incoming ? ball.z : 0.0f;
    const float miss = target - paddle.z;
    const float velocity = std::fabs(miss) < kComputerDeadZone ? 0.0f : (miss > 0.0f ? 1.0f : -1.0f) * m_Rules.ComputerSpeed;
    MovePaddle(world, m_RightPaddle, velocity, dt);
}

void PongSystem::Serve(ECS::World& world)
{
    if (!BodyOf(world, m_Ball).IsValid())
        return; // the body is created on a later frame; try again then
    // Alternate the serve angle so consecutive serves do not repeat.
    const float across = (m_ServeCount % 2 == 0) ? 0.45f : -0.45f;
    ++m_ServeCount;
    SetBallVelocity(world, m_Ball, Vector3(m_BallHeading, 0.0f, across).Normalize() * m_BallSpeed);
    m_Phase = Phase::Playing;
}

// The ball bounces on Jolt contacts (restitution 1, no friction). This keeps it on
// the court plane at the rally's speed, speeds it up on every return and aims a
// return by where the ball struck the paddle.
void PongSystem::KeepBallMoving(ECS::World& world)
{
    Physics::PhysicsWorld* physics = PhysicsWorldOrNull();
    const Physics::BodyHandle ball = BodyOf(world, m_Ball);
    if (!physics || !ball.IsValid())
        return;

    Vector3 velocity = physics->GetLinearVelocity(ball);
    if (m_PendingPaddleHit.IsValid())
    {
        const ECS::EntityHandle paddle = m_PendingPaddleHit;
        m_PendingPaddleHit = {};
        const float offset = std::clamp(
            (PositionOf(world, m_Ball).z - PositionOf(world, paddle).z) / kPaddleHalfLength, -1.0f, 1.0f);
        const float away = paddle == m_LeftPaddle ? 1.0f : -1.0f;
        velocity = Vector3(away, 0.0f, offset * 0.9f);
        m_BallHeading = away;
        m_BallSpeed = std::min(m_BallSpeed + m_Rules.SpeedGainPerHit, m_Rules.MaxBallSpeed);
    }
    velocity.y = 0.0f;
    Vector3 direction = velocity.NormalizeOrZero();
    if (std::fabs(direction.x) < kMinAcrossFraction)
    {
        const float side = direction.x < 0.0f ? -1.0f : 1.0f;
        const float along = direction.z < 0.0f ? -1.0f : 1.0f;
        direction = Vector3(side * kMinAcrossFraction, 0.0f,
                            along * std::sqrt(1.0f - kMinAcrossFraction * kMinAcrossFraction));
    }
    physics->SetLinearVelocity(ball, direction * m_BallSpeed);

    const Vector3 position = PositionOf(world, m_Ball);
    if (std::fabs(position.y) > 0.01f)
        PlaceEntity(world, m_Ball, Vector3(position.x, 0.0f, position.z));
}

void PongSystem::ApplyGoal(ECS::World& world)
{
    if (!m_PendingGoal.IsValid())
        return;
    // A ball in the left goal is a point for the right player, and the next serve
    // goes toward the player who conceded.
    if (m_PendingGoal == m_LeftGoal)
    {
        ++m_RightScore;
        m_BallHeading = -1.0f;
    }
    else
    {
        ++m_LeftScore;
        m_BallHeading = 1.0f;
    }
    m_PendingGoal = {};
    ResetBall(world);
    m_Phase = (m_LeftScore >= m_Rules.WinningScore || m_RightScore >= m_Rules.WinningScore) ? Phase::MatchOver : Phase::Serving;
}

void PongSystem::ResetBall(ECS::World& world)
{
    PlaceEntity(world, m_Ball, Vector3(0.0f, 0.0f, 0.0f));
    SetBallVelocity(world, m_Ball, Vector3(0.0f, 0.0f, 0.0f));
    m_PendingPaddleHit = {};
    m_BallSpeed = m_Rules.ServeSpeed;
    m_ServeTimer = 0.0f;
}

void PongSystem::NewMatch(ECS::World& world)
{
    m_LeftScore = 0;
    m_RightScore = 0;
    m_ServeCount = 0;
    m_RightIsHuman = false;
    m_BallHeading = 1.0f;
    m_PendingGoal = {};
    ResetBall(world);
    m_Phase = Phase::Serving;
}

void PongSystem::UpdateHud()
{
    std::string prompt;
    if (m_Phase == Phase::MatchOver)
        prompt = std::string(m_LeftScore > m_RightScore ? "Blue" : "Red") + " wins! Press R for a new match";
    const char* mode = m_RightIsHuman ? "Blue: W / S    Red: Up / Down"
                                      : "Blue: W / S    Red: computer (press Up or Down to take over)";
    // Elements are rebuilt when the HUD (re)binds, so the cache keys on the bind generation.
    const std::string shown = std::to_string(GameUI::GetBindGeneration()) + "|" + std::to_string(m_LeftScore) + "|" +
                              std::to_string(m_RightScore) + "|" + prompt + "|" + mode;
    if (shown == m_ShownHud)
        return;
    m_ShownHud = shown;
    SetLabelText("left-score"_sid, std::to_string(m_LeftScore));
    SetLabelText("right-score"_sid, std::to_string(m_RightScore));
    SetLabelText("prompt-label"_sid, prompt);
    SetLabelText("mode-label"_sid, mode);
}
