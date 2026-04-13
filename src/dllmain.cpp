#include "stdafx.h"

#include "config.hpp"
#include "log.hpp"

#include <MinHook.h>

namespace {
constexpr std::wstring_view kFixName = L"MetaphorAudioFix";
constexpr std::wstring_view kConfigName = L"MetaphorAudioFix.ini";
constexpr std::wstring_view kLogName = L"MetaphorAudioFix.log";

constexpr std::wstring_view kXAudio27 = L"xaudio2_7.dll";
constexpr std::wstring_view kXAudio28 = L"xaudio2_8.dll";
constexpr std::wstring_view kXAudio29 = L"xaudio2_9.dll";
constexpr std::wstring_view kXAudio29Redist = L"xaudio2_9redist.dll";
constexpr std::wstring_view kOle32 = L"ole32.dll";

constexpr GUID kClsidMMDeviceEnumerator =
    {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
constexpr GUID kIidIMMDeviceEnumerator =
    {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
constexpr GUID kIidIAudioClient =
    {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
constexpr GUID kIidIAudioClient2 =
    {0x726778CD, 0xF60A, 0x4EDA, {0x82, 0xDE, 0xE4, 0x76, 0x10, 0xCD, 0x78, 0xAA}};
constexpr GUID kIidISpatialAudioClient =
    {0xBBF8E066, 0xAAAA, 0x49BE, {0x9A, 0x4D, 0xFD, 0x2A, 0x85, 0x8E, 0xA2, 0x7F}};
constexpr GUID kSubFormatIEEEFloat =
    {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

constexpr size_t kXAudio2ModernCreateMasteringVoiceIndex = 7;
constexpr size_t kXAudio27CreateMasteringVoiceIndex = 10;
constexpr size_t kIMMDeviceEnumeratorGetDefaultAudioEndpointIndex = 4;
constexpr size_t kIMMDeviceEnumeratorGetDeviceIndex = 5;
constexpr size_t kIMMDeviceActivateIndex = 3;
constexpr size_t kIAudioClientInitializeIndex = 3;
constexpr size_t kIAudioClientIsFormatSupportedIndex = 7;
constexpr size_t kIAudioClientGetMixFormatIndex = 8;
constexpr REFERENCE_TIME kSpatialDefaultPeriod = 100000;
constexpr DWORD kSpatialStreamFlags =
    AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;

HMODULE g_this_module = nullptr;
std::filesystem::path g_module_dir;
Config g_config{};

using XAudio2CreateFn = HRESULT(WINAPI*)(void** ppXAudio2, UINT32 flags, XAUDIO2_PROCESSOR processor);
using CreateMasteringVoiceModernFn =
    HRESULT(STDMETHODCALLTYPE*)(void* self, IXAudio2MasteringVoice** mastering_voice, UINT32 input_channels,
                                UINT32 input_sample_rate, UINT32 flags, LPCWSTR device_id,
                                const XAUDIO2_EFFECT_CHAIN* effect_chain, AUDIO_STREAM_CATEGORY stream_category);
using CreateMasteringVoice27Fn =
    HRESULT(STDMETHODCALLTYPE*)(void* self, IXAudio2MasteringVoice** mastering_voice, UINT32 input_channels,
                                UINT32 input_sample_rate, UINT32 flags, UINT32 device_index,
                                const XAUDIO2_EFFECT_CHAIN* effect_chain);
using CoCreateInstanceFn = HRESULT(WINAPI*)(REFCLSID clsid, LPUNKNOWN outer, DWORD clsctx, REFIID iid, LPVOID* out);
using GetDefaultAudioEndpointFn = HRESULT(STDMETHODCALLTYPE*)(void* self, EDataFlow flow, ERole role, IMMDevice** device);
using GetDeviceFn = HRESULT(STDMETHODCALLTYPE*)(void* self, LPCWSTR device_id, IMMDevice** device);
using IMMDeviceActivateFn = HRESULT(STDMETHODCALLTYPE*)(void* self, REFIID iid, DWORD clsctx, PROPVARIANT* activation_params, void** out);
using AudioClientInitializeFn =
    HRESULT(STDMETHODCALLTYPE*)(void* self, AUDCLNT_SHAREMODE share_mode, DWORD stream_flags,
                                REFERENCE_TIME hns_buffer_duration, REFERENCE_TIME hns_periodicity,
                                const WAVEFORMATEX* format, const GUID* audio_session_guid);
using AudioClientIsFormatSupportedFn =
    HRESULT(STDMETHODCALLTYPE*)(void* self, AUDCLNT_SHAREMODE share_mode, const WAVEFORMATEX* format, WAVEFORMATEX** closest_match);
using AudioClientGetMixFormatFn = HRESULT(STDMETHODCALLTYPE*)(void* self, WAVEFORMATEX** device_format);

XAudio2CreateFn g_xaudio2_create_modern = nullptr;
XAudio2CreateFn g_xaudio2_create_27 = nullptr;
CreateMasteringVoiceModernFn g_create_mastering_voice_modern = nullptr;
CreateMasteringVoice27Fn g_create_mastering_voice_27 = nullptr;
CoCreateInstanceFn g_co_create_instance = nullptr;
GetDefaultAudioEndpointFn g_get_default_audio_endpoint = nullptr;
GetDeviceFn g_get_device = nullptr;
IMMDeviceActivateFn g_immdevice_activate = nullptr;
AudioClientInitializeFn g_audio_client_initialize = nullptr;
AudioClientIsFormatSupportedFn g_audio_client_is_format_supported = nullptr;
AudioClientGetMixFormatFn g_audio_client_get_mix_format = nullptr;

std::atomic<bool> g_hooked_xaudio2_modern = false;
std::atomic<bool> g_hooked_xaudio2_27 = false;
std::atomic<bool> g_hooked_create_mastering_voice_modern = false;
std::atomic<bool> g_hooked_create_mastering_voice_27 = false;
std::atomic<bool> g_hooked_co_create_instance = false;
std::atomic<bool> g_hooked_get_default_audio_endpoint = false;
std::atomic<bool> g_hooked_get_device = false;
std::atomic<bool> g_hooked_immdevice_activate = false;
std::atomic<bool> g_hooked_audio_client_initialize = false;
std::atomic<bool> g_hooked_audio_client_is_format_supported = false;
std::atomic<bool> g_hooked_audio_client_get_mix_format = false;
std::atomic<bool> g_saw_wasapi_activity = false;

std::string Narrow(const std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string utf8(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(), required, nullptr, nullptr);
    return utf8;
}

std::wstring GetModuleFileNameString(HMODULE module)
{
    std::wstring buffer(MAX_PATH, L'\0');

    while (true) {
        const DWORD copied = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            return {};
        }

        if (copied < buffer.size() - 1) {
            buffer.resize(copied);
            return buffer;
        }

        buffer.resize(buffer.size() * 2);
    }
}

std::string GuidToString(REFGUID guid)
{
    wchar_t buffer[64]{};
    const int copied = StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
    if (copied <= 1) {
        return "{}";
    }

    return Narrow(std::wstring_view(buffer, static_cast<size_t>(copied - 1)));
}

const char* ShareModeToString(AUDCLNT_SHAREMODE share_mode)
{
    switch (share_mode) {
    case AUDCLNT_SHAREMODE_SHARED:
        return "shared";
    case AUDCLNT_SHAREMODE_EXCLUSIVE:
        return "exclusive";
    default:
        return "unknown";
    }
}

bool IsAudioClientIid(REFIID iid)
{
    return IsEqualIID(iid, kIidIAudioClient) || IsEqualIID(iid, kIidIAudioClient2);
}

bool IsWaveFormatExtensible(const WAVEFORMATEX* format)
{
    return format && format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
           format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
}

bool ShouldForceStereoWaveFormat(const WAVEFORMATEX* format)
{
    return format && format->nChannels > 2;
}

void ForceStereoWaveFormatInPlace(WAVEFORMATEX* format)
{
    if (!format || format->nChannels <= 2) {
        return;
    }

    format->nChannels = 2;
    if (format->wBitsPerSample != 0) {
        format->nBlockAlign = static_cast<WORD>((format->nChannels * format->wBitsPerSample) / 8);
    }
    if (format->nBlockAlign != 0) {
        format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
    }

    if (IsWaveFormatExtensible(format)) {
        auto* extensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);
        extensible->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    }
}

std::vector<std::uint8_t> CloneWaveFormat(const WAVEFORMATEX* format)
{
    const size_t size = sizeof(WAVEFORMATEX) + format->cbSize;
    std::vector<std::uint8_t> copy(size);
    std::memcpy(copy.data(), format, size);
    return copy;
}

std::string DescribeWaveFormat(const WAVEFORMATEX* format)
{
    if (!format) {
        return "<null>";
    }

    std::ostringstream stream;
    stream << "tag=0x" << std::hex << format->wFormatTag << std::dec
           << " channels=" << format->nChannels
           << " sample_rate=" << format->nSamplesPerSec
           << " bits=" << format->wBitsPerSample
           << " block_align=" << format->nBlockAlign
           << " avg_bytes=" << format->nAvgBytesPerSec
           << " cbSize=" << format->cbSize;

    if (IsWaveFormatExtensible(format)) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        stream << " mask=0x" << std::hex << extensible->dwChannelMask << std::dec
               << " subformat=" << GuidToString(extensible->SubFormat);
    }

    return stream.str();
}

bool ShouldOverrideInputChannels(UINT32 input_channels)
{
    if (!g_config.force_stereo_mastering_voice) {
        return false;
    }

    if (input_channels == 0) {
        return true;
    }

    if (input_channels <= 2) {
        return false;
    }

    return g_config.override_explicit_multichannel_voices;
}

void LogMasteringVoiceResult(HRESULT result, IXAudio2MasteringVoice* mastering_voice)
{
    if (!mastering_voice) {
        Log::Warn("CreateMasteringVoice returned 0x%08lX without a mastering voice pointer", static_cast<unsigned long>(result));
        return;
    }

    XAUDIO2_VOICE_DETAILS details{};
    mastering_voice->GetVoiceDetails(&details);

    DWORD channel_mask = 0;
    const HRESULT mask_result = mastering_voice->GetChannelMask(&channel_mask);

    Log::Info(
        "CreateMasteringVoice result=0x%08lX actual_channels=%u channel_mask_result=0x%08lX channel_mask=0x%08lX",
        static_cast<unsigned long>(result),
        details.InputChannels,
        static_cast<unsigned long>(mask_result),
        static_cast<unsigned long>(channel_mask)
    );
}

HRESULT STDMETHODCALLTYPE HookedCreateMasteringVoiceModern(
    void* self,
    IXAudio2MasteringVoice** mastering_voice,
    UINT32 input_channels,
    UINT32 input_sample_rate,
    UINT32 flags,
    LPCWSTR device_id,
    const XAUDIO2_EFFECT_CHAIN* effect_chain,
    AUDIO_STREAM_CATEGORY stream_category)
{
    const UINT32 original_channels = input_channels;
    if (ShouldOverrideInputChannels(input_channels)) {
        input_channels = 2;
        Log::Warn("Overriding modern mastering voice channels from %u to %u", original_channels, input_channels);
    } else {
        Log::Info("Leaving modern mastering voice channels unchanged at %u", input_channels);
    }

    if (device_id) {
        Log::Info("Modern mastering voice device=%s sample_rate=%u flags=0x%08lX stream_category=%d",
                  Narrow(std::wstring_view(device_id)).c_str(),
                  input_sample_rate,
                  static_cast<unsigned long>(flags),
                  static_cast<int>(stream_category));
    } else {
        Log::Info("Modern mastering voice using default device sample_rate=%u flags=0x%08lX stream_category=%d",
                  input_sample_rate,
                  static_cast<unsigned long>(flags),
                  static_cast<int>(stream_category));
    }

    const HRESULT result = g_create_mastering_voice_modern(
        self,
        mastering_voice,
        input_channels,
        input_sample_rate,
        flags,
        device_id,
        effect_chain,
        stream_category
    );

    LogMasteringVoiceResult(result, mastering_voice ? *mastering_voice : nullptr);
    return result;
}

HRESULT STDMETHODCALLTYPE HookedCreateMasteringVoice27(
    void* self,
    IXAudio2MasteringVoice** mastering_voice,
    UINT32 input_channels,
    UINT32 input_sample_rate,
    UINT32 flags,
    UINT32 device_index,
    const XAUDIO2_EFFECT_CHAIN* effect_chain)
{
    const UINT32 original_channels = input_channels;
    if (ShouldOverrideInputChannels(input_channels)) {
        input_channels = 2;
        Log::Warn("Overriding XAudio2 2.7 mastering voice channels from %u to %u", original_channels, input_channels);
    } else {
        Log::Info("Leaving XAudio2 2.7 mastering voice channels unchanged at %u", input_channels);
    }

    Log::Info("XAudio2 2.7 mastering voice device_index=%u sample_rate=%u flags=0x%08lX",
              device_index,
              input_sample_rate,
              static_cast<unsigned long>(flags));

    const HRESULT result = g_create_mastering_voice_27(
        self,
        mastering_voice,
        input_channels,
        input_sample_rate,
        flags,
        device_index,
        effect_chain
    );

    LogMasteringVoiceResult(result, mastering_voice ? *mastering_voice : nullptr);
    return result;
}

void HookModernCreateMasteringVoice(void* xaudio2_instance, const wchar_t* module_name)
{
    if (g_hooked_create_mastering_voice_modern.load() || !xaudio2_instance) {
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(xaudio2_instance);
    if (!vtable) {
        Log::Error("Failed to inspect IXAudio2 vtable for %s", Narrow(module_name).c_str());
        return;
    }

    void* target = vtable[kXAudio2ModernCreateMasteringVoiceIndex];
    if (!target) {
        Log::Error("Modern CreateMasteringVoice vtable slot was null for %s", Narrow(module_name).c_str());
        return;
    }

    const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedCreateMasteringVoiceModern),
                                                  reinterpret_cast<void**>(&g_create_mastering_voice_modern));
    if (create_status != MH_OK) {
        Log::Error("MH_CreateHook failed for modern CreateMasteringVoice in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(create_status));
        return;
    }

    const MH_STATUS enable_status = MH_EnableHook(target);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        Log::Error("MH_EnableHook failed for modern CreateMasteringVoice in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(enable_status));
        return;
    }

    g_hooked_create_mastering_voice_modern = true;
    Log::Info("Hooked modern CreateMasteringVoice in %s", Narrow(module_name).c_str());
}

void Hook27CreateMasteringVoice(void* xaudio2_instance, const wchar_t* module_name)
{
    if (g_hooked_create_mastering_voice_27.load() || !xaudio2_instance) {
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(xaudio2_instance);
    if (!vtable) {
        Log::Error("Failed to inspect IXAudio2 2.7 vtable for %s", Narrow(module_name).c_str());
        return;
    }

    void* target = vtable[kXAudio27CreateMasteringVoiceIndex];
    if (!target) {
        Log::Error("XAudio2 2.7 CreateMasteringVoice vtable slot was null for %s", Narrow(module_name).c_str());
        return;
    }

    const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedCreateMasteringVoice27),
                                                  reinterpret_cast<void**>(&g_create_mastering_voice_27));
    if (create_status != MH_OK) {
        Log::Error("MH_CreateHook failed for XAudio2 2.7 CreateMasteringVoice in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(create_status));
        return;
    }

    const MH_STATUS enable_status = MH_EnableHook(target);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        Log::Error("MH_EnableHook failed for XAudio2 2.7 CreateMasteringVoice in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(enable_status));
        return;
    }

    g_hooked_create_mastering_voice_27 = true;
    Log::Info("Hooked XAudio2 2.7 CreateMasteringVoice in %s", Narrow(module_name).c_str());
}

HRESULT WINAPI HookedXAudio2CreateModern(void** xaudio2, UINT32 flags, XAUDIO2_PROCESSOR processor)
{
    const HRESULT result = g_xaudio2_create_modern(xaudio2, flags, processor);
    Log::Info("XAudio2Create modern result=0x%08lX flags=0x%08lX processor=0x%08lX instance=%p",
              static_cast<unsigned long>(result),
              static_cast<unsigned long>(flags),
              static_cast<unsigned long>(processor),
              xaudio2 ? *xaudio2 : nullptr);

    if (SUCCEEDED(result) && xaudio2 && *xaudio2) {
        HookModernCreateMasteringVoice(*xaudio2, L"modern XAudio2");
    }

    return result;
}

HRESULT WINAPI HookedXAudio2Create27(void** xaudio2, UINT32 flags, XAUDIO2_PROCESSOR processor)
{
    const HRESULT result = g_xaudio2_create_27(xaudio2, flags, processor);
    Log::Info("XAudio2Create 2.7 result=0x%08lX flags=0x%08lX processor=0x%08lX instance=%p",
              static_cast<unsigned long>(result),
              static_cast<unsigned long>(flags),
              static_cast<unsigned long>(processor),
              xaudio2 ? *xaudio2 : nullptr);

    if (SUCCEEDED(result) && xaudio2 && *xaudio2) {
        Hook27CreateMasteringVoice(*xaudio2, kXAudio27.data());
    }

    return result;
}

void HookXAudio2Export(const wchar_t* module_name, bool is_xaudio27)
{
    HMODULE module = GetModuleHandleW(module_name);
    if (!module) {
        return;
    }

    if (is_xaudio27 && g_hooked_xaudio2_27.load()) {
        return;
    }
    if (!is_xaudio27 && g_hooked_xaudio2_modern.load()) {
        return;
    }

    auto* target = reinterpret_cast<void*>(GetProcAddress(module, "XAudio2Create"));
    if (!target) {
        Log::Warn("Module %s was loaded but XAudio2Create was not exported", Narrow(module_name).c_str());
        return;
    }

    void* original = nullptr;
    const MH_STATUS create_status = MH_CreateHook(
        target,
        is_xaudio27 ? reinterpret_cast<void*>(&HookedXAudio2Create27) : reinterpret_cast<void*>(&HookedXAudio2CreateModern),
        &original
    );

    if (create_status != MH_OK) {
        Log::Error("MH_CreateHook failed for XAudio2Create in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(create_status));
        return;
    }

    const MH_STATUS enable_status = MH_EnableHook(target);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        Log::Error("MH_EnableHook failed for XAudio2Create in %s: %d",
                   Narrow(module_name).c_str(),
                   static_cast<int>(enable_status));
        return;
    }

    if (is_xaudio27) {
        g_xaudio2_create_27 = reinterpret_cast<XAudio2CreateFn>(original);
        g_hooked_xaudio2_27 = true;
    } else {
        g_xaudio2_create_modern = reinterpret_cast<XAudio2CreateFn>(original);
        g_hooked_xaudio2_modern = true;
    }

    Log::Info("Hooked XAudio2Create in %s", Narrow(module_name).c_str());
}

void HookAudioClient(void* audio_client);
struct StereoMixCoefficients {
    float left = 0.0f;
    float right = 0.0f;
};

constexpr UINT32 kSpatialSupportedStaticMaskBits =
    AudioObjectType_FrontLeft |
    AudioObjectType_FrontRight |
    AudioObjectType_FrontCenter |
    AudioObjectType_LowFrequency |
    AudioObjectType_BackLeft |
    AudioObjectType_BackRight |
    AudioObjectType_SideLeft |
    AudioObjectType_SideRight |
    AudioObjectType_TopFrontLeft |
    AudioObjectType_TopFrontRight |
    AudioObjectType_TopBackLeft |
    AudioObjectType_TopBackRight;

bool IsFloatObjectFormat(const WAVEFORMATEX* format)
{
    if (!format || format->nChannels != 1 || format->nSamplesPerSec != 48000 || format->wBitsPerSample != 32) {
        return false;
    }

    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }

    if (IsWaveFormatExtensible(format)) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return IsEqualGUID(extensible->SubFormat, kSubFormatIEEEFloat);
    }

