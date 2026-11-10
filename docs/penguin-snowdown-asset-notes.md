# Penguin Snowdown – asset facts to use in code

Collected while preparing the Blender assets (Nov 2026). Values the code must account for.

## Files
- `penguin_v2.glb` – one skinned mesh, 2 primitives (body material + eye material), share one skin. Loop over ALL primitives.
- `arena.glb` – static environment, ~50k triangles, ~72 nodes/draw calls, 6 materials, no vertex colours (use `baseColorFactor`). Loader must walk the node tree and multiply each node's world matrix (transforms are not applied in Blender; some nodes have negative scale → check winding/cull per node determinant).

## Scale
- Penguin is exported unscaled, ≈3.6 m tall (verify Dimensions in Penguin.blend).
- Intended size in the arena: scale **0.413** (≈1.5 m tall), as chosen in the arena file.
- Apply in code: `penguinModel = T(pos) × R(yaw) × S(0.413)`, used as parent matrix for the Root joint. Snowball spawn (Flipper.R joint world pos) goes through the same matrix.
- Collision radii for penguins must use the scaled size.

## Ground height
- Ice surface top: Blender Z = 0.05 → **y = 0.05** in OpenGL (glTF is Y-up).
- Snow base top: y = −0.05. Snow rim reaches y ≈ 0.79.
- Place penguin feet (origin) at y = 0.05; snowball hits ground when `p.y − r < 0.05`.
- Ice rink extent (Blender X/Y): x ∈ [−10, 10], y ∈ [−7, 7] → OpenGL x ∈ [−10, 10], z ∈ [−7, 7] (glTF z = −Blender y).

## Coordinates
- Blender (x, y, z) → glTF (x, z, −y). Penguin faces −Y in Blender → **+Z** in program. Yaw toward target: `atan2(dx, dz)`.

## Animation
- Clips looked up by name: `Idle` (2.0 s, loop), `Aim` (≈0.8 s, one-shot, hold last frame), `Throw` (23 frames ≈0.96 s, one-shot).
- Throw release: `(6 − 1)/24 ≈ 0.208 s` after Throw's first keyframe time; detect crossing, not equality.
- Skinned mesh: ignore mesh node transform (NODE_SKINNED_MESH_NON_ROOT warning is expected).
