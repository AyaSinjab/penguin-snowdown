// =====================================================================
// Penguin Snowdown - main.cpp
// Step 1: load arena.glb with cgltf and draw it with a static shader.
// =====================================================================

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string.h>
#include <math.h>
#include <time.h>
#include <algorithm>
#include <cstring>

#define MAIN
#include "MicroGlut.h"
#include "VectorUtils4.h"
#include "GL_utilities.h"
#include "LittleOBJLoader.h"

#define CGLTF_IMPLEMENTATION   // only in ONE .cpp file
#include "cgltf.h"

// ---------------------------------------------------------------------
// PART A: the glTF loader for static models (the arena)
// ---------------------------------------------------------------------

// One drawable piece of the arena: geometry + where it is + its colour
struct StaticPart {
    Model* model;   // VAO etc., created by LoadDataToModel
    mat4   world;   // node's world matrix from the file
    vec3   color;   // baseColorFactor from the material
};

// glTF stores matrices column-major, VectorUtils4's mat4 is row-major
// (m[row*4 + col]), so we transpose while copying.
mat4 matFromGltf(const float* f)
{
    mat4 m;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            m.m[r*4 + c] = f[c*4 + r];
    return m;
}

// Visits one node (and, recursively, its children) and turns every
// mesh primitive it finds into a StaticPart.
void loadNodeRecursive(cgltf_node* node, std::vector<StaticPart>& parts)
{
    if (node->mesh)
    {
        float wf[16];
        cgltf_node_transform_world(node, wf);   // node matrix incl. all parents
        mat4 world = matFromGltf(wf);

        // a mesh can have several primitives (one per material)
        for (cgltf_size p = 0; p < node->mesh->primitives_count; p++)
        {
            cgltf_primitive* prim = &node->mesh->primitives[p];

            // 1. find the accessors for position and normal
            cgltf_accessor* posAcc = NULL;
            cgltf_accessor* nrmAcc = NULL;
            for (cgltf_size a = 0; a < prim->attributes_count; a++)
            {
                if (prim->attributes[a].type == cgltf_attribute_type_position) posAcc = prim->attributes[a].data;
                if (prim->attributes[a].type == cgltf_attribute_type_normal)   nrmAcc = prim->attributes[a].data;
            }
            if (!posAcc || !nrmAcc || !prim->indices) continue;

            // 2. copy positions and normals into our own arrays
            //    (LoadDataToModel keeps these pointers -> never free them)
            int nv = (int)posAcc->count;
            vec3* pos = (vec3*)malloc(nv * sizeof(vec3));
            vec3* nrm = (vec3*)malloc(nv * sizeof(vec3));
            cgltf_accessor_unpack_floats(posAcc, (float*)pos, nv * 3);
            cgltf_accessor_unpack_floats(nrmAcc, (float*)nrm, nv * 3);

            // 3. indices can be 16- or 32-bit in the file; DrawModel wants 32-bit
            int ni = (int)prim->indices->count;
            GLuint* idx = (GLuint*)malloc(ni * sizeof(GLuint));
            for (int i = 0; i < ni; i++)
                idx[i] = (GLuint)cgltf_accessor_read_index(prim->indices, i);

            // 4. colour from the material (white if there is none)
            vec3 col = vec3(1, 1, 1);
            if (prim->material && prim->material->has_pbr_metallic_roughness)
            {
                float* bc = prim->material->pbr_metallic_roughness.base_color_factor;
                col = vec3(bc[0], bc[1], bc[2]);
            }

            StaticPart part;
            part.model = LoadDataToModel(pos, nrm, NULL, NULL, idx, nv, ni);
            part.world = world;
            part.color = col;
            parts.push_back(part);
        }
    }

    for (cgltf_size c = 0; c < node->children_count; c++)
        loadNodeRecursive(node->children[c], parts);
}

// Opens a .glb file and returns all its drawable parts.
std::vector<StaticPart> loadStaticGltf(const char* path)
{
    std::vector<StaticPart> parts;

    cgltf_options options = {};
    cgltf_data* data = NULL;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success ||
        cgltf_load_buffers(&options, data, path) != cgltf_result_success)
    {
        printf("Could not load %s\n", path);
        return parts;
    }

    cgltf_scene* scene = data->scene ? data->scene : &data->scenes[0];
    for (cgltf_size n = 0; n < scene->nodes_count; n++)
        loadNodeRecursive(scene->nodes[n], parts);

    printf("Loaded %s: %d parts\n", path, (int)parts.size());
    cgltf_free(data);   // fine: everything we need was copied
    return parts;
}

// ---------------------------------------------------------------------
// PART A2: the penguin mesh (has vertex colours, later also bones)
// ---------------------------------------------------------------------

// One primitive = one VAO (the penguin has two: body + eyes)
struct MeshPrimitive {
    GLuint vao;
    int    numIndices;
};

// One bone of the skeleton
struct Joint {
    char  name[32];
    int   parent;      // index of the parent joint, -1 for the top (Root)
    vec3  restT;       // rest pose: translation ...
    vec4  restR;       //            ... rotation as quaternion (x, y, z, w) ...
    vec3  restS;       //            ... and scale, relative to the parent
    mat4  inverseBind; // takes a vertex from mesh space into this bone's space
};

struct Skeleton {
    std::vector<Joint> joints;   // same order as the bone indices in in_Joints
};

// One animated property of one bone, e.g. "Flipper.R rotation"
struct AnimChannel {
    int   joint;                 // which joint (index into skeleton.joints)
    int   path;                  // 0 = translation, 1 = rotation, 2 = scale
    bool  step;                  // true = STEP (constant), false = LINEAR
    std::vector<float> times;    // keyframe times in seconds
    std::vector<float> values;   // 3 floats per key (T, S) or 4 floats per key (R)
};

// One animation clip, e.g. "Idle"
struct AnimClip {
    char  name[32];
    float start;                 // time of the first keyframe (1/24 s for our clips)
    float end;                   // time of the last keyframe
    std::vector<AnimChannel> channels;
};

struct PenguinMesh {
    std::vector<MeshPrimitive> prims;
    float minY;   // lowest point of the mesh (used to put the feet on the ground)
    Skeleton skeleton;
    std::vector<AnimClip> clips;
};

// Rotation matrix from a unit quaternion q = (x, y, z, w)
mat4 quatToMat4(vec4 q)
{
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return mat4(1 - 2*(y*y + z*z),     2*(x*y - z*w),     2*(x*z + y*w), 0,
                    2*(x*y + z*w), 1 - 2*(x*x + z*z),     2*(y*z - x*w), 0,
                    2*(x*z - y*w),     2*(y*z + x*w), 1 - 2*(x*x + y*y), 0,
                    0,                 0,                 0,             1);
}

// Local matrix of a joint relative to its parent: T * R * S
mat4 composeTRS(vec3 t, vec4 r, vec3 s)
{
    return T(t.x, t.y, t.z) * quatToMat4(r) * S(s.x, s.y, s.z);
}

