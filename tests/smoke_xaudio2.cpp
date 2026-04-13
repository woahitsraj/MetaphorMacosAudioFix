#include <windows.h>

#include <audioclient.h>
#include <xaudio2.h>

#include <cstdio>

namespace {
using XAudio2CreateFn = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);

void PrintHr(const char* label, HRESULT hr)
{
    std::printf("%s: 0x%08lX\n", label, static_cast<unsigned long>(hr));
}

int RunCase(const char* label, UINT32 requested_channels)
{
    HMODULE xaudio_module = LoadLibraryW(L"xaudio2_9.dll");
    if (!xaudio_module) {
        std::printf("Failed to load xaudio2_9.dll: %lu\n", GetLastError());
        return 1;
    }

    auto xaudio2_create = reinterpret_cast<XAudio2CreateFn>(GetProcAddress(xaudio_module, "XAudio2Create"));
    if (!xaudio2_create) {
        std::puts("Failed to locate XAudio2Create");
        return 1;
    }

    IXAudio2* engine = nullptr;
    HRESULT hr = xaudio2_create(&engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
    PrintHr("XAudio2Create", hr);
    if (FAILED(hr) || !engine) {
        return 1;
    }

    IXAudio2MasteringVoice* mastering_voice = nullptr;
    hr = engine->CreateMasteringVoice(
        &mastering_voice,
        requested_channels,
        XAUDIO2_DEFAULT_SAMPLERATE,
        0,
        nullptr,
        nullptr,
        AudioCategory_GameEffects
    );
    std::printf("%s requested_channels=%u\n", label, requested_channels);
    PrintHr("CreateMasteringVoice", hr);

    if (SUCCEEDED(hr) && mastering_voice) {
        XAUDIO2_VOICE_DETAILS details{};
        mastering_voice->GetVoiceDetails(&details);

        DWORD channel_mask = 0;
        const HRESULT mask_hr = mastering_voice->GetChannelMask(&channel_mask);

        std::printf("actual_channels=%u mask_hr=0x%08lX channel_mask=0x%08lX\n",
                    details.InputChannels,
                    static_cast<unsigned long>(mask_hr),
                    static_cast<unsigned long>(channel_mask));

        mastering_voice->DestroyVoice();
    }

    engine->Release();
    return SUCCEEDED(hr) ? 0 : 1;
}
}

int main()
{
    std::puts("smoke_xaudio2 starting");

    HMODULE plugin = LoadLibraryW(L"MetaphorAudioFix.asi");
    if (!plugin) {
        std::printf("Failed to load MetaphorAudioFix.asi: %lu\n", GetLastError());
        return 1;
    }

    Sleep(500);

    if (RunCase("default", XAUDIO2_DEFAULT_CHANNELS) != 0) {
        return 1;
    }

    if (RunCase("explicit-6ch", 6) != 0) {
        return 1;
    }

    std::puts("smoke_xaudio2 completed");
    return 0;
}
