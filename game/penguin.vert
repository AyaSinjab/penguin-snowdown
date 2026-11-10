#version 150

in vec3 in_Position;
in vec3 in_Normal;
in vec3 in_Color;      // the vertex colours you painted in Blender (COLOR_0)
in vec4 in_Joints;     // the 4 bones that affect this vertex (indices 0..7)
in vec4 in_Weights;    // how much each of those bones pulls (sum = 1)

uniform mat4 projMatrix;
uniform mat4 viewMatrix;
uniform mat4 modelMatrix;
uniform mat4 bones[8]; // skin matrices = jointWorld * inverseBind

out vec3 worldNormal;
out vec3 vertColor;

void main(void)
{
    // Linear blend skinning: weighted sum of the 4 bone matrices
    mat4 skin = in_Weights.x * bones[int(in_Joints.x)]
              + in_Weights.y * bones[int(in_Joints.y)]
              + in_Weights.z * bones[int(in_Joints.z)]
              + in_Weights.w * bones[int(in_Joints.w)];

    vec4 skinnedPos    = skin * vec4(in_Position, 1.0);
    vec3 skinnedNormal = mat3(skin) * in_Normal;

    worldNormal = mat3(transpose(inverse(modelMatrix))) * skinnedNormal;
    vertColor   = in_Color;

    gl_Position = projMatrix * viewMatrix * modelMatrix * skinnedPos;
}