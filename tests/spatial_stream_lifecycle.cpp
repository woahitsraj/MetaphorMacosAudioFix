#include "../src/spatial_stream_lifecycle.hpp"

#include <cassert>

int main()
{
    SpatialStreamLifecycle lifecycle;

    lifecycle.OnStartResult(true);
    assert(lifecycle.Started());
    assert(lifecycle.OnResetResult(false, true, true) == SpatialStreamLifecycle::ResetAction::Recover);
    lifecycle.OnRecoveryResult(true, true);
    assert(lifecycle.Started());

    lifecycle.OnStopResult(false);
    assert(lifecycle.Started());
    lifecycle.OnStopResult(true);
    assert(!lifecycle.Started());

    lifecycle.OnStartResult(true);
    assert(lifecycle.OnResetResult(false, true, false) == SpatialStreamLifecycle::ResetAction::None);
    assert(lifecycle.Started());

    assert(lifecycle.OnResetResult(true, false, true) == SpatialStreamLifecycle::ResetAction::None);
    assert(!lifecycle.Started());

    lifecycle.OnStartResult(true);
    lifecycle.OnRecoveryResult(false, false);
    assert(lifecycle.Started());
    lifecycle.OnRecoveryResult(true, false);
    assert(!lifecycle.Started());
}
