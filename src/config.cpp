#include "config.hpp"

namespace {
std::string Trim(std::string value)
{
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };

    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }

    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }

    return value;
}

std::string ToLower(std::string value)
{
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool ParseBool(const std::string& value, bool fallback)
{
    const std::string lowered = ToLower(Trim(value));
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        return false;
    }
    return fallback;
}

int ParseInt(const std::string& value, int fallback)
{
    try {
        return std::stoi(Trim(value));
    } catch (...) {
        return fallback;
    }
}
}

Config LoadConfig(const std::filesystem::path& path)
{
    Config config;

    std::ifstream stream(path);
    if (!stream.is_open()) {
        return config;
    }

    std::string current_section;
    std::string line;
    while (std::getline(stream, line)) {
        const auto comment_pos = line.find_first_of(";#");
        if (comment_pos != std::string::npos) {
            line.erase(comment_pos);
        }

        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            current_section = ToLower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const auto equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            continue;
        }

        const std::string key = ToLower(Trim(line.substr(0, equals_pos)));
        const std::string value = Trim(line.substr(equals_pos + 1));

        if (current_section == "xaudio2") {
            if (key == "enabled") {
                config.xaudio2_enabled = ParseBool(value, config.xaudio2_enabled);
            } else if (key == "forcestereomasteringvoice") {
                config.force_stereo_mastering_voice = ParseBool(value, config.force_stereo_mastering_voice);
            } else if (key == "overrideexplicitmultichannelvoices") {
                config.override_explicit_multichannel_voices =
                    ParseBool(value, config.override_explicit_multichannel_voices);
            }
        } else if (current_section == "wasapi") {
            if (key == "enabled") {
                config.wasapi_enabled = ParseBool(value, config.wasapi_enabled);
            } else if (key == "forcestereomixformat") {
                config.force_stereo_mix_format = ParseBool(value, config.force_stereo_mix_format);
            } else if (key == "forcestereoisformatsupported") {
                config.force_stereo_is_format_supported =
                    ParseBool(value, config.force_stereo_is_format_supported);
            } else if (key == "forcestereoinitialize") {
                config.force_stereo_initialize = ParseBool(value, config.force_stereo_initialize);
            } else if (key == "disablespatialaudioclient") {
                config.disable_spatial_audio_client = ParseBool(value, config.disable_spatial_audio_client);
            } else if (key == "rejectmultichannelisformatsupported") {
                config.reject_multichannel_is_format_supported =
                    ParseBool(value, config.reject_multichannel_is_format_supported);
            } else if (key == "rejectmultichannelinitialize") {
                config.reject_multichannel_initialize = ParseBool(value, config.reject_multichannel_initialize);
            }
        } else if (current_section == "spatial") {
            if (key == "wrapperenabled") {
                config.spatial_wrapper_enabled = ParseBool(value, config.spatial_wrapper_enabled);
            }
        } else if (current_section == "bootstrap") {
            if (key == "modulepolltimeoutms") {
                config.module_poll_timeout_ms = ParseInt(value, config.module_poll_timeout_ms);
            } else if (key == "modulepollintervalms") {
                config.module_poll_interval_ms = ParseInt(value, config.module_poll_interval_ms);
            }
        } else if (current_section == "logging") {
            if (key == "verbose") {
                config.verbose_logging = ParseBool(value, config.verbose_logging);
            }
        }
    }

    if (config.module_poll_timeout_ms < 0) {
        config.module_poll_timeout_ms = 0;
    }
    if (config.module_poll_interval_ms < 50) {
        config.module_poll_interval_ms = 50;
    }

    return config;
}
