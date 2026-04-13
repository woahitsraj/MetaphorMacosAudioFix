# Wine / CrossOver Spatial Audio Notes

This is the write-up to attach to a Wine issue or send to CodeWeavers.

## Symptom

On CrossOver/macOS, `Metaphor: ReFantazio` produces normal music and effects but extremely faint, hollow, or echo-like dialogue.

The issue is reproducible on stereo output devices such as `MacBook Pro Speakers`.

## What The Game Is Doing

The game does not rely only on the normal stereo `IAudioClient::GetMixFormat()` path.

In the affected setup it:

- queries a normal stereo shared-mode output stream
- also activates `ISpatialAudioClient`
- creates a shared-mode `12-channel` float render stream with channel mask `0x2d63f`

That mask expands to:

- `FrontLeft`
- `FrontRight`
- `FrontCenter`
- `LowFrequency`
- `BackLeft`
- `BackRight`
- `SideLeft`
- `SideRight`
- `TopFrontLeft`
- `TopFrontRight`
- `TopBackLeft`
- `TopBackRight`

So the game is effectively asking for a `7.1.4`-style static object bed.

## What CrossOver Advertises

The ordinary MMDevice mix format exposed by CrossOver for `MacBook Pro Speakers` is already stereo:

- `channels=2`
- `mask=0x3`
- `sample_rate=48000`
- `bits=32`

So the bug is not explained by `GetMixFormat()` alone reporting a multichannel speaker setup.

## What Failed

Game-side shims that:

- forced `XAudio2` mastering voices to stereo
- forced multichannel `IAudioClient` formats to stereo
- rejected multichannel `IAudioClient::Initialize`
- blocked `ISpatialAudioClient`

either had no effect, prevented launch, or removed audio entirely.

That strongly suggests the game expects the spatial API path to exist and remain functional.

## Working Mitigation

A plugin-side replacement of Wine's spatial path fixed the issue by:

- letting the game keep using `ISpatialAudioClient`
- exposing the expected mono object format to the game
- intercepting `ActivateSpatialAudioStream`
- creating a normal stereo `IAudioClient` output stream internally
- explicitly mixing static spatial objects down to stereo in the plugin

With that wrapper active, dialogue becomes normal again.

## Likely Root Cause

Wine/CrossOver currently appears to flatten the game's static spatial objects into a raw `12-channel` PCM stream and hand that stream to the regular shared-mode output path.

On macOS stereo playback, that leaves important dialogue-heavy channels, especially `FrontCenter`, in a translation path that does not downmix correctly for this title.

The practical result is:

- center/dialogue content is severely attenuated or misrouted
- music/effects remain comparatively normal

## Candidate Fix Direction In Wine / CrossOver

The likely fix belongs in Wine's spatial audio implementation rather than in a per-game patch.

Areas to inspect:

- `dlls/mmdevapi/spatialaudio.c`
- the object-to-bed mixing logic used for static objects
- the handoff from that mixed bed into shared-mode `IAudioClient`
- any missing or incorrect stereo downmix behavior for `FrontCenter`, `LFE`, and height channels when the real endpoint is stereo

The plugin workaround suggests that a correct explicit downmix matrix for the static object bed is sufficient to resolve the issue.

