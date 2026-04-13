# MetaphorAudioFix

`MetaphorAudioFix` is a Windows ASI plugin that fixes the faint, hollow, echo-like dialogue bug in `Metaphor: ReFantazio` on Wine and CrossOver.

The game uses `ISpatialAudioClient` and builds a `12-channel` static object bed even when the real endpoint is stereo. On CrossOver/macOS that path leaves dialogue-heavy channels mixed incorrectly. This plugin keeps the spatial API alive for the game, intercepts `ActivateSpatialAudioStream`, and explicitly mixes the static spatial objects down to stereo before they hit the real output stream.

## Status

This repo contains the working fix that was validated in-game on CrossOver for macOS with the dialogue issue reproduced beforehand.

## Repository Layout

- `src/`: plugin source
- `external-minhook/`: vendored MinHook dependency
- `scripts/package-release.sh`: creates a GitHub-friendly zip from the built package
- `.github/workflows/release.yml`: builds and publishes release zips on tags
- `docs/WINE_SPATIAL_AUDIO_NOTES.md`: root-cause write-up for Wine or CodeWeavers

## Build

Install a Windows cross-compiler toolchain such as `mingw-w64`, then run:

```bash
./build-windows.sh
```

That produces:

- `build/windows/package/MetaphorAudioFix.asi`
- `build/windows/package/MetaphorAudioFix.ini`
- `build/windows/package/libwinpthread-1.dll`

## Install

1. Put an ASI loader `winmm.dll` in the game directory. You can use Ultimate ASI Loader or the loader that comes with other Lyall-style fixes.
2. Copy `MetaphorAudioFix.asi`, `MetaphorAudioFix.ini`, and `libwinpthread-1.dll` into the same directory as `METAPHOR.exe`.
3. On Wine or CrossOver, make sure `winmm` is loaded as native first.

Steam launch option form:

```text
WINEDLLOVERRIDES="winmm=n,b" %command%
```

On CrossOver you can also set the equivalent per-app DLL override in the bottle registry.

## Release Zip

After building, create a distributable zip with:

```bash
./scripts/package-release.sh v0.1.0
```

That creates:

- `dist/MetaphorAudioFix-v0.1.0-win64.zip`
- `dist/MetaphorAudioFix-v0.1.0-win64.zip.sha256`

## GitHub Releases

The included GitHub Actions workflow supports two paths:

- push a tag like `v0.1.0`
- run the `Release` workflow manually and provide a version string

The workflow:

- installs `mingw-w64`
- builds the plugin
- packages the zip
- uploads the zip as a workflow artifact
- publishes a GitHub release automatically for tag pushes

## Logging

When the plugin is active, it writes `MetaphorAudioFix.log` next to the game executable.

Useful lines include:

- `Returning wrapped ISpatialAudioClient`
- `Spatial wrapper ActivateSpatialAudioStream`
- `Spatial wrapper initializing stereo stream`

## Root Cause Notes

See [`docs/WINE_SPATIAL_AUDIO_NOTES.md`](docs/WINE_SPATIAL_AUDIO_NOTES.md) for the issue write-up you can attach to a Wine bug or send to CodeWeavers.

## License

This project is under the MIT license. `MinHook` remains under its own license in `external-minhook/`.
