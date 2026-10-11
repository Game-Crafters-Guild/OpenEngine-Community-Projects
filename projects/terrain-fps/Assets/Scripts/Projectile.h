#pragma once

#include <GameSDK/Component.h>

// A ball fired by ProjectileLauncherSystem; it is removed when its time runs out.
// @ge-no-add        added by the launcher, not from the Add Component menu
struct Projectile : ECS::ComponentBase
{
    float SecondsLeft = 6.0f; // @ge-readonly
};
