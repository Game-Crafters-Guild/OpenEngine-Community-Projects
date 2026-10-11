#pragma once

#include <GameSDK/System.h>

#include "PongRules.h"

#include <cstdint>
#include <string>

// Pong's rules: two paddles, a Jolt ball that bounces off the paddles and the side
// walls, goal triggers behind each paddle, first to PongRules::WinningScore (five by
// default) wins, R starts a new match.
// The match rules come from the PongRules component on the "Rules" entity.
// The left paddle is W/S; the right paddle is the computer until Up or Down is
// pressed, then it belongs to a second player for the rest of the match.
//
// The scene supplies the entities by name: "Left Paddle", "Right Paddle", "Ball",
// "Left Goal" and "Right Goal".
struct PongSystem : ECS::SystemBase
{
    void OnStart(ECS::World& world);
    void OnUpdate(ECS::World& world, float dt);
    void OnDestroy(ECS::World& world);

  private:
    enum class Phase
    {
        Serving,
        Playing,
        MatchOver,
    };

    void FindEntities(ECS::World& world);
    void ReadRules(ECS::World& world);
    void SubscribeBallEvents();
    void MovePaddles(ECS::World& world, float dt);
    void Serve(ECS::World& world);
    void KeepBallMoving(ECS::World& world);
    void ApplyGoal(ECS::World& world);
    void ResetBall(ECS::World& world);
    void NewMatch(ECS::World& world);
    void UpdateHud();

    ECS::EntityHandle m_LeftPaddle{};
    ECS::EntityHandle m_RightPaddle{};
    ECS::EntityHandle m_Ball{};
    ECS::EntityHandle m_LeftGoal{};
    ECS::EntityHandle m_RightGoal{};
    uint64_t m_ContactSubscription = 0;
    uint64_t m_TriggerSubscription = 0;
    ECS::EntityHandle m_PendingPaddleHit{};
    ECS::EntityHandle m_PendingGoal{};

    PongRules m_Rules{};
    Phase m_Phase = Phase::Serving;
    float m_ServeTimer = 0.0f;
    float m_BallSpeed = 0.0f;
    float m_BallHeading = 1.0f; // +1 while the ball heads for the right paddle; also the next serve's direction
    int m_ServeCount = 0;
    int m_LeftScore = 0;
    int m_RightScore = 0;
    bool m_RightIsHuman = false;
    std::string m_ShownHud;
};
