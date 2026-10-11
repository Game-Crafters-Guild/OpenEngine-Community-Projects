#pragma once

#include <GameSDK/Component.h>

#include <cstdint>

// Match tuning, on the scene's "Rules" entity. Edit it in the Inspector.
struct PongRules : ECS::ComponentBase
{
    int32_t WinningScore = 5;      // @ge-range 1 21
    float ServeSpeed = 12.0f;      // @ge-range 2 40   m/s
    float SpeedGainPerHit = 0.6f;  // @ge-range 0 5    m/s added by every paddle return
    float MaxBallSpeed = 20.0f;    // @ge-range 2 60   m/s
    float ComputerSpeed = 8.5f;    // @ge-range 1 40   m/s; lower makes the computer easier to beat
};