    return false;
}

bool WaveFormatsEqual(const WAVEFORMATEX* lhs, const WAVEFORMATEX* rhs)
{
    if (!lhs || !rhs || lhs->cbSize != rhs->cbSize) {
        return false;
    }

    return std::memcmp(lhs, rhs, sizeof(WAVEFORMATEX) + lhs->cbSize) == 0;
}

WAVEFORMATEX* AllocateWaveFormatCopy(const WAVEFORMATEX* format)
{
    if (!format) {
        return nullptr;
    }

    const size_t size = sizeof(WAVEFORMATEX) + format->cbSize;
    auto* copy = static_cast<WAVEFORMATEX*>(std::malloc(size));
    if (!copy) {
        return nullptr;
    }

    std::memcpy(copy, format, size);
    return copy;
}

StereoMixCoefficients GetStereoMixCoefficients(AudioObjectType type)
{
    switch (type) {
    case AudioObjectType_FrontLeft:
        return {1.00f, 0.00f};
    case AudioObjectType_FrontRight:
        return {0.00f, 1.00f};
    case AudioObjectType_FrontCenter:
        return {0.85f, 0.85f};
    case AudioObjectType_LowFrequency:
        return {0.30f, 0.30f};
    case AudioObjectType_BackLeft:
        return {0.55f, 0.00f};
    case AudioObjectType_BackRight:
        return {0.00f, 0.55f};
    case AudioObjectType_SideLeft:
        return {0.65f, 0.00f};
    case AudioObjectType_SideRight:
        return {0.00f, 0.65f};
    case AudioObjectType_TopFrontLeft:
        return {0.45f, 0.00f};
    case AudioObjectType_TopFrontRight:
        return {0.00f, 0.45f};
    case AudioObjectType_TopBackLeft:
        return {0.30f, 0.00f};
    case AudioObjectType_TopBackRight:
        return {0.00f, 0.30f};
    case AudioObjectType_BackCenter:
        return {0.40f, 0.40f};
    default:
        return {};
    }
}

