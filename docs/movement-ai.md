# Movement AI – Development Notes

## 1. Objective

Extend the existing finite state machine (FSM) to allow autonomous penguins to move around the ice rink while remaining inside its boundaries.

Movement will use a Seek Behaviour to guide each penguin toward a selected destination.

## 2. Existing implementation

The current application contains three FSM states: `IDLE`, `AIM`, and `THROW`.

Each penguin already stores its world position and orientation. The existing `turnTowards()` function provides gradual rotation toward a target angle.

The application also uses delta time (`dt`) to update animation and simulation.

## 3. Planned implementation

- Add a destination to each penguin.
- Define valid movement boundaries within the ice rink.
- Implement Seek Behaviour.
- Add a `MOVE` state to the FSM.
- Connect the movement state to a looping Blender animation.

## 4. Implementation log

### Step 1 – Destination

Added `vec3 destination` to the `Penguin` structure.

This variable will store the world-space destination used by the movement algorithm.

**Status:** Implemented and compiled successfully. Movement behaviour not yet tested.

### Step 2 – Ice rink boundaries

Defined the ice rink boundaries using the dimensions documented
in the asset notes.

Implemented `clampDestination()` to restrict movement destinations
to the valid area of the ice rink.

A safety margin of 0.5 metres is used to account for the
penguin's body size.

**Status:** Implemented and compiled successfully. Boundary behaviour not yet tested at runtime.

### Step 3 – Random destinations

Implemented `randomDestination()` to generate random target
positions within the ice rink.

The generated positions are passed through `clampDestination()`
to ensure they remain inside the permitted movement area.

**Status:** Implemented and compiled successfully. Random destination generation not yet tested at runtime.

### Step 4 – Seek Behaviour

Implemented `updateSeek()` to move a penguin towards its destination.

The direction is calculated using the difference between the
destination and the current position in the XZ-plane.

The penguin rotates towards its destination using the existing
`turnTowards()` function.

Movement uses a constant speed multiplied by delta time (`dt`)
to provide frame-rate-independent displacement.

An arrival radius of 0.1 metres is used to determine when
the destination has been reached.

**Status:** Implemented and compiled successfully. Seek Behaviour not yet tested at runtime.

### Step 5 – MOVE state

Extended the existing finite state machine by adding a new
`MOVE` state.

Updated the state name array to include the new state.

The movement state will later call `updateSeek()` to move
penguins towards their destinations.

**Status:** Implemented and compiled successfully. FSM integration not yet tested.

### Step 6 – FSM integration

Integrated the MOVE state into the existing finite state machine.

When entering MOVE, the penguin receives a random destination
within the ice rink.

The `updateSeek()` function is called each frame to move the
penguin towards its destination.

Once the destination is reached, the penguin transitions
to AIM and continues the existing throwing behaviour.

The Idle animation is temporarily used during movement
until a dedicated Move animation is available.

**Status:** Awaiting compilation and runtime testing.

## 5. Testing and results

Not tested yet.

## 6. Future improvements

- Movement animation created in Blender.
- Avoidance between penguins.
- More advanced movement decisions.