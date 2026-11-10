# plasma.water

Profile-driven water for PlasmaEngine — oceans, rivers and lakes, with junctions, wave interaction,
buoyancy hooks and underwater rendering. Moved out of the engine tree to be built and distributed as
a package.

Published as [`plasma.water`](https://plasmaengine.github.io/PlasmaPackages/index.plPackageIndex) in
the Plasma package registry.

## Layout

```
Package.plPackage       the manifest - identity, compatibility, components
Water.plPluginBundle    legacy descriptor, kept until the engine reads manifests directly
Source/
  WaterPlugin/          runtime               -> plWaterPlugin
  EnginePluginWater/    editor engine process -> plEnginePluginWater
  EditorPluginWater/    editor                -> plEditorPluginWater  (profile editor, actions)
EditorIcons/            editor icons, registered as an icon folder by the editor plugin
Configs/                build rules for building against an installed engine SDK
Branding/               icon and banner
```

## How water gets into a render pipeline

Nobody edits a pipeline asset. When the plugin starts it registers two pipeline injections with the engine
(`plRenderPipelineInjections`, in `Source/WaterPlugin/Startup.cpp`):

| Injection | Point | What it does |
| --- | --- | --- |
| `plasma.water/Surface` | `BeforeTransparent` | `plWaterSurfacePass`: wave simulation, caustics and the surface itself, in front of the transparent pass. Adds `plWaterFrameDataExtractor`. |
| `plasma.water/Underwater` | `AfterTransparentResolved` | `plWaterUnderwaterPass`: fog, distortion and light shafts while the camera is under water. |

The engine places the passes in every pipeline that has a transparent pass, undoes the wiring if it does not hold,
and bypasses a pass that fails to build, so the rest of the frame still renders. Pipeline assets never name a water
type, so they load without this package.

- **No water, no cost.** Water bodies report `PL_WATER_CONTENT_FLAG_SURFACES` while they are extracted. Without it
  both passes hand their inputs on and create nothing.
- **Quality.** The cvar `Water.Quality` (0 Low, 1 Balanced, 2 High, 3 Ultra) sets the tier. Reflection and
  render-target views are held at Low.
- **Hand-placed passes win.** A pipeline whose asset already contains a `plWaterSurfacePass` keeps it, with the
  Quality property of its node, and gets no injection.
- **Switching it off.** Add `plasma.water/Surface;plasma.water/Underwater` to the cvar
  `Rendering.Pipeline.Injections.Disabled`, or set `Rendering.Pipeline.Injections` to false for every package.
- **Pipelines without a transparent pass** (the mobile pipeline) get no water. The log says so once per pipeline.

This needs an engine with `plRenderPipelineInjections`. On an older engine the plugin does not build.

## What comes from the engine, not from here

The shaders (`Assets/Shaders/Water/`), their permutation variables (`Assets/Shaders/PermutationVars/`) and the
default Ocean, River and Lake profiles (`Assets/Water/DefaultProfiles/`) ship in this package and are mounted as the
data directory `plasma.water`.

Still taken from the engine's `Data/Base`:

- the default normal map and foam texture the profiles reference (`Textures/WaterBump`, `Textures/Foam`),
- the constant-buffer and lighting headers the water shaders include,
- the editor string table (`Data/Tools/plEditor/Localization/en/WaterPlugin.txt`).

The engine also still carries the legacy `Materials/WaterSurface.plShader`, `Pipeline/UnderwaterPostProcess.plShader`
and its own copies of the `WATER_*` permutation variables. They belong to the deprecated `plOceanComponent` and
`plUnderwaterPass` and can go when those do.

## Dependencies

None resolvable through the registry. It binds to GameEngine and RendererCore, both part of the
engine.

`WaterTerrainBridge` declares `RequiredPlugins{"Water, Terrain"}` and stays in the engine for now. It
reaches water purely through reflection — `IsType(..., "plWaterBodyComponent")` — and includes no
water header, so its build-time import of `plWaterPlugin` was removed when water moved out. It needs
this package installed to do anything, which is what `RequiredPlugins` expresses.

## Building

Requires an engine SDK — a built engine checkout — named by `PLASMA_SDK_ROOT`:

```bash
PLASMA_SDK_ROOT=/path/to/engine plasmabuild build EditorPluginWater
```

`EnginePluginWater` is a separate target and is not pulled in by the editor plugin, so build it too.
Output lands in `Bin/Windows_x64_Development/`.

## Artwork

The bundle shipped with `Icon{""}` and `Banner{""}`, so both were drawn for the package. The motif
and palette come from `EditorIcons/WaterPlugin/Water.svg`, which is what the component
icons already use, so the package art and the in-editor iconography agree. They are generated, not
hand-painted — regenerate them by rerunning the renderer if the palette ever changes.

## Publishing a new version

Staging and publishing are driven from the engine and registry repositories:

```bash
# engine repo - stage and validate, emitting machine-readable output
python Utilities/PackageInspector.py <staged-package-dir> --json <out>.json

# registry repo - upload blobs and record the release
python tools/publish_package.py <out>.json
```

Versions are immutable. A mistake is fixed by publishing a new patch version, never by replacing an
existing one — every lock file that already trusts a version must keep resolving to the same bytes.