void BuildStereoFloatFormat(WAVEFORMATEXTENSIBLE& format, UINT32 sample_rate)
{
    std::memset(&format, 0, sizeof(format));
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 2;
    format.Format.nSamplesPerSec = sample_rate;
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign = static_cast<WORD>((format.Format.nChannels * format.Format.wBitsPerSample) / 8);
    format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = format.Format.wBitsPerSample;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = kSubFormatIEEEFloat;
}

class SpatialAudioWrapper;

class SpatialRenderObject final : public ISpatialAudioObject {
public:
    SpatialRenderObject(class SpatialRenderStream* stream, AudioObjectType type, UINT32 frames);
    ~SpatialRenderObject();
    void DetachStream();
    const std::vector<float>& Samples() const { return buffer_; }
    std::vector<float>& Samples() { return buffer_; }
    AudioObjectType Type() const { return type_; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** buffer, UINT32* bytes) override;
    HRESULT STDMETHODCALLTYPE SetEndOfStream(UINT32) override;
    HRESULT STDMETHODCALLTYPE IsActive(BOOL* active) override;
    HRESULT STDMETHODCALLTYPE GetAudioObjectType(AudioObjectType* type) override;
    HRESULT STDMETHODCALLTYPE SetPosition(float, float, float) override;
    HRESULT STDMETHODCALLTYPE SetVolume(float) override;

