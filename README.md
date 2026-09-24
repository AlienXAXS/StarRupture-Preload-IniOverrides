# IniOverrides (StarRupture preload plugin)

Makes StarRupture read your `Engine.ini`, `Game.ini`, `Input.ini`, `Scalability.ini` and the rest
from its `Saved\Config` folder, on top of the config baked into the game -- the way most UE5 games
already do.

There are no settings. If the DLL is in `ModLoader\Preload\`, the patch is applied. Remove the DLL
to go back to stock behaviour.

## Install

Requires the [StarRupture ModLoader](https://github.com/AlienXAXS/StarRupture-ModLoader) with
preload plugin support.

Grab `IniOverrides-Client-<version>.zip` or `IniOverrides-Server-<version>.zip` from
[Releases](https://github.com/AlienXAXS/StarRupture-Preload-IniOverrides/releases/latest) and
extract it into `Binaries\Win64\ModLoader\` -- it contains `Preload\IniOverrides-<Client|Server>.dll`.

| Build | DLL goes in |
|---|---|
| Client | `<game>\StarRupture\Binaries\Win64\ModLoader\Preload\` |
| Server | `<server>\StarRupture\Binaries\Win64\ModLoader\Preload\` |

The file name does not matter to the loader, but keep only one copy in the folder.

## Where your INI files go

| Game | Folder |
|---|---|
| Client | `%LOCALAPPDATA%\StarRupture\Saved\Config\Windows\` |
| Dedicated server | `<server>\StarRupture\Saved\Config\WindowsServer\` |

The file name is the config name: `Engine.ini`, `Game.ini`, `Input.ini`, `Scalability.ini`,
`DeviceProfiles.ini`, `GameplayTags.ini`, `Hardware.ini`, `RuntimeOptions.ini`, `InstallBundle.ini`.
The syntax is ordinary UE config syntax, including `+Key=` / `-Key=` array operators:

```ini
; Engine.ini
[SystemSettings]
r.SomeCVar=1
```

**The game does not write these files**, with or without the plugin. Only `GameUserSettings.ini` is
ever saved by the game. Anything you put in the others stays exactly as you wrote it.

If you already have files in that folder from earlier experiments, the game will start applying
them as soon as the plugin is installed.

## Checking it worked

`ModLoader\Logs\modloader.log` shows:

```
[Plugin:IniOverrides] patched FConfigContext::PerformLoad at exe+0x...: 44 88 B7 81 01 00 00 -> C6 87 81 01 00 00 01
[Plugin:IniOverrides] user INI files in ... will now be applied on top of the baked config; ...
```

The `preload` console command lists it as `running`. Launching the game with `-dumpiniloads`
additionally makes the engine log each saved-layer file it finds.

After a game update that moves this code, the pattern stops matching. The mod loader then refuses
the plugin, reports it in the hook failure window / `hookfailures`, and the game starts normally
without it.

## How it works

StarRupture loads its configuration from a baked `Config/BinaryConfig.ini`. After loading it, the
engine goes through every config file again for one purpose: to apply the user's saved-layer file
`Saved\Config\<Platform>\<Name>.ini` on top.

The game was built with `DISABLE_GENERATED_INI_WHEN_COOKED`, so `FConfigContext::PerformLoad`
clears `bAllowGeneratedIniWhenCooked` for every file except `GameUserSettings`. With that flag
cleared, the saved layer is never read.

The plugin rewrites that one store so it sets the flag instead:

```
44 88 B7 81 01 00 00    mov [rdi+181h], r14b         ; bAllowGeneratedIniWhenCooked = 0
C6 87 81 01 00 00 01    mov byte ptr [rdi+181h], 1   ; bAllowGeneratedIniWhenCooked = 1
```

Both instructions are the same length. The `NoSave` flag set right after it is left untouched, and
that flag is why the game still never writes these files.

This has to be a preload plugin because the config system initialises in `FEngineLoop::PreInit`,
long before ordinary plugins load. Preload runs before the game's entry point.

The same pattern matches once in both the client and the dedicated server executables. The only
wildcard is the `NoSave` field offset, which differs between the two (`0x4A0` / `0x4C0`).

## Building

Open `IniOverrides.sln` and build `Client Release` or `Server Release`. Output goes to
`build\<Configuration>\Preload\IniOverrides.dll`.

Releases are built by the manually run **Build and Release** workflow (Actions tab). It builds both
configurations, bumps the minor version from the latest `vX.Y.Z` release (or uses a higher one from
an optional `version` file), and publishes the two ZIPs plus the bare suffixed DLLs.

`include\` is a copy of the headers from
[StarRupture-PreLoadPlugin-SDK](https://github.com/AlienXAXS/StarRupture-PreLoadPlugin-SDK).