// Reads the skin (bone list, parents, rest pose, inverse bind matrices)
Skeleton loadSkeleton(cgltf_data* data)
{
    Skeleton sk;
    cgltf_skin* skin = &data->skins[0];

    for (cgltf_size j = 0; j < skin->joints_count; j++)
    {
        cgltf_node* node = skin->joints[j];
        Joint jt;
        snprintf(jt.name, sizeof(jt.name), "%s", node->name ? node->name : "?");

        // parent = the joint whose node is this node's parent (-1 if none)
        jt.parent = -1;
        for (cgltf_size k = 0; k < skin->joints_count; k++)
            if (skin->joints[k] == node->parent) jt.parent = (int)k;

        // rest pose (glTF leaves out values that are default)
        jt.restT = node->has_translation ? vec3(node->translation[0], node->translation[1], node->translation[2]) : vec3(0, 0, 0);
        jt.restR = node->has_rotation    ? vec4(node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]) : vec4(0, 0, 0, 1);
        jt.restS = node->has_scale       ? vec3(node->scale[0], node->scale[1], node->scale[2]) : vec3(1, 1, 1);

        // inverse bind matrix (column-major in the file -> transpose)
        float f[16];
        cgltf_accessor_read_float(skin->inverse_bind_matrices, j, f, 16);
        jt.inverseBind = matFromGltf(f);

        sk.joints.push_back(jt);
        printf("  joint %d %-10s parent %d\n", (int)j, jt.name, jt.parent);
    }
    return sk;
}

// If worldOut is given, the joints' world matrices are copied there too
// (step 6: needed to find where the flipper is).
void computeSkinMatrices(const Skeleton& sk, const std::vector<mat4>& locals, std::vector<mat4>& skin,
                         std::vector<mat4>* worldOut = NULL)
{
    int n = (int)sk.joints.size();
    std::vector<mat4> world(n);
    skin.resize(n);
    for (int j = 0; j < n; j++)
    {
        // Blender exports parents before children, so world[parent] is ready
        int p = sk.joints[j].parent;
        world[j] = (p < 0) ? locals[j] : world[p] * locals[j];
        skin[j]  = world[j] * sk.joints[j].inverseBind;
    }
    if (worldOut) *worldOut = world;
}

// ---------------------------------------------------------------------
// PART A3: animation clips (step 4)
// ---------------------------------------------------------------------

// Reads all animations in the file. Each channel is connected to a joint
// by comparing the channel's target node with the skin's joint nodes.
std::vector<AnimClip> loadClips(cgltf_data* data)
{
    std::vector<AnimClip> clips;
    cgltf_skin* skin = &data->skins[0];

    for (cgltf_size a = 0; a < data->animations_count; a++)
    {
        cgltf_animation* anim = &data->animations[a];
        AnimClip clip;
        snprintf(clip.name, sizeof(clip.name), "%s", anim->name ? anim->name : "?");
        clip.start = 1e9f;
        clip.end   = -1e9f;

        for (cgltf_size c = 0; c < anim->channels_count; c++)
        {
            cgltf_animation_channel* ch = &anim->channels[c];
            AnimChannel out;

            // which joint does this channel move?
            out.joint = -1;
            for (cgltf_size j = 0; j < skin->joints_count; j++)
                if (skin->joints[j] == ch->target_node) out.joint = (int)j;
            if (out.joint < 0) continue;

            if      (ch->target_path == cgltf_animation_path_type_translation) out.path = 0;
            else if (ch->target_path == cgltf_animation_path_type_rotation)    out.path = 1;
            else if (ch->target_path == cgltf_animation_path_type_scale)       out.path = 2;
            else continue;

            out.step = (ch->sampler->interpolation == cgltf_interpolation_type_step);

            // keyframe times (input) and values (output)
            cgltf_accessor* in  = ch->sampler->input;
            cgltf_accessor* val = ch->sampler->output;
            int comps = (out.path == 1) ? 4 : 3;
            out.times.resize(in->count);
            out.values.resize(val->count * comps);
            for (cgltf_size k = 0; k < in->count; k++)
                cgltf_accessor_read_float(in, k, &out.times[k], 1);
            for (cgltf_size k = 0; k < val->count; k++)
                cgltf_accessor_read_float(val, k, &out.values[k * comps], comps);

            if (out.times.front() < clip.start) clip.start = out.times.front();
            if (out.times.back()  > clip.end)   clip.end   = out.times.back();
            clip.channels.push_back(out);
        }
        clips.push_back(clip);
        printf("  clip %-6s %.3f s - %.3f s (%d channels)\n", clip.name, clip.start, clip.end, (int)clip.channels.size());
    }
    return clips;
}

// Finds a clip by name (glTF does not keep the order from Blender)
int findClip(const PenguinMesh& mesh, const char* name)
{
    for (size_t i = 0; i < mesh.clips.size(); i++)
    {
        if (strcmp(mesh.clips[i].name, name) == 0)
            return (int)i;
    }

    printf("ERROR: Animation clip '%s' not found!\n", name);
    return -1;
}