    std::atomic<ULONG> ref_{1};
    class SpatialRenderStream* stream_ = nullptr;
    AudioObjectType type_ = AudioObjectType_None;
    std::vector<float> buffer_;
};

class SpatialRenderStream final : public ISpatialAudioObjectRenderStream {
public:
    SpatialRenderStream(SpatialAudioWrapper* owner, const SpatialAudioObjectRenderStreamActivationParams& params);
    ~SpatialRenderStream();
    void RemoveObject(SpatialRenderObject* object);
    HRESULT ActivateOutput();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetAvailableDynamicObjectCount(UINT32* count) override;
    HRESULT STDMETHODCALLTYPE GetService(REFIID riid, void** service) override;
    HRESULT STDMETHODCALLTYPE Start() override;
    HRESULT STDMETHODCALLTYPE Stop() override;
    HRESULT STDMETHODCALLTYPE Reset() override;
    HRESULT STDMETHODCALLTYPE BeginUpdatingAudioObjects(UINT32* dynamic_count, UINT32* frames) override;
    HRESULT STDMETHODCALLTYPE EndUpdatingAudioObjects() override;
    HRESULT STDMETHODCALLTYPE ActivateSpatialAudioObject(AudioObjectType type, ISpatialAudioObject** out) override;
    void MixToStereo();

    std::atomic<ULONG> ref_{1};
    CRITICAL_SECTION lock_{};
    SpatialAudioWrapper* owner_ = nullptr;
    IAudioClient* audio_client_ = nullptr;
    IAudioRenderClient* render_client_ = nullptr;
    ISpatialAudioObjectRenderStreamNotify* notify_ = nullptr;
    HANDLE event_handle_ = nullptr;
    AudioObjectType static_mask_ = AudioObjectType_None;
    UINT32 period_frames_ = 0;
    UINT32 update_frames_ = ~0u;
    float* render_buffer_ = nullptr;
    WAVEFORMATEX* object_format_ = nullptr;
    WAVEFORMATEXTENSIBLE output_format_{};
    std::list<SpatialRenderObject*> objects_;

    friend class SpatialRenderObject;
};

class SpatialAudioWrapper final : public ISpatialAudioClient, public IAudioFormatEnumerator {
public:
    SpatialAudioWrapper(IMMDevice* device, ISpatialAudioClient* passthrough);
    ~SpatialAudioWrapper();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE GetStaticObjectPosition(AudioObjectType, float* x, float* y, float* z) override;
    HRESULT STDMETHODCALLTYPE GetNativeStaticObjectTypeMask(AudioObjectType* mask) override;
    HRESULT STDMETHODCALLTYPE GetMaxDynamicObjectCount(UINT32* value) override;
    HRESULT STDMETHODCALLTYPE GetSupportedAudioObjectFormatEnumerator(IAudioFormatEnumerator** enumerator) override;
    HRESULT STDMETHODCALLTYPE GetMaxFrameCount(const WAVEFORMATEX* format, UINT32* count) override;
    HRESULT STDMETHODCALLTYPE IsAudioObjectFormatSupported(const WAVEFORMATEX* format) override;
    HRESULT STDMETHODCALLTYPE IsSpatialAudioStreamAvailable(REFIID stream_uuid, const PROPVARIANT* info) override;
    HRESULT STDMETHODCALLTYPE ActivateSpatialAudioStream(const PROPVARIANT* prop, REFIID riid, void** out) override;

    HRESULT STDMETHODCALLTYPE GetCount(UINT32* count) override;
    HRESULT STDMETHODCALLTYPE GetFormat(UINT32 index, WAVEFORMATEX** format) override;

    IMMDevice* Device() const { return device_; }
    ISpatialAudioClient* Passthrough() const { return passthrough_; }
    const WAVEFORMATEX* ObjectFormat() const { return &object_format_; }

    std::atomic<ULONG> ref_{1};
    IMMDevice* device_ = nullptr;
    ISpatialAudioClient* passthrough_ = nullptr;
    WAVEFORMATEX object_format_{};
};

SpatialRenderObject::SpatialRenderObject(SpatialRenderStream* stream, AudioObjectType type, UINT32 frames)
    : stream_(stream), type_(type), buffer_(frames, 0.0f)
{
    if (stream_) {
        stream_->AddRef();
    }
}

void SpatialRenderObject::DetachStream()
{
    stream_ = nullptr;
}

