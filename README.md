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
- `build/windows/package/winmm.dll`
- `build/windows/package/Ultimate-ASI-Loader-LICENSE.txt`

## Install

The packaged fix now includes a bundled `winmm.dll` ASI loader, so all required files ship together.

1. Open the game install directory in Steam:
   `Metaphor: ReFantazio` -> `Properties...` -> `Installed Files` -> `Browse...`
2. Copy these four files into the same directory as `METAPHOR.exe`:
   - `winmm.dll`
   - `MetaphorAudioFix.asi`
   - `MetaphorAudioFix.ini`
   - `libwinpthread-1.dll`
3. Make sure Wine/CrossOver loads `winmm` as `native,builtin`.

### Steam DLL Override

If you launch the game through Steam inside Wine, Proton, or a CrossOver Steam bottle, set the DLL override directly on the game in Steam:

1. In Steam, right-click `Metaphor: ReFantazio`.
2. Click `Properties...`.
3. Stay on the `General` tab.
4. Find the `Launch Options` box near the bottom.
5. Paste this exactly:

```text
WINEDLLOVERRIDES="winmm=n,b" %command%
```

6. Close the Properties window.
7. Launch the game from Steam normally.

`n,b` is Wine shorthand for `native,builtin`, which makes Wine try the bundled `winmm.dll` in the game folder first and fall back to Wine's builtin `winmm` only if needed.

### Bottle Registry Override

If you prefer to set the override in the bottle itself instead of using a Steam launch option, add the DLL override in the bottle registry:

1. Open CrossOver.
2. Select the bottle that contains Steam and `Metaphor: ReFantazio`.
3. Choose `Run Command...` for that bottle.
4. Run `regedit`.
5. In Registry Editor, go to:

```text
HKEY_CURRENT_USER\Software\Wine\DllOverrides
```

6. If `DllOverrides` does not exist, create it:
   - Right-click `Wine`
   - `New` -> `Key`
   - Name it `DllOverrides`
7. With `DllOverrides` selected, create a new string value:
   - Right-click in the right pane
   - `New` -> `String Value`
   - Name: `winmm`
8. Double-click the new `winmm` value and set its data to:

```text
native,builtin
```

9. Close Registry Editor.
10. Fully quit Steam inside the bottle, then start Steam again before launching the game.

If you prefer the command-line form, the same registry override can be created with:

```text
reg add "HKEY_CURRENT_USER\Software\Wine\DllOverrides" /v winmm /t REG_SZ /d native,builtin /f
```

Do not set both a conflicting registry override and a conflicting Steam launch option. If both exist with the same `native,builtin` value, that is fine.

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

## Root Cause Notes

See [`docs/WINE_SPATIAL_AUDIO_NOTES.md`](docs/WINE_SPATIAL_AUDIO_NOTES.md) for the issue write-up you can attach to a Wine bug or send to CodeWeavers.

## License

This project is under the MIT license. `MinHook` remains under its own license in `external-minhook/`. The bundled `winmm.dll` comes from [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), which is also MIT licensed; its license text is included as `Ultimate-ASI-Loader-LICENSE.txt`.
