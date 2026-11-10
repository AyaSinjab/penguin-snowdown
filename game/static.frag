#version 150

in vec3 worldNormal;

uniform vec3 baseColor;

out vec4 out_Color;

void main(void)
{
    vec3 n = normalize(worldNormal);
    vec3 lightDir = normalize(vec3(0.4, 1.0, 0.3));   // sun from above, WORLD space

    float diffuse = max(dot(n, lightDir), 0.0);
    vec3 color = baseColor * (0.3 + 0.7 * diffuse);   // ambient + diffuse <= 1

    color = pow(color, vec3(1.0 / 2.2));              // glTF colours are linear -> gamma
    out_Color = vec4(color, 1.0);
}
