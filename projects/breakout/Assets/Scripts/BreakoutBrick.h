#pragma once

#include <GameSDK/Component.h>

#include <cstdint>

// A brick in the wall. The ball's first touch switches the brick's entity off
// and adds Points to the score.
struct BreakoutBrick : ECS::ComponentBase
{
    int32_t Points = 10; // @ge-range 0 1000
};
