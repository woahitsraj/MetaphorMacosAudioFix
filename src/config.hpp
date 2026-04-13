#pragma once

#include "stdafx.h"

struct Config {
    bool xaudio2_enabled = true;
    bool force_stereo_mastering_voice = true;
    bool override_explicit_multichannel_voices = true;
    bool wasapi_enabled = true;
    bool force_stereo_mix_format = true;
    bool force_stereo_is_format_supported = true;
    bool force_stereo_initialize = false;
    bool disable_spatial_audio_client = false;
    bool reject_multichannel_is_format_supported = true;
    bool reject_multichannel_initialize = true;
    bool spatial_wrapper_enabled = true;
    int module_poll_timeout_ms = 120000;
    int module_poll_interval_ms = 250;
    bool verbose_logging = true;
};

Config LoadConfig(const std::filesystem::path& path);
