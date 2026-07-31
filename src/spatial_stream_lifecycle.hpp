#pragma once

class SpatialStreamLifecycle {
public:
    enum class ResetAction {
        None,
        Recover,
    };

    void OnStartResult(bool succeeded)
    {
        if (succeeded) {
            started_ = true;
        }
    }

    ResetAction OnResetResult(bool succeeded, bool was_not_stopped, bool recovery_enabled)
    {
        if (succeeded) {
            started_ = false;
        } else if (started_ && was_not_stopped && recovery_enabled) {
            return ResetAction::Recover;
        }
        return ResetAction::None;
    }

    void OnStopResult(bool succeeded)
    {
        if (succeeded) {
            started_ = false;
        }
    }

    void OnRecoveryResult(bool stop_succeeded, bool start_succeeded)
    {
        if (stop_succeeded) {
            started_ = start_succeeded;
        }
    }

    bool Started() const { return started_; }

private:
    bool started_ = false;
};
