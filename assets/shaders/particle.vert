#version 450

layout(binding = 0) uniform CameraBuffer {
    mat4 viewProj;
} ubo;

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec4 inVel;

layout(location = 0) out vec4 outColor;

void main() {
    gl_Position = ubo.viewProj * vec4(inPos.xyz, 1.0);

    float mass = inPos.w;
    float velocityMag = length(inVel.xyz);

    float t_vel = clamp(velocityMag * 0.1, 0.0, 1.0);
    float t_mass = clamp(mass * 0.01, 0.0, 1.0);

    vec3 slowColor = vec3(0.1, 0.4, 1.0);
    vec3 fastColor = vec3(1.0, 0.2, 0.1);
    vec3 heavyColor = vec3(1.0, 0.8, 0.1);

    vec3 color = mix(slowColor, fastColor, t_vel);
    color = mix(color, heavyColor, t_mass);

    outColor = vec4(color, 1.0);

    gl_PointSize = clamp(2.0 + mass * 0.5, 2.0, 12.0);
}
