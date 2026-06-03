#version 450

layout(binding = 0) uniform CameraBuffer {
    mat4 viewProj;
} ubo;

// colorMode: 0=velocity, 1=mass, 2=kinetic energy, 3=rainbow
layout(push_constant) uniform PC {
    uint colorMode;
} pc;

layout(location = 0) in vec4 inPos;
layout(location = 1) in vec4 inVel;

layout(location = 0) out vec4 outColor;

// Map a value [0,1] to a fire-like gradient: black → red → orange → yellow → white
vec3 fireRamp(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 a = vec3(0.0,  0.0,  0.0);
    vec3 b = vec3(0.7,  0.0,  0.0);
    vec3 c = vec3(1.0,  0.4,  0.0);
    vec3 d = vec3(1.0,  0.95, 0.4);
    if (t < 0.333)      return mix(a, b, t * 3.0);
    else if (t < 0.666) return mix(b, c, (t - 0.333) * 3.0);
    else                return mix(c, d, (t - 0.666) * 3.0);
}

// Simple hue-to-rgb for rainbow palette
vec3 hueToRGB(float h) {
    h = fract(h) * 6.0;
    float r = abs(h - 3.0) - 1.0;
    float g = 2.0 - abs(h - 2.0);
    float b = 2.0 - abs(h - 4.0);
    return clamp(vec3(r, g, b), 0.0, 1.0);
}

void main() {
    gl_Position = ubo.viewProj * vec4(inPos.xyz, 1.0);

    float mass       = inPos.w;
    float velocityMag = length(inVel.xyz);
    float kineticE   = 0.5 * mass * dot(inVel.xyz, inVel.xyz);

    vec3 color;

    if (pc.colorMode == 0u) {
        // Velocity magnitude: slow=blue, fast=red, heavy=yellow
        float t_vel  = clamp(velocityMag * 0.1, 0.0, 1.0);
        float t_mass = clamp(mass * 0.01, 0.0, 1.0);
        vec3 slowColor  = vec3(0.1, 0.4, 1.0);
        vec3 fastColor  = vec3(1.0, 0.2, 0.1);
        vec3 heavyColor = vec3(1.0, 0.8, 0.1);
        color = mix(mix(slowColor, fastColor, t_vel), heavyColor, t_mass);

    } else if (pc.colorMode == 1u) {
        // Mass: light=cool teal, heavy=saturated gold
        float t = clamp(mass * 0.02, 0.0, 1.0);
        color = mix(vec3(0.05, 0.55, 0.75), vec3(1.0, 0.75, 0.1), t);

    } else if (pc.colorMode == 2u) {
        // Kinetic energy proxy → fire ramp
        float t = clamp(kineticE * 0.05, 0.0, 1.0);
        color = fireRamp(t);

    } else {
        // Rainbow: hash vertex index to a hue
        float hue = fract(float(gl_VertexIndex) * 0.618033988);
        color = hueToRGB(hue);
    }

    outColor     = vec4(color, 1.0);
    gl_PointSize = clamp(2.0 + mass * 0.5, 2.0, 12.0);
}
