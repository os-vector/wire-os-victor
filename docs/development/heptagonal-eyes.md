# Heptagonal eyes

This build uses a seven-sided procedural eye outline by default: a flat top,
deeply clipped upper corners, vertical sides, and a pointed base. The outline
keeps the original eye dimensions and spacing.
Existing eyelids, blinks, eye movement, transforms, lighting, and color overlays
still use the normal rendering pipeline. Animated corner-radius values apply
only to the original rounded style.

The style applies to every procedural eye keyframe, including the eyes during
emotional reactions and idle animations. Eyelid motion and curvature, eye
position, rotation, scaling, and blink timing continue to animate. Expressions
that rely on changing corner radii instead retain the fixed seven-sided
outline, so their silhouettes differ from the original design. Individual
frames can show fewer visible sides when the eyelids cover part of an eye or
the eyes close.

Sprite and image artwork, such as full-screen icons or an SDK-provided face
image, is not reshaped. Animations that combine artwork with procedural eyes
show the new shape on their eye layer. Body motion and audio are unchanged.

Build and deploy the updated animation process using the repository's normal
build and deployment instructions. On that build, `Face.ParameterizedFace` has
a `ProcFace_EyeShape` console setting:

- `0`: original rounded outlines
- `1`: heptagonal outlines (default)

The console variable is declared as `kProcFace_EyeShape`; the remote console
exposes it as `ProcFace_EyeShape`. Changes redraw the eyes immediately, even
while the face is stationary.

To save a choice across reboots, use the existing `consoleVarsAnim.ini` file in
the robot's persistent directory and preserve its other settings:

```ini
[Face.ParameterizedFace]
ProcFace_EyeShape = 1
```

An older firmware build does not have this setting. Rocket-Pod's existing eye
overlay controls change the color texture; the new outline requires this
updated renderer.
