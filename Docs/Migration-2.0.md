# Moving a project from water 1.x to 2.0

2.0 needs an engine with `plRenderPipelineInjections`.

## What you have to do

**Nothing, to keep water on screen.** Scenes, profiles and junction components load as they are.

**Render pipelines.** A pipeline asset that holds `plWaterSurfacePass` and `plWaterUnderwaterPass` nodes keeps
working: the engine adds no passes of its own where they exist. To move to the engine's placement and the
`Water.Quality` cvar, delete the nodes and reconnect what they were wired between. Tools > Water > Inspect Water
Pipeline says which case a project is in.

## What looks different

- **Shallow water is visible.** 1.x faded the whole surface out over Shore Fade Depth along the view ray. 2.0 keeps
  the surface and its reflection and softens only the last centimetres. If a shore now shows more water than you
  want, the water is there: lower it or raise the ground.
- **Colour follows depth, not the view ray.** Shallow Color is reached at Refraction Depth measured straight down. A
  profile tuned by eye from one camera height will be close from that height and more consistent from others. The
  alpha of Shallow Color and Deep Color is no longer used.
- **The sun is shadowed on water**, and its reflection is narrower and brighter up close. Sun Glitter Strength 0.5
  and above is now the full sun; below that it dims.
- **The sea is a spectrum of thousands of waves** where it was eight per cascade. Wave Amplitude now splits evenly
  between the four regular swells and the spectrum. Foam on open water starts at Foam Crest Threshold and lingers.
- **River mouths.** The transition patch, its cutout and the lake current ripples are gone. Rivers and lakes share
  their mouth vertices; Blend Distance is the mix length.
- **A river with a bend tighter than its width is drawn** (pinched in the bend) where 1.x drew nothing.

## For code

- `plWaterRenderData` no longer carries a profile's colours and wave settings. A draw names its profile by index into
  the profile table (`plWaterWorldModule::FillGpuProfileTable`, `Shaders/Water/WaterProfileData.h`).
- `plWaterJunctionRenderSnapshot` describes the mouth (line, shared vertex counts, mix lengths, apron cutout) instead
  of a patch mesh. `CopyJunctionRenderSnapshots` returns every junction; check `m_bValid` and `m_sProblem`.
- `plWaterWorldModule::TryGetBodyRenderInfo` is the cheap way to what a body draws.
- The cvar `Water.Debug.JunctionPatches` modes 1 and 2 are now the profile weight view (mode 1).
- `plWaterPipelineExtension` is gone. So is `Shaders/Water/WaterJunctionFlow.h`.
- River mesh texture coordinates are ribbon coordinates: metres along the river, metres to the left of its middle.
