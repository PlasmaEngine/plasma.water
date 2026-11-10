# Water: setting it up, how it looks, and how bodies join

For people who place water in a scene. What the code does is in the source and in the Readme.

## Getting water on screen

1. Install the package. Nothing else is needed for the render pipeline: the engine adds the water passes to every
   pipeline that has a transparent pass.
2. Add a **Water Body** component to an object. Pick the type:
   - **Ocean**: endless, level, at the object's height. No spline.
   - **Lake**: a closed, flat spline on the same object is the shore.
   - **River**: an open spline is the middle of the river, from where it starts to where it ends. Width, depth and
     flow can change along it with **Water Spline Point** components on the spline's points.
3. Give it a **Profile** (below). Without one it uses the defaults.

If water does not show, the log says why once: the view's pipeline has no place for it (the mobile pipeline has no
transparent pass), or injections are off (`Rendering.Pipeline.Injections`).

`Water.Quality` sets how much is spent: 0 Low (analytic waves, no caustics), 1 Balanced, 2 High, 3 Ultra. Probes and
render targets always get Low. On the test machine caustics were three quarters of what High costs over Low; the wave
spectrum was not measurable.

## Profiles: what the settings do to the picture

A profile is the look and the motion of one kind of water. Bodies share profiles.

**Waves**

| Setting | What it does |
| --- | --- |
| Wave Amplitude | How tall the waves are, in metres. Half of it goes to four regular swells (the ones floating objects follow), half is the standard deviation of the spectrum (the rest of the sea). |
| Base Wavelength | The dominant wave, in metres. Everything scales from it: the spectrum's largest tile is about 25 of these across. |
| Wave Steepness | How far water is pushed towards the crests. Near 0.25 the sea is rounded, near 0.6 crests are sharp and start to break. |
| Spectrum Mode | The shape of the sea: JONSWAP (wind sea with a clear dominant wave), Phillips (broader), Analytic (a plain hump around the base wavelength). |
| GPU Cascade Count | How many scales of waves the spectrum has, 1 to 4. Three is an ocean; a lake needs two and **Enable Lake Spectrum**. Rivers have none. |
| Wind Direction | Where the waves run to. |
| Wind Speed | The pace of the four regular swells. Not metres per second: 1 is their natural speed. |
| Foam Crest Threshold | Foam starts where the surface is squeezed below this (1 is still water). Higher means foam sooner. Foam stays behind the wave that made it and fades over about five seconds. |

**Colour**

There is one model. What you see through the surface is the bed, dimmed by the water above it, plus the water's own
colour for as much of the way as is water.

| Setting | What it does |
| --- | --- |
| Shallow Color | The tint of the water at Refraction Depth: white sand under that much water looks like this colour. Red falls off first in clear water, which is what makes shallows turquoise. |
| Refraction Depth | The depth, in metres, at which Shallow Color is reached. Smaller means murkier. |
| Deep Color | The colour of water with no bed in sight, under white light. |
| Shore Fade Depth | Only softens the last centimetres at the waterline (one twentieth of the value). Shallow water is no longer faded out: it is clear, and it still reflects. |
| Foam Shore Depth | How deep the shore foam reaches. It comes in lines that follow the depth and travel up the beach. |
| Roughness | The surface's own roughness. Waves smaller than a pixel add to it by themselves, so far water is rougher than near water without any setting. |
| Sun Glitter Strength | A dimmer for the sun's reflection. 0.5 and above is the sun as it is. |

Depth is measured straight down, so a shore band keeps its width whatever the camera's pitch. The sun on water, its
glint and the light on foam are shadowed by whatever shades the surface, clouds included.

Alpha of the two colours is not used any more. Absorption and Scattering are what the underwater view uses.

## Joining bodies

Bodies do not blend because they overlap. A **Water Junction** component names two bodies and joins them. Place its
object where they meet.

**River and lake.** The lake's shore keeps the river's two bank points, and the river's mesh ends on exactly the
vertices the lake has between them, at every level of detail. Nothing overlaps. The shore is open across the mouth:
waves are not pinned there. The river's look fades into the lake's on the river's side, and reaches into the lake as
a plume. A lake with three rivers has three plumes.

**River and ocean.** The river's mesh continues as an apron, a fan about two and a half river widths long that carries
the river's look out to sea. Its outer band is pure ocean, so its edge does not show, and rivers always draw after
the ocean, so the mouth looks the same from every side.

A river can have a junction at each end.

| Setting | What it does |
| --- | --- |
| Blend Distance | The mix length in metres: how far the two waters mix across the mouth. The receiving body gets all of it, the river's side 40%. Where a lake drains into a river it is the other way round. With Auto Blend Distance it follows the river's width. |
| Front Breakup | How ragged the front between the two waters is drawn. 0 is a smooth ramp, 1 is torn. |
| Snap Radius | How far the river's end may be from the shore and still join. |
| Elevation Tolerance | How far apart in height the two surfaces may be. A river has to arrive at the lake's level. |

Where a strong current meets waves, the mouth churns: the foam there follows from the river's Flow Speed and the
receiver's waves, not from a setting.

**What is not joined.** Lake to lake and lake to ocean junctions have no mesh yet; gameplay queries blend across
them, the picture does not. Two rivers cannot meet.

**When a junction is left out**, the log says why (the river's end is too far from the shore, not at the lake's
level, wider than the shore, or the end already has a junction), and both bodies are drawn as if it was not there.

## Looking at what the mesh is doing

- `Water.Debug.Stitch`: mouth lines in green where the two meshes share every vertex (with the vertex counts of the
  three levels of detail), red where a junction was left out, and the profile weights on the water itself: a body's
  own water dark blue, and across a mix from magenta (the first profile) to green (the second).
- `Water.Debug.ShowJunctions`, `ShowBodies`, `ShowFlow`, `ShowLod`: anchors, outlines, flow vectors, mesh sizes.
- `Water.Debug.JunctionPatches` 3 and up show one term of the shading instead of the picture. 16 is the sun's shadow
  at the surface, 17 whether the shadow map covers it.

## Things that go wrong

- **A river is missing a piece in a tight bend.** It bends more tightly than it is wide. The log names the body and
  counts the triangles left out. Widen the bend or narrow the river there. (Before 2.0 such a river was not drawn at
  all, and nothing said so.)
- **Reflections near the horizon look like bare ground.** A sky-light probe that captures the scene shows the sea bed
  where the sea is, because water is not in it. The surface asks the probe above the horizon to stay clear of it; a
  probe that only captures the sky has no such problem.
- **A floating object sits wrong on a stormy sea.** Gameplay follows the four regular swells, not the spectrum. The
  spectrum adds at most about four times its standard deviation (twice the Wave Amplitude) on top.
