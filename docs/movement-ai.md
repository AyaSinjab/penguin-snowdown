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

#### Step 7.4 – Static obstacle collision prevention

Updated `updateSeek()` to check the penguin's next position
before applying movement.

The proposed position is tested using `collidesWithObstacle()`.

If a collision is detected, the movement is rejected.
Otherwise, the penguin moves towards its destination.

This prevents movement into static obstacles under normal
movement conditions.

Obstacle avoidance and pathfinding are not yet implemented.

**Status:** Implemented, compiled and tested at runtime.
Penguins stop when their next movement would collide with
a static obstacle. However, they remain stuck because
the current Seek Behaviour does not calculate an alternative
path around obstacles.

#### Step 7.5 – Local Obstacle Avoidance

Extended `updateSeek()` with a simple local steering behaviour.

When the direct movement towards the destination is blocked,
the penguin attempts to move sideways.

Two perpendicular directions are tested, and the first
collision-free position is selected.

If all proposed positions are blocked, the penguin remains
stationary.

This is an initial obstacle avoidance implementation.
It does not guarantee successful navigation around obstacles.

**Status:** Awaiting compilation and runtime testing.

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

**Status:** Implemented, compiled and successfully tested at runtime.
Penguins autonomously move towards random destinations within the
ice rink and continue aiming and throwing snowballs after reaching
their destinations. Collision avoidance is not yet implemented.

### Step 7 – Obstacle Avoidance

#### Step 7.1 – Obstacle representation

Created an `Obstacle` structure containing a position and
a collision radius.

Static obstacles will be represented as circles in the XZ-plane
to simplify collision detection and avoidance.

**Status:** Implemented and compiled successfully.
Obstacle representation has not yet been tested at runtime.

#### Step 7.2 – Static obstacles

Added a list of static obstacles representing trees and
snowmen in the ice rink.

Obstacle positions are based on the arena model.
Each obstacle is represented by a circular collision area
in the XZ-plane.

Initial collision radii:
- Trees: 0.8 metres
- Snowmen: 0.7 metres

These radii may be adjusted during runtime testing.

**Status:** Implemented and compiled successfully.
Obstacle positions and collision radii have not yet been validated at runtime.

#### Step 7.3 – Static obstacle collision detection

Implemented `collidesWithObstacle()` to detect collisions
between penguins and static obstacles.

Collision detection uses circular collision areas in the
XZ-plane.

The distance between the penguin and each obstacle is
compared against the sum of their collision radii.

A penguin collision radius of 0.4 metres is currently used.

The function returns true when a collision is detected.

**Status:** Implemented and compiled successfully.
Collision detection has not yet been tested at runtime.

#### Step 7.4 – Static obstacle collision prevention

Updated `updateSeek()` to check the penguin's next position
before applying movement.

The proposed position is tested using `collidesWithObstacle()`.

If a collision is detected, the movement is rejected.
Otherwise, the penguin moves towards its destination.

This prevents movement into static obstacles under normal
movement conditions.

Obstacle avoidance and pathfinding are not yet implemented.

**Status:** Implemented, compiled and tested at runtime.
Penguins stop when their next movement would collide with
a static obstacle. However, they remain stuck because
the current Seek Behaviour does not calculate an alternative
path around obstacles.

#### Step 7.5 – Local Obstacle Avoidance

Extended `updateSeek()` with a simple local steering behaviour.

When the direct movement towards the destination is blocked,
the penguin attempts to move sideways.

Two perpendicular directions are tested, and the first
collision-free position is selected.

If all proposed positions are blocked, the penguin remains
stationary.

This is an initial obstacle avoidance implementation.
It does not guarantee successful navigation around obstacles.

**Status:** Implemented, compiled and tested at runtime.
Penguins successfully navigate past some static obstacles
using local sideways movement. However, oscillating movement
is still observed near obstacles, resulting in unnatural
movement. Further improvements are needed.

#### Step 7.6 – Persistent Obstacle Avoidance

Added variables to the Penguin structure to support
persistent obstacle avoidance:

- avoidanceSide: Stores the chosen avoidance direction.
- avoidanceObstacle: Stores the index of the obstacle.
- avoidanceWaypoint: Stores a temporary navigation target.

Runtime testing of the previous local steering approach
showed that some penguins could navigate around obstacles,
while others experienced jitter or became stuck.

The new variables will be used to maintain a consistent
avoidance path around obstacles.

**Status:** Implemented and compiled successfully.
The avoidance variables have been added to the Penguin structure,
but waypoint-based obstacle avoidance has not yet been integrated
or tested at runtime.

#### Step 7.7 – Path obstruction detection

Implemented `isPathBlocked()` to determine whether a
straight path intersects a static obstacle.

The function calculates the closest point on the path
segment to each obstacle and compares the distance
against the combined collision radii and a safety margin.

The index of the blocking obstacle is returned through
an output parameter.

