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

## Where to read more

- [`Docs/Water.md`](Docs/Water.md): placing water, what the profile settings do to the picture, joining bodies.
- [`Docs/Migration-2.0.md`](Docs/Migration-2.0.md): what changes for a project that used 1.x.

## How it is put together

- **Profiles by index.** Every body's profile is uploaded once per frame and view as one table
  (`Shaders/Water/WaterProfileData.h`). A draw names its entry; the vertices of a river or lake name the pair of
  entries they blend and a weight, so one mesh can carry any number of looks.
- **Networks.** Rivers and lakes that junctions join are meshed together (`Utils/WaterNetworkMesher`): they share
  the vertices of every mouth line at every level of detail. A river that reaches the ocean continues as an apron.
  Each body draws its own part.
- **Waves.** Four deterministic swells that gameplay queries follow, plus a spectrum of N x N waves per cascade,
  transformed on the GPU (`Shaders/Water/WaterSpectrum*.plShader`). Vertices read it at the scale of the mesh; pixels
  add the rest as normals and roughness.
- **Light.** One optical model on vertical depth; the sun is shadowed with the engine's shadow map at the surface.

## Tests

`plasmabuild build WaterTest` builds `Bin/Tests/<platform>/WaterTest`, which runs against the SDK named by
`PLASMA_SDK_ROOT`.

- **Water**: geometry, the network mesher (shared mouth vertices and no T-junction at any level of detail, open
  mouths, plumes, aprons, rivers that fold, draw order at a mouth), ring seams, spectrum normalisation, profiles,
  quality tiers. No GPU needed.
- **Water Rendering Tests**: image checks on a hand-built pipeline.
- **Water Gallery**: scenes built in code and drawn through the project's main pipeline as shipped (water is only
  there through the injection). Saves fixed shots to `<output>/Gallery/` and a frame-time table per quality tier.
  Pictures are the same from run to run, so two runs can be compared pixel by pixel.

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
Output lands in `Bin/<Platform>_x64_<Configuration>/`.

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
