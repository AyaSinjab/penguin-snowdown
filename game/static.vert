#version 150

in vec3 in_Position;
in vec3 in_Normal;

uniform mat4 projMatrix;
uniform mat4 viewMatrix;
uniform mat4 modelMatrix;

out vec3 worldNormal;

void main(void)
{
    // Inverse-transpose: the arena parts have NON-uniform scale
    // (e.g. Snow_base 13 x 10 x 0.5). Without it the normals tilt
    // and the lighting looks wrong.
    worldNormal = mat3(transpose(inverse(modelMatrix))) * in_Normal;

    gl_Position = projMatrix * viewMatrix * modelMatrix * vec4(in_Position, 1.0);
}
