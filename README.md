# Penguin Snowdown

TSBK03 Advanced Game Programming, final project (LiU).
Aya Sinjab & Adna Maric

Autonomous penguins in a winter arena: an FSM (Idle -> Aim -> Throw) controls
skinned, animated penguins that throw snowballs with ballistic physics.

## Structure
- `game/`    C++/OpenGL code (MicroGlut, VectorUtils4, cgltf), CMake build
- `game/assets/`  exported models (penguin.glb, arena.glb, sphere.obj)
- `blender/` source .blend files (penguin.blend = rigged + animated, arena.blend = exported arena)
- `docs/`    project spec and notes

## Build (Windows, MSYS2 ucrt64 GCC + CMake, same as the TSBK03 labs)
1. Open the `game` folder in VS Code
2. CMake: Configure (choose the GCC kit), then Build
3. Run `penguin_snowdown.exe` from the `game` folder