// Spherical linear interpolation between two unit quaternions
vec4 slerp(vec4 a, vec4 b, float u)
{
    float d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;   // cos of the angle between them

    // q and -q are the same rotation: flip one so we take the SHORT way around
    if (d < 0) { b = vec4(-b.x, -b.y, -b.z, -b.w); d = -d; }

    float wa, wb;
    if (d > 0.9995f)
    {
        // almost identical: plain linear blend (avoids dividing by sin(~0))
        wa = 1 - u;
        wb = u;
    }
    else
    {
        float theta = acosf(d);              // angle between the quaternions
        wa = sinf((1 - u) * theta) / sinf(theta);
        wb = sinf(u * theta)       / sinf(theta);
    }
    vec4 r = vec4(wa*a.x + wb*b.x, wa*a.y + wb*b.y, wa*a.z + wb*b.z, wa*a.w + wb*b.w);

    // normalize to remove rounding errors
    float len = sqrtf(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    return vec4(r.x/len, r.y/len, r.z/len, r.w/len);
}

// Computes every joint's local matrix for 'clip' at time t (seconds, file time).
// Joints/properties that the clip doesn't animate keep their rest value.
void sampleClip(const Skeleton& sk, const AnimClip& clip, float t, std::vector<mat4>& locals)
{
    int n = (int)sk.joints.size();
    std::vector<vec3> T_(n), S_(n);
    std::vector<vec4> R_(n);
    for (int j = 0; j < n; j++)          // start from the rest pose
    {
        T_[j] = sk.joints[j].restT;
        R_[j] = sk.joints[j].restR;
        S_[j] = sk.joints[j].restS;
    }

    for (const AnimChannel& ch : clip.channels)
    {
        const std::vector<float>& tm = ch.times;
        int last = (int)tm.size() - 1;

        // find the two keyframes k0 and k1 around t, and how far between them (u)
        int k0, k1;
        float u;
        if (t <= tm[0])         { k0 = k1 = 0;    u = 0; }
        else if (t >= tm[last]) { k0 = k1 = last; u = 0; }
        else
        {
            k0 = 0;
            while (tm[k0 + 1] <= t) k0++;
            k1 = k0 + 1;
            u = (t - tm[k0]) / (tm[k1] - tm[k0]);
        }
        if (ch.step) u = 0;              // STEP: hold the earlier key

        if (ch.path == 1)                // rotation: SLERP
        {
            const float* a = &ch.values[k0 * 4];
            const float* b = &ch.values[k1 * 4];
            R_[ch.joint] = slerp(vec4(a[0], a[1], a[2], a[3]), vec4(b[0], b[1], b[2], b[3]), u);
        }
        else                             // translation / scale: LERP
        {
            const float* a = &ch.values[k0 * 3];
            const float* b = &ch.values[k1 * 3];
            vec3 v = vec3(a[0] + u*(b[0]-a[0]), a[1] + u*(b[1]-a[1]), a[2] + u*(b[2]-a[2]));
            if (ch.path == 0) T_[ch.joint] = v;
            else              S_[ch.joint] = v;
        }
    }

    locals.resize(n);
    for (int j = 0; j < n; j++)
        locals[j] = composeTRS(T_[j], R_[j], S_[j]);
}

// Creates a VBO with 'components' floats per vertex and connects it to the
// shader input called 'name'. If the shader doesn't use that input
// (e.g. in_Joints before step 3), glGetAttribLocation returns -1 -> skip.
void addVertexBuffer(GLuint program, const char* name, const float* data, int numVerts, int components)
{
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, numVerts * components * sizeof(float), data, GL_STATIC_DRAW);

    GLint loc = glGetAttribLocation(program, name);
    if (loc >= 0)
    {
        glVertexAttribPointer(loc, components, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(loc);
    }
}

// Reads one attribute (POSITION, NORMAL, ...) of a primitive into a float array.
// Returns NULL if the primitive doesn't have it.
float* readAttribute(cgltf_primitive* prim, cgltf_attribute_type type, int components)
{
    for (cgltf_size a = 0; a < prim->attributes_count; a++)
    {
        if (prim->attributes[a].type != type) continue;
        cgltf_accessor* acc = prim->attributes[a].data;
        float* out = (float*)malloc(acc->count * components * sizeof(float));
        // read_float converts any stored type (float, byte, short...) to float
        for (cgltf_size i = 0; i < acc->count; i++)
            cgltf_accessor_read_float(acc, i, &out[i * components], components);
        return out;
    }
    return NULL;
}

PenguinMesh loadPenguinMesh(const char* path, GLuint program)
{
    PenguinMesh mesh;
    mesh.minY = 0;

    cgltf_options options = {};
    cgltf_data* data = NULL;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success ||
        cgltf_load_buffers(&options, data, path) != cgltf_result_success)
    {
        printf("Could not load %s\n", path);
        return mesh;
    }

    cgltf_mesh* m = &data->meshes[0];   // the penguin file has exactly one mesh
    for (cgltf_size p = 0; p < m->primitives_count; p++)
    {
        cgltf_primitive* prim = &m->primitives[p];

        // number of vertices = number of positions
        int nv = 0;
        for (cgltf_size a = 0; a < prim->attributes_count; a++)
            if (prim->attributes[a].type == cgltf_attribute_type_position)
            {
                nv = (int)prim->attributes[a].data->count;
                // the accessor stores min/max of the positions -> lowest point
                float lowest = prim->attributes[a].data->min[1];
                if (p == 0 || lowest < mesh.minY) mesh.minY = lowest;
            }

        float* pos     = readAttribute(prim, cgltf_attribute_type_position, 3);
        float* nrm     = readAttribute(prim, cgltf_attribute_type_normal,   3);
        float* col     = readAttribute(prim, cgltf_attribute_type_color,    3);
        float* joints  = readAttribute(prim, cgltf_attribute_type_joints,   4);  // used in step 3
        float* weights = readAttribute(prim, cgltf_attribute_type_weights,  4);  // used in step 3

        MeshPrimitive mp;
        glGenVertexArrays(1, &mp.vao);
        glBindVertexArray(mp.vao);

        addVertexBuffer(program, "in_Position", pos, nv, 3);
        addVertexBuffer(program, "in_Normal",   nrm, nv, 3);
        if (col)     addVertexBuffer(program, "in_Color",   col,     nv, 3);
        if (joints)  addVertexBuffer(program, "in_Joints",  joints,  nv, 4);
        if (weights) addVertexBuffer(program, "in_Weights", weights, nv, 4);

        // index buffer (always stored as 32-bit for us)
        mp.numIndices = (int)prim->indices->count;
        GLuint* idx = (GLuint*)malloc(mp.numIndices * sizeof(GLuint));
        for (int i = 0; i < mp.numIndices; i++)
            idx[i] = (GLuint)cgltf_accessor_read_index(prim->indices, i);
        GLuint ibo;
        glGenBuffers(1, &ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mp.numIndices * sizeof(GLuint), idx, GL_STATIC_DRAW);

        glBindVertexArray(0);
        mesh.prims.push_back(mp);

        // the data is on the GPU now -> the CPU copies can go
        free(pos); free(nrm); free(col); free(joints); free(weights); free(idx);
    }

    printf("Loaded %s: %d primitives, lowest point y = %.3f\n", path, (int)mesh.prims.size(), mesh.minY);
    mesh.skeleton = loadSkeleton(data); // load the skeleton
    mesh.clips = loadClips(data);
    cgltf_free(data);
    return mesh;
}