SpatialRenderObject::~SpatialRenderObject()
{
    if (stream_) {
        stream_->RemoveObject(this);
        stream_->Release();
    }
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::QueryInterface(REFIID riid, void** out)
{
    if (!out) {
        return E_POINTER;
    }

    *out = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ISpatialAudioObjectBase) ||
        IsEqualIID(riid, IID_ISpatialAudioObject)) {
        *out = static_cast<ISpatialAudioObject*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE SpatialRenderObject::AddRef()
{
    return ref_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE SpatialRenderObject::Release()
{
    const ULONG ref = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (ref == 0) {
        delete this;
    }
    return ref;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::GetBuffer(BYTE** buffer, UINT32* bytes)
{
    if (!buffer || !bytes || !stream_) {
        return E_POINTER;
    }

    EnterCriticalSection(&stream_->lock_);
    if (stream_->update_frames_ == ~0u) {
        LeaveCriticalSection(&stream_->lock_);
        return SPTLAUDCLNT_E_OUT_OF_ORDER;
    }

    *buffer = reinterpret_cast<BYTE*>(buffer_.data());
    *bytes = stream_->update_frames_ * static_cast<UINT32>(sizeof(float));
    LeaveCriticalSection(&stream_->lock_);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::SetEndOfStream(UINT32)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::IsActive(BOOL* active)
{
    if (!active) {
        return E_POINTER;
    }
    *active = TRUE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::GetAudioObjectType(AudioObjectType* type)
{
    if (!type) {
        return E_POINTER;
    }
    *type = type_;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::SetPosition(float, float, float)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderObject::SetVolume(float)
{
    return S_OK;
}

SpatialRenderStream::SpatialRenderStream(SpatialAudioWrapper* owner, const SpatialAudioObjectRenderStreamActivationParams& params)
    : owner_(owner),
      notify_(params.NotifyObject),
      event_handle_(params.EventHandle),
      static_mask_(params.StaticObjectTypeMask),
      object_format_(AllocateWaveFormatCopy(params.ObjectFormat))
{
    InitializeCriticalSection(&lock_);
    if (owner_) {
        owner_->AddRef();
    }
    if (notify_) {
        notify_->AddRef();
    }
}

SpatialRenderStream::~SpatialRenderStream()
{
    if (audio_client_) {
        audio_client_->Stop();
    }
    if (render_client_ && update_frames_ != ~0u && update_frames_ > 0) {
        render_client_->ReleaseBuffer(update_frames_, 0);
    }

    for (SpatialRenderObject* object : objects_) {
        object->DetachStream();
        delete object;
    }
    objects_.clear();

    if (notify_) {
        notify_->Release();
    }
    if (render_client_) {
        render_client_->Release();
    }
    if (audio_client_) {
        audio_client_->Release();
    }
    if (owner_) {
        owner_->Release();
    }
    std::free(object_format_);
    DeleteCriticalSection(&lock_);
}

void SpatialRenderStream::RemoveObject(SpatialRenderObject* object)
{
    EnterCriticalSection(&lock_);
    objects_.remove(object);
    LeaveCriticalSection(&lock_);
}

HRESULT SpatialRenderStream::ActivateOutput()
{
    if (!owner_ || !owner_->Device() || !g_immdevice_activate || !event_handle_) {
        return E_FAIL;
    }

    void* raw_audio_client = nullptr;
    HRESULT hr = g_immdevice_activate(owner_->Device(), kIidIAudioClient, CLSCTX_INPROC_SERVER, nullptr, &raw_audio_client);
    if (FAILED(hr) || !raw_audio_client) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    audio_client_ = static_cast<IAudioClient*>(raw_audio_client);

    REFERENCE_TIME default_period = 0;
    hr = audio_client_->GetDevicePeriod(&default_period, nullptr);
    if (FAILED(hr) || default_period <= 0) {
        default_period = kSpatialDefaultPeriod;
    }

    BuildStereoFloatFormat(output_format_, object_format_ ? object_format_->nSamplesPerSec : 48000);
    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, kSpatialStreamFlags, default_period, 0, &output_format_.Format, nullptr);
    if (FAILED(hr)) {
        return hr;
    }

    hr = audio_client_->SetEventHandle(event_handle_);
    if (FAILED(hr)) {
        return hr;
    }

    hr = audio_client_->GetService(IID_IAudioRenderClient, reinterpret_cast<void**>(&render_client_));
    if (FAILED(hr) || !render_client_) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    period_frames_ = static_cast<UINT32>(MulDiv(default_period, output_format_.Format.nSamplesPerSec, 10000000));
    if (period_frames_ == 0) {
        period_frames_ = 1;
    }
    update_frames_ = ~0u;
    return S_OK;
}

void SpatialRenderStream::MixToStereo()
{
    if (!render_buffer_) {
        return;
    }

    std::fill(render_buffer_, render_buffer_ + static_cast<size_t>(update_frames_) * 2, 0.0f);
    for (SpatialRenderObject* object : objects_) {
        const StereoMixCoefficients gains = GetStereoMixCoefficients(object->Type());
        if (gains.left == 0.0f && gains.right == 0.0f) {
            continue;
        }

        const auto& samples = object->Samples();
        for (UINT32 index = 0; index < update_frames_; ++index) {
            render_buffer_[index * 2] += samples[index] * gains.left;
            render_buffer_[index * 2 + 1] += samples[index] * gains.right;
        }
    }
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::QueryInterface(REFIID riid, void** out)
{
    if (!out) {
        return E_POINTER;
    }

    *out = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ISpatialAudioObjectRenderStreamBase) ||
        IsEqualIID(riid, IID_ISpatialAudioObjectRenderStream)) {
        *out = static_cast<ISpatialAudioObjectRenderStream*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE SpatialRenderStream::AddRef()
{
    return ref_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE SpatialRenderStream::Release()
{
    const ULONG ref = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (ref == 0) {
        delete this;
    }
    return ref;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::GetAvailableDynamicObjectCount(UINT32* count)
{
    if (!count) {
        return E_POINTER;
    }
    *count = 0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::GetService(REFIID, void** service)
{
    if (service) {
        *service = nullptr;
    }
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::Start()
{
    return audio_client_ ? audio_client_->Start() : E_FAIL;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::Stop()
{
    return audio_client_ ? audio_client_->Stop() : E_FAIL;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::Reset()
{
    return audio_client_ ? audio_client_->Reset() : E_FAIL;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::BeginUpdatingAudioObjects(UINT32* dynamic_count, UINT32* frames)
{
    if (!dynamic_count || !frames || !render_client_) {
        return E_POINTER;
    }

    EnterCriticalSection(&lock_);
    if (update_frames_ != ~0u) {
        LeaveCriticalSection(&lock_);
        return SPTLAUDCLNT_E_OUT_OF_ORDER;
    }

    update_frames_ = period_frames_;
    HRESULT hr = render_client_->GetBuffer(update_frames_, reinterpret_cast<BYTE**>(&render_buffer_));
    if (FAILED(hr)) {
        update_frames_ = ~0u;
        render_buffer_ = nullptr;
        LeaveCriticalSection(&lock_);
        return hr;
    }

    for (SpatialRenderObject* object : objects_) {
        std::fill(object->Samples().begin(), object->Samples().end(), 0.0f);
    }

    *dynamic_count = 0;
    *frames = update_frames_;
    LeaveCriticalSection(&lock_);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::EndUpdatingAudioObjects()
{
    if (!render_client_) {
        return E_FAIL;
    }

    EnterCriticalSection(&lock_);
    if (update_frames_ == ~0u) {
        LeaveCriticalSection(&lock_);
        return SPTLAUDCLNT_E_OUT_OF_ORDER;
    }

    MixToStereo();
    const UINT32 frames = update_frames_;
    update_frames_ = ~0u;
    render_buffer_ = nullptr;
    LeaveCriticalSection(&lock_);
    return render_client_->ReleaseBuffer(frames, 0);
}

HRESULT STDMETHODCALLTYPE SpatialRenderStream::ActivateSpatialAudioObject(AudioObjectType type, ISpatialAudioObject** out)
{
    if (!out) {
        return E_POINTER;
    }

    *out = nullptr;
    if (type == AudioObjectType_Dynamic) {
        return SPTLAUDCLNT_E_NO_MORE_OBJECTS;
    }

    const UINT32 type_bits = static_cast<UINT32>(type);
    if (type_bits == 0 || (type_bits & (type_bits - 1)) != 0) {
        return E_INVALIDARG;
    }
    if ((static_cast<UINT32>(static_mask_) & type_bits) == 0) {
        return SPTLAUDCLNT_E_STATIC_OBJECT_NOT_AVAILABLE;
    }

    EnterCriticalSection(&lock_);
    for (SpatialRenderObject* object : objects_) {
        if (object->Type() == type) {
            LeaveCriticalSection(&lock_);
            return SPTLAUDCLNT_E_OBJECT_ALREADY_ACTIVE;
        }
    }

    auto* object = new (std::nothrow) SpatialRenderObject(this, type, period_frames_);
    if (!object) {
        LeaveCriticalSection(&lock_);
        return E_OUTOFMEMORY;
    }

    objects_.push_back(object);
    LeaveCriticalSection(&lock_);
    *out = object;
    return S_OK;
}

SpatialAudioWrapper::SpatialAudioWrapper(IMMDevice* device, ISpatialAudioClient* passthrough)
    : device_(device), passthrough_(passthrough)
{
    if (device_) {
        device_->AddRef();
    }
    if (passthrough_) {
        passthrough_->AddRef();
    }

    std::memset(&object_format_, 0, sizeof(object_format_));
    object_format_.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    object_format_.nChannels = 1;
    object_format_.nSamplesPerSec = 48000;
    object_format_.wBitsPerSample = 32;
    object_format_.nBlockAlign = 4;
    object_format_.nAvgBytesPerSec = object_format_.nSamplesPerSec * object_format_.nBlockAlign;
    object_format_.cbSize = 0;
}

SpatialAudioWrapper::~SpatialAudioWrapper()
{
    if (passthrough_) {
        passthrough_->Release();
    }
    if (device_) {
        device_->Release();
    }
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::QueryInterface(REFIID riid, void** out)
{
    if (!out) {
        return E_POINTER;
    }

    *out = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ISpatialAudioClient)) {
        *out = static_cast<ISpatialAudioClient*>(this);
    } else if (IsEqualIID(riid, IID_IAudioFormatEnumerator)) {
        *out = static_cast<IAudioFormatEnumerator*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE SpatialAudioWrapper::AddRef()
{
    return ref_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE SpatialAudioWrapper::Release()
{
    const ULONG ref = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (ref == 0) {
        delete this;
    }
    return ref;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetStaticObjectPosition(AudioObjectType, float* x, float* y, float* z)
{
    if (x) {
        *x = 0.0f;
    }
    if (y) {
        *y = 0.0f;
    }
    if (z) {
        *z = 0.0f;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetNativeStaticObjectTypeMask(AudioObjectType* mask)
{
    if (!mask) {
        return E_POINTER;
    }
    *mask = static_cast<AudioObjectType>(kSpatialSupportedStaticMaskBits);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetMaxDynamicObjectCount(UINT32* value)
{
    if (!value) {
        return E_POINTER;
    }
    *value = 0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetSupportedAudioObjectFormatEnumerator(IAudioFormatEnumerator** enumerator)
{
    if (!enumerator) {
        return E_POINTER;
    }
    *enumerator = static_cast<IAudioFormatEnumerator*>(this);
    AddRef();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetMaxFrameCount(const WAVEFORMATEX* format, UINT32* count)
{
    if (!format || !count) {
        return E_POINTER;
    }
    *count = static_cast<UINT32>(MulDiv(kSpatialDefaultPeriod, format->nSamplesPerSec, 10000000));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::IsAudioObjectFormatSupported(const WAVEFORMATEX* format)
{
    return WaveFormatsEqual(&object_format_, format) ? S_OK : AUDCLNT_E_UNSUPPORTED_FORMAT;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::IsSpatialAudioStreamAvailable(REFIID, const PROPVARIANT*)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::ActivateSpatialAudioStream(const PROPVARIANT* prop, REFIID riid, void** out)
{
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;

    if (!IsEqualIID(riid, IID_ISpatialAudioObjectRenderStream)) {
        return passthrough_ ? passthrough_->ActivateSpatialAudioStream(prop, riid, out) : E_NOINTERFACE;
    }

    if (!prop || prop->vt != VT_BLOB || prop->blob.cbSize != sizeof(SpatialAudioObjectRenderStreamActivationParams)) {
        return E_INVALIDARG;
    }

    const auto* params = reinterpret_cast<const SpatialAudioObjectRenderStreamActivationParams*>(prop->blob.pBlobData);
    if (!params || !params->ObjectFormat || !IsFloatObjectFormat(params->ObjectFormat) ||
        params->EventHandle == nullptr || params->EventHandle == INVALID_HANDLE_VALUE) {
        return E_INVALIDARG;
    }

    if ((static_cast<UINT32>(params->StaticObjectTypeMask) & static_cast<UINT32>(AudioObjectType_Dynamic)) != 0) {
        return E_INVALIDARG;
    }

    auto* stream = new (std::nothrow) SpatialRenderStream(this, *params);
    if (!stream) {
        return E_OUTOFMEMORY;
    }

    Log::Info("Spatial wrapper ActivateSpatialAudioStream static_mask=0x%08lX object_format=%s",
              static_cast<unsigned long>(params->StaticObjectTypeMask),
              DescribeWaveFormat(params->ObjectFormat).c_str());

    HRESULT hr = stream->ActivateOutput();
    if (FAILED(hr)) {
        Log::Warn("Spatial wrapper activation failed (0x%08lX), falling back to passthrough client", static_cast<unsigned long>(hr));
        delete stream;
        return passthrough_ ? passthrough_->ActivateSpatialAudioStream(prop, riid, out) : hr;
    }

    *out = static_cast<ISpatialAudioObjectRenderStream*>(stream);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetCount(UINT32* count)
{
    if (!count) {
        return E_POINTER;
    }
    *count = 1;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE SpatialAudioWrapper::GetFormat(UINT32 index, WAVEFORMATEX** format)
{
    if (!format) {
        return E_POINTER;
    }
    if (index != 0) {
        return E_INVALIDARG;
    }
    *format = &object_format_;
    return S_OK;
}

SpatialAudioWrapper* CreateSpatialAudioWrapper(IMMDevice* device, ISpatialAudioClient* passthrough)
{
    return new (std::nothrow) SpatialAudioWrapper(device, passthrough);
}

HRESULT STDMETHODCALLTYPE HookedAudioClientGetMixFormat(void* self, WAVEFORMATEX** device_format)
{
    g_saw_wasapi_activity = true;
    const HRESULT result = g_audio_client_get_mix_format(self, device_format);

    const WAVEFORMATEX* format = (device_format && *device_format) ? *device_format : nullptr;
    Log::Info("IAudioClient::GetMixFormat result=0x%08lX format=%s",
              static_cast<unsigned long>(result),
              DescribeWaveFormat(format).c_str());

    if (SUCCEEDED(result) && device_format && *device_format && g_config.force_stereo_mix_format &&
        ShouldForceStereoWaveFormat(*device_format)) {
        const std::string before = DescribeWaveFormat(*device_format);
        ForceStereoWaveFormatInPlace(*device_format);
        Log::Warn("Forced stereo GetMixFormat: %s -> %s", before.c_str(), DescribeWaveFormat(*device_format).c_str());
    }

    return result;
}

HRESULT STDMETHODCALLTYPE HookedAudioClientIsFormatSupported(
    void* self,
    AUDCLNT_SHAREMODE share_mode,
    const WAVEFORMATEX* format,
    WAVEFORMATEX** closest_match)
{
    g_saw_wasapi_activity = true;

    if (format && g_config.reject_multichannel_is_format_supported && format->nChannels > 2) {
        Log::Warn("Rejecting multichannel IsFormatSupported input share_mode=%s format=%s",
                  ShareModeToString(share_mode),
                  DescribeWaveFormat(format).c_str());
        if (closest_match) {
            *closest_match = nullptr;
        }
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    std::vector<std::uint8_t> format_copy_storage;
    const WAVEFORMATEX* format_to_use = format;
    if (format && g_config.force_stereo_is_format_supported && ShouldForceStereoWaveFormat(format)) {
        format_copy_storage = CloneWaveFormat(format);
        auto* format_copy = reinterpret_cast<WAVEFORMATEX*>(format_copy_storage.data());
        const std::string before = DescribeWaveFormat(format_copy);
        ForceStereoWaveFormatInPlace(format_copy);
        format_to_use = format_copy;
        Log::Warn("Forced stereo IsFormatSupported input: %s -> %s", before.c_str(), DescribeWaveFormat(format_copy).c_str());
    } else {
        Log::Info("IAudioClient::IsFormatSupported input share_mode=%s format=%s",
                  ShareModeToString(share_mode),
                  DescribeWaveFormat(format).c_str());
    }

    const HRESULT result = g_audio_client_is_format_supported(self, share_mode, format_to_use, closest_match);
    const WAVEFORMATEX* closest = (closest_match && *closest_match) ? *closest_match : nullptr;

    Log::Info("IAudioClient::IsFormatSupported result=0x%08lX closest_match=%s",
              static_cast<unsigned long>(result),
              DescribeWaveFormat(closest).c_str());
    return result;
}

HRESULT STDMETHODCALLTYPE HookedAudioClientInitialize(
    void* self,
    AUDCLNT_SHAREMODE share_mode,
    DWORD stream_flags,
    REFERENCE_TIME hns_buffer_duration,
    REFERENCE_TIME hns_periodicity,
    const WAVEFORMATEX* format,
    const GUID* audio_session_guid)
{
    g_saw_wasapi_activity = true;

    if (format && g_config.reject_multichannel_initialize && format->nChannels > 2) {
        Log::Warn("Rejecting multichannel Initialize input share_mode=%s flags=0x%08lX format=%s",
                  ShareModeToString(share_mode),
                  static_cast<unsigned long>(stream_flags),
                  DescribeWaveFormat(format).c_str());
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }

    std::vector<std::uint8_t> format_copy_storage;
    const WAVEFORMATEX* format_to_use = format;
    if (format && g_config.force_stereo_initialize && ShouldForceStereoWaveFormat(format)) {
        format_copy_storage = CloneWaveFormat(format);
        auto* format_copy = reinterpret_cast<WAVEFORMATEX*>(format_copy_storage.data());
        const std::string before = DescribeWaveFormat(format_copy);
        ForceStereoWaveFormatInPlace(format_copy);
        format_to_use = format_copy;
        Log::Warn("Forced stereo Initialize format: %s -> %s", before.c_str(), DescribeWaveFormat(format_copy).c_str());
    } else {
        Log::Info("IAudioClient::Initialize input share_mode=%s flags=0x%08lX buffer=%lld periodicity=%lld format=%s",
                  ShareModeToString(share_mode),
                  static_cast<unsigned long>(stream_flags),
                  static_cast<long long>(hns_buffer_duration),
                  static_cast<long long>(hns_periodicity),
                  DescribeWaveFormat(format).c_str());
    }

    const std::string session_guid = audio_session_guid ? GuidToString(*audio_session_guid) : "<null>";
    const HRESULT result = g_audio_client_initialize(
        self,
        share_mode,
        stream_flags,
        hns_buffer_duration,
        hns_periodicity,
        format_to_use,
        audio_session_guid
    );

    Log::Info("IAudioClient::Initialize result=0x%08lX session_guid=%s format_used=%s",
              static_cast<unsigned long>(result),
              session_guid.c_str(),
              DescribeWaveFormat(format_to_use).c_str());
    return result;
}

void HookAudioClient(void* audio_client)
{
    if (!audio_client) {
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(audio_client);
    if (!vtable) {
        Log::Error("Failed to inspect IAudioClient vtable");
        return;
    }

    if (!g_hooked_audio_client_initialize.load()) {
        void* target = vtable[kIAudioClientInitializeIndex];
        if (target) {
            const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedAudioClientInitialize),
                                                          reinterpret_cast<void**>(&g_audio_client_initialize));
            if (create_status == MH_OK) {
                const MH_STATUS enable_status = MH_EnableHook(target);
                if (enable_status == MH_OK || enable_status == MH_ERROR_ENABLED) {
                    g_hooked_audio_client_initialize = true;
                    Log::Info("Hooked IAudioClient::Initialize");
                }
            } else {
                Log::Error("MH_CreateHook failed for IAudioClient::Initialize: %d", static_cast<int>(create_status));
            }
        }
    }

    if (!g_hooked_audio_client_is_format_supported.load()) {
        void* target = vtable[kIAudioClientIsFormatSupportedIndex];
        if (target) {
            const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedAudioClientIsFormatSupported),
                                                          reinterpret_cast<void**>(&g_audio_client_is_format_supported));
            if (create_status == MH_OK) {
                const MH_STATUS enable_status = MH_EnableHook(target);
                if (enable_status == MH_OK || enable_status == MH_ERROR_ENABLED) {
                    g_hooked_audio_client_is_format_supported = true;
                    Log::Info("Hooked IAudioClient::IsFormatSupported");
                }
            } else {
                Log::Error("MH_CreateHook failed for IAudioClient::IsFormatSupported: %d", static_cast<int>(create_status));
            }
        }
    }

    if (!g_hooked_audio_client_get_mix_format.load()) {
        void* target = vtable[kIAudioClientGetMixFormatIndex];
        if (target) {
            const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedAudioClientGetMixFormat),
                                                          reinterpret_cast<void**>(&g_audio_client_get_mix_format));
            if (create_status == MH_OK) {
                const MH_STATUS enable_status = MH_EnableHook(target);
                if (enable_status == MH_OK || enable_status == MH_ERROR_ENABLED) {
                    g_hooked_audio_client_get_mix_format = true;
                    Log::Info("Hooked IAudioClient::GetMixFormat");
                }
            } else {
                Log::Error("MH_CreateHook failed for IAudioClient::GetMixFormat: %d", static_cast<int>(create_status));
            }
        }
    }
}

HRESULT STDMETHODCALLTYPE HookedIMMDeviceActivate(void* self, REFIID iid, DWORD clsctx, PROPVARIANT* activation_params, void** out)
{
    g_saw_wasapi_activity = true;
    if (g_config.disable_spatial_audio_client && IsEqualIID(iid, kIidISpatialAudioClient)) {
        Log::Warn("Blocking IMMDevice::Activate for ISpatialAudioClient and returning E_NOINTERFACE");
        if (out) {
            *out = nullptr;
        }
        return E_NOINTERFACE;
    }

    if (g_config.spatial_wrapper_enabled && IsEqualIID(iid, kIidISpatialAudioClient)) {
        void* passthrough = nullptr;
        const HRESULT result = g_immdevice_activate(self, iid, clsctx, activation_params, &passthrough);

        Log::Info("IMMDevice::Activate iid=%s clsctx=0x%08lX result=0x%08lX instance=%p",
                  GuidToString(iid).c_str(),
                  static_cast<unsigned long>(clsctx),
                  static_cast<unsigned long>(result),
                  passthrough);

        if (FAILED(result) || !passthrough) {
            if (out) {
                *out = passthrough;
            }
            return result;
        }

        SpatialAudioWrapper* wrapper = CreateSpatialAudioWrapper(
            reinterpret_cast<IMMDevice*>(self),
            static_cast<ISpatialAudioClient*>(passthrough)
        );
        if (!wrapper) {
            Log::Error("Failed to allocate spatial wrapper, returning passthrough client");
            if (out) {
                *out = passthrough;
            }
            return result;
        }

        static_cast<ISpatialAudioClient*>(passthrough)->Release();
        if (out) {
            *out = static_cast<ISpatialAudioClient*>(wrapper);
        }
        Log::Info("Returning wrapped ISpatialAudioClient passthrough=%p wrapper=%p",
                  passthrough,
                  static_cast<ISpatialAudioClient*>(wrapper));
        return S_OK;
    }

    const HRESULT result = g_immdevice_activate(self, iid, clsctx, activation_params, out);

    Log::Info("IMMDevice::Activate iid=%s clsctx=0x%08lX result=0x%08lX instance=%p",
              GuidToString(iid).c_str(),
              static_cast<unsigned long>(clsctx),
              static_cast<unsigned long>(result),
              out ? *out : nullptr);

    if (SUCCEEDED(result) && out && *out && IsAudioClientIid(iid)) {
        HookAudioClient(*out);
    }

    return result;
}

void HookIMMDevice(void* device)
{
    if (g_hooked_immdevice_activate.load() || !device) {
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(device);
    if (!vtable) {
        Log::Error("Failed to inspect IMMDevice vtable");
        return;
    }

    void* target = vtable[kIMMDeviceActivateIndex];
    if (!target) {
        Log::Error("IMMDevice::Activate vtable slot was null");
        return;
    }

    const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedIMMDeviceActivate),
                                                  reinterpret_cast<void**>(&g_immdevice_activate));
    if (create_status != MH_OK) {
        Log::Error("MH_CreateHook failed for IMMDevice::Activate: %d", static_cast<int>(create_status));
        return;
    }

    const MH_STATUS enable_status = MH_EnableHook(target);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        Log::Error("MH_EnableHook failed for IMMDevice::Activate: %d", static_cast<int>(enable_status));
        return;
    }

    g_hooked_immdevice_activate = true;
    Log::Info("Hooked IMMDevice::Activate");
}

HRESULT STDMETHODCALLTYPE HookedGetDefaultAudioEndpoint(void* self, EDataFlow flow, ERole role, IMMDevice** device)
{
    g_saw_wasapi_activity = true;
    const HRESULT result = g_get_default_audio_endpoint(self, flow, role, device);

    Log::Info("IMMDeviceEnumerator::GetDefaultAudioEndpoint flow=%d role=%d result=0x%08lX device=%p",
              static_cast<int>(flow),
              static_cast<int>(role),
              static_cast<unsigned long>(result),
              device ? *device : nullptr);

    if (SUCCEEDED(result) && device && *device) {
        HookIMMDevice(*device);
    }

    return result;
}

HRESULT STDMETHODCALLTYPE HookedGetDevice(void* self, LPCWSTR device_id, IMMDevice** device)
{
    g_saw_wasapi_activity = true;
    const HRESULT result = g_get_device(self, device_id, device);

    Log::Info("IMMDeviceEnumerator::GetDevice id=%s result=0x%08lX device=%p",
              device_id ? Narrow(std::wstring_view(device_id)).c_str() : "<null>",
              static_cast<unsigned long>(result),
              device ? *device : nullptr);

    if (SUCCEEDED(result) && device && *device) {
        HookIMMDevice(*device);
    }

    return result;
}

void HookIMMDeviceEnumerator(void* enumerator)
{
    if (!enumerator) {
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(enumerator);
    if (!vtable) {
        Log::Error("Failed to inspect IMMDeviceEnumerator vtable");
        return;
    }

    if (!g_hooked_get_default_audio_endpoint.load()) {
        void* target = vtable[kIMMDeviceEnumeratorGetDefaultAudioEndpointIndex];
        if (target) {
            const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedGetDefaultAudioEndpoint),
                                                          reinterpret_cast<void**>(&g_get_default_audio_endpoint));
            if (create_status == MH_OK) {
                const MH_STATUS enable_status = MH_EnableHook(target);
                if (enable_status == MH_OK || enable_status == MH_ERROR_ENABLED) {
                    g_hooked_get_default_audio_endpoint = true;
                    Log::Info("Hooked IMMDeviceEnumerator::GetDefaultAudioEndpoint");
                }
            } else {
                Log::Error("MH_CreateHook failed for GetDefaultAudioEndpoint: %d", static_cast<int>(create_status));
            }
        }
    }

    if (!g_hooked_get_device.load()) {
        void* target = vtable[kIMMDeviceEnumeratorGetDeviceIndex];
        if (target) {
            const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedGetDevice),
                                                          reinterpret_cast<void**>(&g_get_device));
            if (create_status == MH_OK) {
                const MH_STATUS enable_status = MH_EnableHook(target);
                if (enable_status == MH_OK || enable_status == MH_ERROR_ENABLED) {
                    g_hooked_get_device = true;
                    Log::Info("Hooked IMMDeviceEnumerator::GetDevice");
                }
            } else {
                Log::Error("MH_CreateHook failed for GetDevice: %d", static_cast<int>(create_status));
            }
        }
    }
}

HRESULT WINAPI HookedCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD clsctx, REFIID iid, LPVOID* out)
{
    const HRESULT result = g_co_create_instance(clsid, outer, clsctx, iid, out);

    if (IsEqualGUID(clsid, kClsidMMDeviceEnumerator) || IsEqualIID(iid, kIidIMMDeviceEnumerator) || IsAudioClientIid(iid)) {
        g_saw_wasapi_activity = true;
        Log::Info("CoCreateInstance clsid=%s iid=%s clsctx=0x%08lX result=0x%08lX instance=%p",
                  GuidToString(clsid).c_str(),
                  GuidToString(iid).c_str(),
                  static_cast<unsigned long>(clsctx),
                  static_cast<unsigned long>(result),
                  out ? *out : nullptr);
    }

    if (SUCCEEDED(result) && out && *out && IsEqualGUID(clsid, kClsidMMDeviceEnumerator)) {
        HookIMMDeviceEnumerator(*out);
    }

    return result;
}

void HookCoCreateInstance()
{
    if (!g_config.wasapi_enabled || g_hooked_co_create_instance.load()) {
        return;
    }

    HMODULE module = GetModuleHandleW(kOle32.data());
    if (!module) {
        return;
    }

    void* target = reinterpret_cast<void*>(GetProcAddress(module, "CoCreateInstance"));
    if (!target) {
        Log::Error("ole32.dll loaded without CoCreateInstance export");
        return;
    }

    const MH_STATUS create_status = MH_CreateHook(target, reinterpret_cast<void*>(&HookedCoCreateInstance),
                                                  reinterpret_cast<void**>(&g_co_create_instance));
    if (create_status != MH_OK) {
        Log::Error("MH_CreateHook failed for CoCreateInstance: %d", static_cast<int>(create_status));
        return;
    }

    const MH_STATUS enable_status = MH_EnableHook(target);
    if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
        Log::Error("MH_EnableHook failed for CoCreateInstance: %d", static_cast<int>(enable_status));
        return;
    }

    g_hooked_co_create_instance = true;
    Log::Info("Hooked CoCreateInstance in %s", Narrow(kOle32).c_str());
}

DWORD WINAPI MainThread(void*)
{
    const std::wstring module_path = GetModuleFileNameString(g_this_module);
    g_module_dir = std::filesystem::path(module_path).parent_path();

    g_config = LoadConfig(g_module_dir / kConfigName);
    Log::Init(g_module_dir / kLogName, g_config.verbose_logging);

    Log::Info("%s loaded from %s", Narrow(kFixName).c_str(), Narrow(g_module_dir.wstring()).c_str());
    Log::Info("Config: xaudio2_enabled=%d force_stereo_mastering_voice=%d override_explicit_multichannel_voices=%d",
              g_config.xaudio2_enabled,
              g_config.force_stereo_mastering_voice,
              g_config.override_explicit_multichannel_voices);
    Log::Info("Config: wasapi_enabled=%d force_stereo_mix_format=%d force_stereo_is_format_supported=%d force_stereo_initialize=%d",
              g_config.wasapi_enabled,
              g_config.force_stereo_mix_format,
              g_config.force_stereo_is_format_supported,
              g_config.force_stereo_initialize);
    Log::Info("Config: disable_spatial_audio_client=%d reject_multichannel_is_format_supported=%d reject_multichannel_initialize=%d",
              g_config.disable_spatial_audio_client,
              g_config.reject_multichannel_is_format_supported,
              g_config.reject_multichannel_initialize);
    Log::Info("Config: spatial_wrapper_enabled=%d", g_config.spatial_wrapper_enabled);
    Log::Info("Config: module_poll_timeout_ms=%d module_poll_interval_ms=%d verbose_logging=%d",
              g_config.module_poll_timeout_ms,
              g_config.module_poll_interval_ms,
              g_config.verbose_logging);

    const MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        Log::Error("MH_Initialize failed: %d", static_cast<int>(init_status));
        return 0;
    }

    const int timeout_ms = g_config.module_poll_timeout_ms;
    const int interval_ms = g_config.module_poll_interval_ms;
    int elapsed_ms = 0;
    while (timeout_ms == 0 || elapsed_ms <= timeout_ms) {
        HookCoCreateInstance();

        if (g_config.xaudio2_enabled) {
            HookXAudio2Export(kXAudio27.data(), true);
            HookXAudio2Export(kXAudio28.data(), false);
            HookXAudio2Export(kXAudio29.data(), false);
            HookXAudio2Export(kXAudio29Redist.data(), false);
        }

        static bool logged_wasapi_activity = false;
        if (g_saw_wasapi_activity.load() && !logged_wasapi_activity) {
            Log::Info("Observed WASAPI activity");
            logged_wasapi_activity = true;
        }

        if (g_hooked_xaudio2_modern.load() || g_hooked_xaudio2_27.load()) {
            Log::Info("At least one XAudio2 module is hooked");
            return 0;
        }

        Sleep(interval_ms);
        elapsed_ms += interval_ms;
    }

    if (!g_config.xaudio2_enabled && !g_config.wasapi_enabled) {
        Log::Warn("No audio hooks are enabled in config");
    } else if (g_config.xaudio2_enabled && !g_hooked_xaudio2_modern.load() && !g_hooked_xaudio2_27.load() && !g_saw_wasapi_activity.load()) {
        Log::Warn("Timed out waiting for XAudio2 or WASAPI activity");
    } else {
        Log::Info("Bootstrap loop ended without additional audio activity");
    }
    return 0;
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t exe_path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        if (wcsstr(exe_path, L"crash_handler.exe") != nullptr) {
            return FALSE;
        }

        DisableThreadLibraryCalls(module);
        g_this_module = module;

        HANDLE thread = CreateThread(nullptr, 0, &MainThread, nullptr, CREATE_SUSPENDED, nullptr);
        if (thread) {
            SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
            ResumeThread(thread);
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        Log::Shutdown();
    }

    return TRUE;
}
