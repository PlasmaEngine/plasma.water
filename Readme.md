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
  EditorPluginWater/    editor                -> plEditorPluginWater  (Qt: profile editor, actions)
Configs/                build rules for building against an installed engine SDK
Branding/               icon and banner
```

## What ships with the engine, not here

The water shaders (`Data/Base/Shaders/Water/`, `WaterSurface.plShader`,
`UnderwaterPostProcess.plShader`), the default Ocean/River/Lake profiles under `Data/Plugins/Water/`,
and the editor string table all stay in the engine.

They stay there because `Mount` is declared in package manifests but nothing consumes it yet, so
content shipped inside a package is not reachable at run time. Water without its shaders is not
water, so relocating them waits until mounts are wired up. Installing this package needs nothing
further.

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
and palette come from `Source/EditorPluginWater/QtResources/Water.svg`, which is what the component
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