void drawPenguinMesh(const PenguinMesh& mesh)
{
    for (const MeshPrimitive& mp : mesh.prims)
    {
        glBindVertexArray(mp.vao);
        glDrawElements(GL_TRIANGLES, mp.numIndices, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
}


// ---------------------------------------------------------------------
// PART B: globals
// ---------------------------------------------------------------------

GLuint staticShader;
mat4 projectionMatrix;
std::vector<StaticPart> arena;


 // ---------------------------------------------------------------------
 // Orbit Camera
 // ---------------------------------------------------------------------

 // Camera angles in radians.
 float cameraYaw = 0.0f;
 float cameraPitch = atan2f(14.0f, 20.0f);

 // Distance from the camera to the centre of the arena.
 float cameraDistance = sqrtf(14.0f * 14.0f + 20.0f * 20.0f);

 // Mouse interaction.
 bool isDragging = false;
 int lastMouseX = 0;
 int lastMouseY = 0;


// --- penguins ---
const float PENGUIN_SCALE = 0.413f;   // from the arena file: ~1.5 m tall penguins
const float GROUND_Y      = 0.05f;    // top of the ice

// Ice rink boundaries in world coordinates.
// A safety margin will be applied when calculating valid destinations.
const float RINK_MIN_X = -10.0f;
const float RINK_MAX_X =  10.0f;
const float RINK_MIN_Z =  -7.0f;
const float RINK_MAX_Z =   7.0f;

GLuint penguinShader;
PenguinMesh penguinMesh;

// The three AI states (one animation clip each)
enum State { IDLE, MOVE, AIM, THROW };
const char* stateNames[] = { "IDLE", "MOVE", "AIM", "THROW" };

// Animation phases used while the penguin is in MOVE.
enum SlidePhase {
    SLIDE_START,
    SLIDE_LOOP,
    SLIDE_END
};

struct Penguin {
    vec3  position;   // where on the ice (y = GROUND_Y)
    float yaw;        // rotation around the vertical axis, radians
    int   clip;       // which animation is playing (index into penguinMesh.clips)
    float clipTime;   // seconds since the clip started
    bool  loop;       // true: start over at the end, false: stop on the last frame

    // --- AI (step 5) ---
    State state;      // current FSM state
    float stateTimer; // seconds spent in the current state
    float waitTime;   // how long to stay in IDLE before aiming (random)
    SlidePhase slidePhase; // Current animation phase during MOVE.
    int   target;     // index of the penguin we are aiming at
    
    // --- score (step 6b) ---
    int   throws;     // snowballs thrown
    int   hits;       // snowballs that hit someone
    int   timesHit;   // times this penguin was hit
    float hitTimer;   // > 0 while the red hit flash is showing (step 6c)

    // movement and seek behaviour (part D)
    vec3  destination;   // Target position for movement (Seek Behaviour)
    int avoidanceSide = 0; // 0 = no avoidance, 1 = left, -1 = right
    int avoidanceObstacle = -1; // Index of obstacle being avoided
    vec3 avoidanceWaypoint;       // Temporary target for obstacle avoidance
    float penguinAvoidanceCooldown = 0.0f;
};
float lastTime = 0;   // for dt (seconds)

// Starts a clip from the beginning
void playClip(Penguin& p, const char* name, bool loop)
{
    int clipIndex = findClip(penguinMesh, name);

    if (clipIndex < 0)
    {
        printf("ERROR: Cannot play missing animation '%s'\n", name);
        exit(EXIT_FAILURE);
    }

    p.clip = clipIndex;
    p.clipTime = 0;
    p.loop = loop;
}


// Moves the penguin's animation clock forward by dt seconds

void advanceClip(Penguin& p, float dt)
{
    const AnimClip& c = penguinMesh.clips[p.clip];
    float duration = c.end - c.start;

    // Speed up the start and end of the sliding animation.
    float speed = 1.0f;

    if (strcmp(c.name, "SlideStart") == 0 ||
        strcmp(c.name, "SlideEnd") == 0)
        speed = 2.0f;

    p.clipTime += dt * speed;

    if (p.loop)
        p.clipTime = fmodf(p.clipTime, duration);
    else if (p.clipTime > duration)
        p.clipTime = duration;
}



std::vector<Penguin> penguins;


 // Checks whether a position is too close to another penguin.
bool collidesWithPenguin(const vec3& position, const Penguin& current)
{
    const float penguinRadius = 0.4f;
    const float safeDistance = 2.0f * penguinRadius;

    for (const Penguin& other : penguins)
    {
        // Ignore the penguin checking its own position.
        if (&other == &current)
            continue;

        float dx = position.x - other.position.x;
        float dz = position.z - other.position.z;

        float distanceSquared = dx * dx + dz * dz;

        if (distanceSquared < safeDistance * safeDistance)
            return true;
    }

    return false;
}


// ---------------------------------------------------------------------
// PART B2: the AI - a finite state machine per penguin (step 5)
// ---------------------------------------------------------------------

const float TURN_SPEED    = 2.5f;          // max turning speed, rad/s (~143 deg/s)
const float AIM_TOLERANCE = 0.05f;         // "facing the target" = within ~3 degrees
const float RELEASE_TIME  = 5.0f / 24.0f;  // Throw frame 6: the snowball leaves the flipper

float randomRange(float a, float b)
{
    return a + (b - a) * (rand() / (float)RAND_MAX);
}

// ---------------------------------------------------------------------
// PART D: Penguin movement and seek behaviour
// ---------------------------------------------------------------------

// Movement settings
const float MOVE_SPEED = 1.5f;       // Movement speed in metres per second
const float ARRIVAL_RADIUS = 0.1f;   // Distance at which the destination is reached

// Represents a static obstacle on the ice rink.
struct Obstacle
{
    vec3 position;  // Centre of the obstacle
    float radius;   // Collision radius in the XZ-plane
};

// Static obstacles in the ice rink.
// Positions are based on the arena model.
// Collision radii are initial estimates and may need adjustment.
const std::vector<Obstacle> obstacles = {
    // Trees
    { vec3(-6.427f, GROUND_Y, -2.689f), 0.8f },
    { vec3( 1.802f, GROUND_Y,  3.083f), 0.8f },

    // Snowmen
    { vec3( 6.440f, GROUND_Y,  0.481f), 0.7f },
    { vec3(-8.253f, GROUND_Y,  0.764f), 0.7f },
    { vec3( 0.872f, GROUND_Y, -4.065f), 0.7f }
};

// Checks whether a position is too close to a static obstacle.
// Returns true if a collision is detected.
bool collidesWithObstacle(const vec3& position)
{
    const float penguinRadius = 0.4f;

    for (const Obstacle& obstacle : obstacles)
    {
        // Calculate horizontal distance to the obstacle.
        float dx = position.x - obstacle.position.x;
        float dz = position.z - obstacle.position.z;

        float distance = sqrtf(dx * dx + dz * dz);

        // Collision occurs when the two collision circles overlap.
        if (distance < penguinRadius + obstacle.radius)
            return true;
    }

    return false;
}

// Checks whether a proposed position is free from static obstacles.
bool isPositionFree(const vec3& position)
{
    return !collidesWithObstacle(position);
}


// Checks whether the straight path between two positions
// intersects a static obstacle.
// Returns the closest blocking obstacle along the path.
bool isPathBlocked(const vec3& start, const vec3& end, int& obstacleIndex)
{
    const float penguinRadius = 0.4f;

    float dx = end.x - start.x;
    float dz = end.z - start.z;
    float lengthSquared = dx * dx + dz * dz;

    obstacleIndex = -1;

    // Keep track of the closest obstacle along the path.
    float closestT = 2.0f;

    for (int i = 0; i < (int)obstacles.size(); i++)
    {
        const Obstacle& obstacle = obstacles[i];

        float t = 0.0f;

        if (lengthSquared > 0.000001f)
        {
            t = ((obstacle.position.x - start.x) * dx +
                 (obstacle.position.z - start.z) * dz) / lengthSquared;

            t = std::clamp(t, 0.0f, 1.0f);
        }

        float closestX = start.x + t * dx;
        float closestZ = start.z + t * dz;

        float diffX = closestX - obstacle.position.x;
        float diffZ = closestZ - obstacle.position.z;

        float safeRadius = obstacle.radius + penguinRadius;

        // Check whether this obstacle blocks the path.
        if (diffX * diffX + diffZ * diffZ < safeRadius * safeRadius)
        {
            // Keep the obstacle closest along the path.
            if (t < closestT)
            {
                closestT = t;
                obstacleIndex = i;
            }
        }
    }

    return obstacleIndex != -1;
}

// Keeps a destination inside the ice rink.
// The margin prevents the penguin's body from crossing the edge.
vec3 clampDestination(vec3 destination)
{
    const float margin = 0.5f;

    destination.x = std::clamp(
        destination.x,
        RINK_MIN_X + margin,
        RINK_MAX_X - margin
    );

    destination.z = std::clamp(
        destination.z,
        RINK_MIN_Z + margin,
        RINK_MAX_Z - margin
    );

    destination.y = GROUND_Y;

    return destination;
}

// Forward declaration for obstacle collision checking.
bool isPositionFree(const vec3& position);

// Generates a random destination within the ice rink.
vec3 randomDestination()
{
    // Try to find a destination that does not overlap an obstacle.
    for (int attempt = 0; attempt < 100; attempt++)
    {
        vec3 destination = vec3(
            randomRange(RINK_MIN_X, RINK_MAX_X),
            GROUND_Y,
            randomRange(RINK_MIN_Z, RINK_MAX_Z)
        );

        destination = clampDestination(destination);

        if (isPositionFree(destination))
        {
            return destination;
        }
    }

    // Fallback if no valid destination was found.
    return vec3(0.0f, GROUND_Y, 0.0f);
}

// ---------------------------------------------------------------------
// PART D2: Seek Behaviour
// ---------------------------------------------------------------------

// Forward declaration of the existing turning function.
float turnTowards(Penguin& p, float targetYaw, float dt);

// Moves a penguin towards its destination using Seek Behaviour.
// Returns true when the destination has been reached.
bool updateSeek(Penguin& p, float dt)
{
    // Check if the original destination has been reached.
    float goalDx = p.destination.x - p.position.x;
    float goalDz = p.destination.z - p.position.z;
    float goalDistance = sqrtf(goalDx * goalDx + goalDz * goalDz);

    if (goalDistance <= ARRIVAL_RADIUS)
    {
        p.avoidanceSide = 0;
        p.avoidanceObstacle = -1;
        return true;
    }

    // Check whether the direct path to the destination is blocked.
    int blockingObstacle = -1;
    bool pathBlocked = isPathBlocked(
        p.position, p.destination, blockingObstacle
    );

    // Start avoiding an obstacle if necessary.
    if (p.avoidanceObstacle == -1 && pathBlocked)
    {
        const Obstacle& obstacle = obstacles[blockingObstacle];

        // Direction from the penguin towards its destination.
        float dirX = goalDx / goalDistance;
        float dirZ = goalDz / goalDistance;

        // Perpendicular direction.
        float sideX = -dirZ;
        float sideZ = dirX;

        // Place the waypoint outside the obstacle's collision radius.
        float safeRadius = obstacle.radius + 0.4f + 0.3f;

        vec3 leftWaypoint = obstacle.position;
        leftWaypoint.x += sideX * safeRadius;
        leftWaypoint.z += sideZ * safeRadius;

        vec3 rightWaypoint = obstacle.position;
        rightWaypoint.x -= sideX * safeRadius;
        rightWaypoint.z -= sideZ * safeRadius;

        // Keep waypoints inside the ice rink.
        leftWaypoint = clampDestination(leftWaypoint);
        rightWaypoint = clampDestination(rightWaypoint);

        // Check whether the entire path to each waypoint is clear.
        int leftBlocker = -1;
        int rightBlocker = -1;

        bool leftClear =
            isPositionFree(leftWaypoint) &&
            !isPathBlocked(p.position, leftWaypoint, leftBlocker);

        bool rightClear =
            isPositionFree(rightWaypoint) &&
            !isPathBlocked(p.position, rightWaypoint, rightBlocker);

        // Choose a waypoint with a collision-free path.
        if (leftClear)
        {
            p.avoidanceSide = 1;
            p.avoidanceWaypoint = leftWaypoint;
            p.avoidanceObstacle = blockingObstacle;
        }
        else if (rightClear)
        {
            p.avoidanceSide = -1;
            p.avoidanceWaypoint = rightWaypoint;
            p.avoidanceObstacle = blockingObstacle;
        }
        else
{
    // Neither waypoint is reachable.
    // Try to find a new destination with a clear path.
    bool foundDestination = false;

    for (int i = 0; i < 100; i++)
    {
        vec3 candidate = randomDestination();
        int candidateBlocker = -1;

        if (!isPathBlocked(p.position, candidate, candidateBlocker))
        {
            p.destination = candidate;
            foundDestination = true;
            break;
        }
    }

    p.avoidanceObstacle = -1;
    p.avoidanceSide = 0;

    if (!foundDestination)
    {
        printf("NO REACHABLE DESTINATION FOUND\n");
    }

    return false;
}
    }

    // Use the waypoint while avoiding an obstacle.
    vec3 target = p.destination;

    if (p.avoidanceObstacle != -1)
    {
        target = p.avoidanceWaypoint;

        float waypointDx = target.x - p.position.x;
        float waypointDz = target.z - p.position.z;

        float waypointDistance = sqrtf(
            waypointDx * waypointDx + waypointDz * waypointDz
        );

        // Once the waypoint is reached, resume normal Seek.
        if (waypointDistance <= ARRIVAL_RADIUS)
        {
            p.avoidanceObstacle = -1;
            p.avoidanceSide = 0;
            target = p.destination;
        }
    }

    // Calculate movement direction towards the current target.
    float dx = target.x - p.position.x;
    float dz = target.z - p.position.z;
    float distance = sqrtf(dx * dx + dz * dz);

    if (distance <= ARRIVAL_RADIUS)
        return false;

    // Rotate towards the current target.
    float targetYaw = atan2f(dx, dz);
    turnTowards(p, targetYaw, dt);

    // Calculate the next position.
    float step = fminf(MOVE_SPEED * dt, distance);

    vec3 nextPosition = p.position;
    nextPosition.x += (dx / distance) * step;
    nextPosition.z += (dz / distance) * step;
    nextPosition.y = GROUND_Y;

    // Move only if the proposed position is collision-free.
if (isPositionFree(nextPosition) &&
    !collidesWithPenguin(nextPosition, p))
{
    p.position = nextPosition;
}

else

    // Move only if the proposed position is collision-free.
    if (isPositionFree(nextPosition) &&
        !collidesWithPenguin(nextPosition, p))
    {
        p.position = nextPosition;
    }
    else
    {
        // Choose a new destination if another penguin blocks the path.
        if (isPositionFree(nextPosition) &&
            collidesWithPenguin(nextPosition, p) &&
            p.penguinAvoidanceCooldown <= 0.0f)
        {
            p.destination = randomDestination();

            // Reset obstacle avoidance for the new destination.
            p.avoidanceObstacle = -1;
            p.avoidanceSide = 0;

            // Wait before allowing another direction change.
            p.penguinAvoidanceCooldown = 1.0f;
        }

        // Debug: check why movement is blocked.
        static int blockedCounter = 0;

        if (++blockedCounter % 120 == 0)
        {
            printf(
                "MOVE BLOCKED | obstacle: %d | "
                "position: (%.2f, %.2f) | "
                "target: (%.2f, %.2f)\n",
                p.avoidanceObstacle,
                p.position.x, p.position.z,
                target.x, target.z
            );
        }
    }

    return false;
}

// ---------------------------------------------------------------------
// PART B3: snowballs and physics (step 6)
// ---------------------------------------------------------------------

const float GRAVITY       = 9.82f;                  // m/s^2
const float SNOWBALL_R    = 0.1f;                   // radius in metres (= sphere.obj's radius)
const float THROW_ANGLE   = 35.0f * (float)M_PI / 180.0f;  // launch angle above horizontal
const float TARGET_HEIGHT = 0.7f;                   // aim at the middle of the body (metres above the feet)
const float FLIPPER_TIP   = 0.85f;                  // flipper tip along the Flipper.R bone (mesh units)


// Hit volume of a penguin: one sphere around the body (step 6b)
const float BODY_CENTER_Y = 0.7f;                   // metres above the feet (same as TARGET_HEIGHT)
const float BODY_RADIUS   = 0.4f;                   // the scaled penguin is ~0.85 m wide

// Aiming error, so the AI sometimes misses (step 6b)
const float AIM_NOISE_DEG = 4.0f;                   // random sideways error, +- degrees
const float SPEED_NOISE   = 0.06f;                  // random speed error, +- 6 %

// Visual feedback (step 6c)
const float HIT_FLASH_TIME = 0.35f;                 // seconds a hit penguin flashes red
const float SPLAT_LIFE     = 4.0f;                  // seconds a miss splat stays on the ice
const float SPLAT_SHRINK   = 1.0f;                  // the last second it shrinks away

struct Snowball {
    vec3 position;
    vec3 velocity;
    int  owner;      // which penguin threw it
    bool alive;
};

std::vector<Snowball> snowballs;
Model* snowballModel;
// A flattened snowball left on the ice where a throw missed
struct Splat {
    vec3  position;
    float age;       // seconds since it landed
};
std::vector<Splat> splats;

// The full placement matrix of a penguin (same as used for drawing)
mat4 penguinModelMatrix(const Penguin& p)
{
    // Read right to left: lift feet to y = 0 -> shrink -> turn -> move into place
    return T(p.position.x, p.position.y, p.position.z)
         * Ry(p.yaw)
         * S(PENGUIN_SCALE, PENGUIN_SCALE, PENGUIN_SCALE)
         * T(0, -penguinMesh.minY, 0);
}

// Where is the tip of the throwing flipper right now, in world coordinates?
// = penguin placement * Flipper.R's joint world matrix * a point along the bone
vec3 flipperTipWorld(const Penguin& p)
{
    const AnimClip& clip = penguinMesh.clips[p.clip];
    std::vector<mat4> locals, skin, jointWorld;
    sampleClip(penguinMesh.skeleton, clip, clip.start + p.clipTime, locals);
    computeSkinMatrices(penguinMesh.skeleton, locals, skin, &jointWorld);

    int flipper = 5;   // joint 5 = Flipper.R (see the joint list in the terminal)
    vec4 tip = penguinModelMatrix(p) * jointWorld[flipper] * vec4(0, FLIPPER_TIP, 0, 1);
    return vec3(tip.x, tip.y, tip.z);
}

// Ballistic aiming: the start velocity that makes a ball launched at angle
// THROW_ANGLE from 'from' pass through 'to' (no air resistance).
//   horizontal: d = v cos(a) t        vertical: h = v sin(a) t - g t^2 / 2
//   eliminate t  ->  v^2 = g d^2 / (2 cos^2(a) (d tan(a) - h))
vec3 ballisticVelocity(vec3 from, vec3 to)
{
    float dx = to.x - from.x, dz = to.z - from.z;
    float d  = sqrtf(dx*dx + dz*dz);    // horizontal distance
    float h  = to.y - from.y;           // height difference (negative = target lower)
    float c  = cosf(THROW_ANGLE);
    float denom = 2 * c * c * (d * tanf(THROW_ANGLE) - h);
    if (denom <= 0 || d < 0.001f)
        return vec3(0, 0, 0);           // no solution at this angle (target too high)

    float v = sqrtf(GRAVITY * d * d / denom);
    // horizontal part towards the target + vertical part
    return vec3(v * c * dx / d, v * sinf(THROW_ANGLE), v * c * dz / d);
}

// Creates a snowball at the flipper tip, flying towards the target penguin
void spawnSnowball(const Penguin& p, int index, const Penguin& target)
{
    Snowball b;
    b.position = flipperTipWorld(p);
    vec3 aimPoint = vec3(target.position.x, target.position.y + TARGET_HEIGHT, target.position.z);
    vec3 v = ballisticVelocity(b.position, aimPoint);

    // Human-like error: turn the throw a little sideways and change the speed a little.
    // Sideways = rotate the horizontal part (x, z) around the vertical axis.
    float err = randomRange(-AIM_NOISE_DEG, AIM_NOISE_DEG) * (float)M_PI / 180.0f;
    float vx = v.x * cosf(err) + v.z * sinf(err);
    float vz = -v.x * sinf(err) + v.z * cosf(err);
    float k  = 1.0f + randomRange(-SPEED_NOISE, SPEED_NOISE);
    b.velocity = vec3(vx * k, v.y * k, vz * k);

    b.owner = index;
    b.alive = true;
    snowballs.push_back(b);
    penguins[index].throws++;
    printf("Penguin %d throws at penguin %d (speed %.1f m/s)\n", index, p.target,
           sqrtf(b.velocity.x*b.velocity.x + b.velocity.y*b.velocity.y + b.velocity.z*b.velocity.z));
}

// Sphere-sphere test: does the snowball touch penguin q's body sphere?
bool hitsPenguin(const Snowball& b, const Penguin& q)
{
    float dx = b.position.x - q.position.x;
    float dy = b.position.y - (q.position.y + BODY_CENTER_Y);
    float dz = b.position.z - q.position.z;
    float r  = SNOWBALL_R + BODY_RADIUS;
    return dx*dx + dy*dy + dz*dz < r*r;   // compare squared distances: no sqrt needed
}

// Moves all snowballs one time step (semi-implicit Euler) and removes the dead ones
void updateSnowballs(float dt)
{
    for (Snowball& b : snowballs)
    {
        // semi-implicit Euler: first velocity, then position with the NEW velocity
        b.velocity.y -= GRAVITY * dt;
        b.position.x += b.velocity.x * dt;
        b.position.y += b.velocity.y * dt;
        b.position.z += b.velocity.z * dt;

        // hit a penguin? (never the one who threw it)
        for (int i = 0; i < (int)penguins.size() && b.alive; i++)
        {
            if (i == b.owner) continue;
            if (hitsPenguin(b, penguins[i]))
            {
                b.alive = false;
                penguins[b.owner].hits++;
                penguins[i].timesHit++;
                penguins[i].hitTimer = HIT_FLASH_TIME;   // start the red flash
                printf("  HIT! Penguin %d hit penguin %d   (penguin %d: %d hits / %d throws)\n",
                       b.owner, i, b.owner, penguins[b.owner].hits, penguins[b.owner].throws);
            }
        }

        // hit the ground (the ice surface) -> missed, the snowball is gone
        if (b.alive && b.position.y - SNOWBALL_R < GROUND_Y)
        {
            b.alive = false;
            splats.push_back({ vec3(b.position.x, GROUND_Y, b.position.z), 0.0f });   // leave a mark
            printf("  miss: penguin %d's snowball landed on the ice\n", b.owner);
        }
    }
    // remove dead snowballs ("erase-remove" idiom)
    snowballs.erase(std::remove_if(snowballs.begin(), snowballs.end(),
                                   [](const Snowball& b) { return !b.alive; }),
                    snowballs.end());
}


// Ages the splats and removes the old ones
void updateSplats(float dt)
{
    for (Splat& s : splats) s.age += dt;
    splats.erase(std::remove_if(splats.begin(), splats.end(),
                                [](const Splat& s) { return s.age > SPLAT_LIFE; }),
                 splats.end());
}

// Angle (yaw) that makes penguin p face penguin q.
// Forward is +Z and Ry(yaw) turns +Z into (sin yaw, 0, cos yaw), so yaw = atan2(dx, dz).
float yawTowards(const Penguin& p, const Penguin& q)
{
    float dx = q.position.x - p.position.x;
    float dz = q.position.z - p.position.z;
    return atan2f(dx, dz);
}

// Shortest signed difference between two angles, in [-pi, pi]
float angleDiff(float target, float current)
{
    float d = target - current;
    return atan2f(sinf(d), cosf(d));
}

// Turns gradually towards the target angle, at most TURN_SPEED * dt per frame.
// Returns how far (radians) we still are from facing the target.
float turnTowards(Penguin& p, float targetYaw, float dt)
{
    float diff = angleDiff(targetYaw, p.yaw);
    float maxStep = TURN_SPEED * dt;
    if (fabsf(diff) <= maxStep) p.yaw = targetYaw;              // close enough: snap
    else                        p.yaw += (diff > 0 ? maxStep : -maxStep);
    return fabsf(angleDiff(targetYaw, p.yaw));
}

// True when a one-shot clip has reached its last frame
bool clipFinished(const Penguin& p)
{
    const AnimClip& c = penguinMesh.clips[p.clip];
    return !p.loop && p.clipTime >= (c.end - c.start);
}

// Everything that happens ONCE when a penguin enters a state
void enterState(Penguin& p, int index, State s)
{
    printf("Penguin %d -> %s\n", index, stateNames[s]);
    p.state = s;
    p.stateTimer = 0;

    switch (s)
    {
    case IDLE:
        playClip(p, "Idle", true);
        p.waitTime = randomRange(1.5f, 3.5f);   // random -> penguins don't throw in sync
        // pick a random opponent (anyone but ourselves)
        do { p.target = rand() % (int)penguins.size(); } while (p.target == index);
        break;
    case MOVE:
    // Choose a random destination within the ice rink.
    p.destination = randomDestination();

    // Start the movement animation sequence.
    p.slidePhase = SLIDE_START;
    playClip(p, "SlideStart", false);
    break;
    case AIM:
        playClip(p, "Aim", false);
        break;
    case THROW:
        playClip(p, "Throw", false);
        break;
    }
}

// Runs every frame: advances the animation, then checks the transitions
void updatePenguin(Penguin& p, int index, float dt)
{
    // Count down the time until another direction change is allowed.
    if (p.penguinAvoidanceCooldown > 0.0f)
    {
        p.penguinAvoidanceCooldown -= dt;
    }

    float prevClipTime = p.clipTime;   // remembered to detect the release moment
    advanceClip(p, dt);
    p.stateTimer += dt;
    if (p.hitTimer > 0) p.hitTimer -= dt;   // the hit flash fades out

    switch (p.state)
    {
    case IDLE:
        // Wait, then go to AIM - but only when the Idle sway passes its neutral
        // pose (t = 0, 1, 2 s), so the switch to the Aim clip doesn't jump.
        if (p.stateTimer > p.waitTime && fmodf(p.clipTime, 1.0f) < dt)
            enterState(p, index, MOVE);
        break;

    case MOVE:
    // Handle the three movement animation phases.
    switch (p.slidePhase)
    {
    case SLIDE_START:
        // Wait until the penguin is lying on its belly.
        if (clipFinished(p))
        {
            p.slidePhase = SLIDE_LOOP;
            playClip(p, "SlideLoop", true);
        }
        break;

    case SLIDE_LOOP:
        // Move towards the destination using Seek Behaviour.
        if (updateSeek(p, dt))
        {
            p.slidePhase = SLIDE_END;
            playClip(p, "SlideEnd", false);
        }
        break;

    case SLIDE_END:
        // Wait until the penguin has stood up.
        if (clipFinished(p))
            enterState(p, index, AIM);
        break;
    }
    break;

    case AIM:
    {
        // Turn towards the target while the wind-up plays (and while holding it)
        float remaining = turnTowards(p, yawTowards(p, penguins[p.target]), dt);
        if (clipFinished(p) && remaining < AIM_TOLERANCE)
            enterState(p, index, THROW);
        break;
    }

    case THROW:
        // The release moment: detect when the clock PASSES the release time
        // (never test ==, the exact time is almost never hit)
        if (prevClipTime < RELEASE_TIME && p.clipTime >= RELEASE_TIME)
            spawnSnowball(p, index, penguins[p.target]);
        if (clipFinished(p))
            enterState(p, index, IDLE);
        break;
    }
}

// ---------------------------------------------------------------------
// PART C: init, display, reshape, main
// ---------------------------------------------------------------------

void init()
{
    glClearColor(0.55f, 0.70f, 0.85f, 1.0f);   // light winter sky
    glEnable(GL_DEPTH_TEST);
    // No GL_CULL_FACE: some parts (snowmen's eyes) have negative scale,
    // which flips triangle winding, so they would vanish with culling.

    staticShader = loadShaders("static.vert", "static.frag");

    // NOTE: VectorUtils4's perspective() takes the angle in RADIANS
    // (despite the parameter name). M_PI / 3.0 = 60 degrees.
    projectionMatrix = perspective(M_PI / 3.0, 1000.0f / 800.0f, 0.1, 200);

    arena = loadStaticGltf("assets/arena.glb");
    penguinShader = loadShaders("penguin.vert", "penguin.frag");
    penguinMesh = loadPenguinMesh("assets/penguin.glb", penguinShader);

        Penguin start = {};   // all fields zero, state = IDLE
    start.position = vec3(-4.0f, GROUND_Y, 1.5f); start.yaw =  0.6f; penguins.push_back(start);
    start.position = vec3( 4.0f, GROUND_Y, 1.5f); start.yaw = -0.6f; penguins.push_back(start);
    start.position = vec3(-2.0f, GROUND_Y, 4.5f); start.yaw =  0.0f; penguins.push_back(start);

    snowballModel = LoadModel("assets/sphere.obj");   // radius 0.1 m, from lab 3
    // Step 5: the FSM takes over. Every penguin starts in IDLE.
    srand((unsigned)time(NULL));
    for (int i = 0; i < (int)penguins.size(); i++)
        enterState(penguins[i], i, IDLE);

    lastTime = glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    printError("init");
}

void display()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Calculate the camera position using spherical coordinates.
    float cameraX = cameraDistance * cosf(cameraPitch) * sinf(cameraYaw);
    float cameraY = cameraDistance * sinf(cameraPitch);
    float cameraZ = cameraDistance * cosf(cameraPitch) * cosf(cameraYaw);

    // The camera always looks towards the centre of the arena.
    mat4 viewMatrix = lookAtv(
        vec3(cameraX, cameraY, cameraZ),
        vec3(0.0f, 0.0f, 0.0f),
        vec3(0.0f, 1.0f, 0.0f)
    );

    glUseProgram(staticShader);
    uploadMat4ToShader(staticShader, "projMatrix", projectionMatrix);
    uploadMat4ToShader(staticShader, "viewMatrix", viewMatrix);

    for (StaticPart& p : arena)
    {
        uploadMat4ToShader(staticShader, "modelMatrix", p.world);
        glUniform3f(glGetUniformLocation(staticShader, "baseColor"), p.color.x, p.color.y, p.color.z);
        DrawModel(p.model, staticShader, "in_Position", "in_Normal", NULL);
    }

    // --- penguins ---
    glUseProgram(penguinShader);
    uploadMat4ToShader(penguinShader, "projMatrix", projectionMatrix);
    uploadMat4ToShader(penguinShader, "viewMatrix", viewMatrix);

        // Time since the last frame (seconds)
    float now = glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    float dt = now - lastTime;
    lastTime = now;
    if (dt > 0.05f) dt = 0.05f;   // e.g. after loading or dragging the window: avoid huge jumps

    // Step 5: let the AI of every penguin think first (may change clip and yaw)
    for (int i = 0; i < (int)penguins.size(); i++)
        updatePenguin(penguins[i], i, dt);
        // Step 6: physics for all snowballs in the air
        updateSnowballs(dt);
        updateSplats(dt);

    for (Penguin& p : penguins)
    {
        // Each penguin has its own clip and clock, so its own bone matrices
        const AnimClip& clip = penguinMesh.clips[p.clip];

        std::vector<mat4> locals, skinMatrices;
        sampleClip(penguinMesh.skeleton, clip, clip.start + p.clipTime, locals);
        computeSkinMatrices(penguinMesh.skeleton, locals, skinMatrices);

        // upload all bone matrices at once; GL_TRUE = transpose (VectorUtils4 is row-major)
        glUniformMatrix4fv(glGetUniformLocation(penguinShader, "bones"),
                           (GLsizei)skinMatrices.size(), GL_TRUE, skinMatrices[0].m);
       
        uploadMat4ToShader(penguinShader, "modelMatrix",penguinModelMatrix(p));
       // 1 right after a hit, fading to 0 (max(.., 0) because the timer can dip below 0)
        float flash = fmaxf(p.hitTimer, 0.0f) / HIT_FLASH_TIME;
        glUniform1f(glGetUniformLocation(penguinShader, "hitFlash"), flash);
        drawPenguinMesh(penguinMesh);
    }
    // --- snowballs (static shader, white) ---
    glUseProgram(staticShader);
    glUniform3f(glGetUniformLocation(staticShader, "baseColor"), 0.95f, 0.95f, 1.0f);
    for (Snowball& b : snowballs)
    {
        uploadMat4ToShader(staticShader, "modelMatrix", T(b.position.x, b.position.y, b.position.z));
        DrawModel(snowballModel, staticShader, "in_Position", "in_Normal", NULL);
    }
    
    // --- splats: the same sphere, flattened into a disc on the ice ---
    for (Splat& s : splats)
    {
        // full size, then shrink to nothing during the last SPLAT_SHRINK seconds
        float size = fminf(1.0f, (SPLAT_LIFE - s.age) / SPLAT_SHRINK);
        mat4 m = T(s.position.x, s.position.y, s.position.z) * S(2.5f * size, 0.15f, 2.5f * size);
        uploadMat4ToShader(staticShader, "modelMatrix", m);
        DrawModel(snowballModel, staticShader, "in_Position", "in_Normal", NULL);
    }

    printError("display");
    glutSwapBuffers();
}

void reshape(int w, int h)
{
    glViewport(0, 0, w, h);
    projectionMatrix = perspective(M_PI / 3.0, (float)w / (float)h, 0.1, 200);
}


 // Called when a mouse button is pressed or released.
 
 // Called when a mouse button is pressed or released.
 void mouse(int button, int state, int x, int y)
 {
     // Rotate the camera with the left mouse button.
     if (button == GLUT_LEFT_BUTTON)
     {
         if (state == GLUT_DOWN)
         {
             isDragging = true;
             lastMouseX = x;
             lastMouseY = y;
         }
         else if (state == GLUT_UP)
         {
             isDragging = false;
         }
     }

     // Zoom using the mouse wheel.
     // Button 3 = scroll up, button 4 = scroll down.
     if (state == GLUT_DOWN)
     {
         if (button == 3)
             cameraDistance -= 1.0f;
         else if (button == 4)
             cameraDistance += 1.0f;

         // Keep the camera within a reasonable distance.
         cameraDistance = std::clamp(cameraDistance, 8.0f, 45.0f);

         glutPostRedisplay();
     }
 }


 // Called when the mouse moves while a button is pressed.
 void mouseMotion(int x, int y)
 {
     if (!isDragging)
         return;

     // Calculate how far the mouse has moved.
     int dx = x - lastMouseX;
     int dy = y - lastMouseY;

     // Update the camera angles.
     const float sensitivity = 0.005f;

     cameraYaw -= dx * sensitivity;
     cameraPitch += dy * sensitivity;

     // Prevent the camera from flipping upside down.
     cameraPitch = std::clamp(cameraPitch, -1.4f, 1.4f);

     // Store the current mouse position.
     lastMouseX = x;
     lastMouseY = y;

     glutPostRedisplay();
 }


 // Zoom the camera using the keyboard.
 void keyboard(unsigned char key, int x, int y)
 {
     if (key == '+' || key == '=')
         cameraDistance -= 1.0f;

     else if (key == '-')
         cameraDistance += 1.0f;

     // Keep the camera within a reasonable distance.
     cameraDistance = std::clamp(cameraDistance, 8.0f, 45.0f);

     glutPostRedisplay();
 }


int main(int argc, char* argv[])
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DEPTH | GLUT_DOUBLE);
    glutInitWindowSize(1000, 800);
    glutInitContextVersion(3, 2);
    glutCreateWindow("Penguin Snowdown");
    
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);

    // Register mouse callbacks for orbit camera controls.
    glutMouseFunc(mouse);
    glutMotionFunc(mouseMotion);
    glutKeyboardFunc(keyboard);

    glutRepeatingTimer(20);

    init();
    glutMainLoop();
    return 0;
}