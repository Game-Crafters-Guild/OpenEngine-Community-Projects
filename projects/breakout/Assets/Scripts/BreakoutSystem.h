#pragma once

#include <GameSDK/System.h>

#include "Mathematics/Vector3.h"

#include <cstdint>
#include <string>
#include <vector>

// Breakout's rules: the paddle follows the keyboard, the ball is a Jolt body that
// bounces off the walls, the paddle and the bricks; a brick the ball touches
// switches off and scores. Three lives; R restarts after a win or a loss.
//
// The scene supplies the entities by name ("Paddle", "Ball", "Score", ...) and
// marks every brick with BreakoutBrick.
struct BreakoutSystem : ECS::SystemBase
{
    void OnStart(ECS::World& world);
    void OnUpdate(ECS::World& world, float dt);
    void OnDestroy(ECS::World& world);

  private:
    enum class Phase
    {
        Serving, // the ball rides on the paddle until it launches
        Playing,
        Won,
        Lost,
    };

    void FindEntities(ECS::World& world);
    void SubscribeBallContacts();
    void MovePaddle(ECS::World& world, float dt);
    void HoldBallOnPaddle(ECS::World& world);
    void Launch(ECS::World& world);
    void KeepBallMoving(ECS::World& world);
    void ApplyHits(ECS::World& world);
    void CheckBallLost(ECS::World& world);
    void Restart(ECS::World& world);
    void UpdateHud();

    ECS::EntityHandle m_Paddle{};
    ECS::EntityHandle m_Ball{};
    std::vector<ECS::EntityHandle> m_Bricks;
    std::vector<ECS::EntityHandle> m_PendingBrickHits;
    bool m_PendingPaddleHit = false;
    uint64_t m_ContactSubscription = 0;

    Phase m_Phase = Phase::Serving;
    float m_ServeTimer = 0.0f;
    int m_Score = 0;
    int m_Lives = 0;
    int m_BricksLeft = 0;
    std::string m_ShownHud;
};
