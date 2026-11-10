#version 150

in vec3 worldNormal;
in vec3 vertColor;

uniform float hitFlash;   // 0 = normal, 1 = just got hit (step 6c)

out vec4 out_Color;

void main(void)
{
    vec3 n = normalize(worldNormal);
    vec3 lightDir = normalize(vec3(0.4, 1.0, 0.3));   // same sun as the arena

    float diffuse = max(dot(n, lightDir), 0.0);
    vec3 color = vertColor * (0.3 + 0.7 * diffuse);

    // hit feedback: blend towards red (in linear space, before gamma)
    color = mix(color, vec3(0.9, 0.05, 0.05), 0.75 * hitFlash);

    color = pow(color, vec3(1.0 / 2.2));   // linear -> screen (gamma)
    out_Color = vec4(color, 1.0);
}