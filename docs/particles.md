
# Snow Particle System

## Overview

A particle system was implemented to simulate continuous snowfall in the Penguin Snowdown arena.

The system uses 500 particles, each represented by an individual position, velocity and size.

## Particle Initialization

Particles are initialized at random positions above and around the arena.

Each particle is assigned:
- A random starting position.
- A downward velocity with slight horizontal movement.
- A randomized size to create visual variation.

## Particle Simulation

Particle positions are updated every frame using Euler integration:

p(t + dt) = p(t) + v * dt

The velocity remains constant during each particle's lifetime.

When a particle reaches the ground, it is repositioned above the arena. This allows the snowfall to continue without repeatedly allocating new particles.

## Particle Rendering

The particles are rendered using the existing static shader and sphere model.

Each particle has an individual model matrix containing translation and scaling.

The particles are rendered in white to represent snowflakes.

## Result

The particle system produces continuous snowfall across the arena, with variation in particle size, falling speed and horizontal movement.
