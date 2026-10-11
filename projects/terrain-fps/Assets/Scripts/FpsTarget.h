#pragma once

#include <GameSDK/Component.h>

#include <cstdint>

// Something worth shooting: the first hit on it scores Points.
struct FpsTarget : ECS::ComponentBase
{
    int32_t Points = 1; // @ge-range 0 100
};