This function will be used to select temporary waypoints
for obstacle avoidance.

**Status:** Implemented and compiled successfully.
The path obstruction detection function has not yet been
integrated into the movement system or tested at runtime.

#### Step 7.8 – Waypoint-based Obstacle Avoidance

Replaced the previous local sideways movement with a
waypoint-based obstacle avoidance approach.

When the direct path to the destination is blocked,
the penguin selects a temporary waypoint beside the obstacle.

The waypoint is positioned outside the obstacle's collision
radius with an additional safety margin.

The penguin maintains its temporary waypoint until it
has been reached, then resumes movement towards the
original destination.

The existing collision detection is used to prevent
movement into static obstacles.

This approach aims to reduce oscillating movement near
obstacles.

**Status:** Implemented, compiled and tested at runtime.
Waypoint-based avoidance significantly reduced oscillating
movement. However, some penguins become stationary near
static obstacles after a period of movement.

Further debugging is required to determine whether the
penguins are blocked while moving towards their waypoints
or repeatedly selecting the same obstacle.

#### Step 7.9 – Waypoint path validation

Improved waypoint selection by checking whether the
entire path towards a temporary waypoint is collision-free.

Previously, only the waypoint position itself was checked,
which could result in penguins becoming stuck behind obstacles.

Both waypoint candidates are now validated using
`isPositionFree()` and `isPathBlocked()`.

If neither candidate has a clear path, no waypoint is selected.

**Status:** Implemented, compiled and tested at runtime.
Path validation prevents the selection of unreachable waypoint
candidates. However, some penguins still become stuck when
both waypoint candidates are rejected.

Debug output confirmed that movement can remain blocked
while no avoidance obstacle is active (`avoidanceObstacle = -1`).

The waypoint generation strategy needs further improvement.

#### Step 7.10 – Valid Random Destinations

Updated `randomDestination()` to avoid selecting
destinations that overlap static obstacles.

The function generates random positions within the ice rink
and validates them using `isPositionFree()`.

Up to 100 attempts are made to find a valid destination.

A fallback position is used if no valid destination is found.
The fallback position must also remain collision-free.

This prevents penguins from intentionally navigating towards
destinations located inside static obstacles.

**Status:** Implemented, compiled and tested at runtime.
The updated random destination generation works without errors.
However, some penguins still become stuck near obstacles due to
limitations in the waypoint-based avoidance algorithm.

Further improvements to obstacle avoidance are required.

#### Step 7.11 – Closest Blocking Obstacle

Updated `isPathBlocked()` to examine all static obstacles
instead of returning immediately when the first obstacle
in the list intersects the path.

The function now selects the blocking obstacle with the
smallest path parameter `t`, representing the earliest
closest point along the movement path.

This improves obstacle selection when multiple obstacles
intersect the path towards a destination.

**Status:** Implemented, compiled and tested at runtime.
The updated obstacle selection successfully identifies blocking
obstacles along the movement path.

However, penguins can still become stuck when both waypoint
candidates are rejected. This will be addressed in Step 7.12.

#### Step 7.12 – Improved Waypoint Generation

Improved obstacle avoidance by generating multiple waypoint
candidates around the blocking obstacle.

The algorithm evaluates 16 candidates around the obstacle's
safety radius. Each candidate must be inside the rink,
collision-free and reachable along a straight path.

The best candidate is selected using the combined distance
from the penguin to the waypoint and from the waypoint
to the destination.

If no valid waypoint is found, the penguin selects a new
random destination instead of repeatedly attempting
a blocked movement.

**Status:** Implemented, compiled and successfully tested at runtime.

The initial waypoint improvements caused jittering and did not
prevent penguins from getting stuck near obstacles.

The solution was revised by making the collision and path-checking
radii consistent and adding a fallback mechanism.

When neither waypoint is reachable, the penguin attempts to find
a new destination with a collision-free path.

After six minutes of runtime testing, no jittering or stuck
penguins were observed.

The revised obstacle avoidance system is considered functional
based on the current tests.

#### **Step 7.13 – MOVE Animation Integration**

Created three separate animations in Blender for the penguins'
movement behaviour: SlideStart, SlideLoop and SlideEnd.

Implemented three internal animation phases within the existing
MOVE state: SLIDE_START, SLIDE_LOOP and SLIDE_END.

SlideStart plays once when entering MOVE. Once completed,
SlideLoop starts and repeats while the penguin moves towards
its destination using Seek Behaviour and obstacle avoidance.

When the destination is reached, SlideEnd plays once before
the penguin transitions to the AIM state.

The existing movement and obstacle avoidance algorithms remain
unchanged. Animation playback and movement are handled separately.

**Status:** Animation sequence implemented in C++ but not yet
compiled or tested. GLB export and runtime verification remain.

## 5. Testing and results

Not tested yet.

## 6. Future improvements

- Movement animation created in Blender.
- Avoidance between penguins.
- More advanced movement decisions.