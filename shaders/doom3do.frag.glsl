#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 2, binding = 0) uniform sampler2D uTexture;

layout(std140, set = 3, binding = 0) uniform Context
{
    vec4 resolution;   // x/y = 320x200, z/w = inverse dimensions
    vec4 scanline;
    vec4 bloom;
    vec4 display;
} context;

float sat(float x) { return clamp(x, 0.0, 1.0); }
vec3 sat3(vec3 x) { return clamp(x, vec3(0.0), vec3(1.0)); }

vec3 sample_rgb(vec2 uv)
{
    vec2 d = vec2(context.bloom.z * context.resolution.z, 0.0);
    float r = texture(uTexture, clamp(uv + d, 0.0, 1.0)).r;
    float g = texture(uTexture, clamp(uv,     0.0, 1.0)).g;
    float b = texture(uTexture, clamp(uv - d, 0.0, 1.0)).b;
    return vec3(r, g, b) * vColor.rgb;
}

vec3 sample_light(vec2 uv)
{
    vec2 t = context.resolution.zw;
    vec3 c = texture(uTexture, clamp(uv, 0.0, 1.0)).rgb;
    vec3 n = texture(uTexture, clamp(uv + vec2(0.0, t.y), 0.0, 1.0)).rgb;
    vec3 s = texture(uTexture, clamp(uv - vec2(0.0, t.y), 0.0, 1.0)).rgb;
    vec3 e = texture(uTexture, clamp(uv + vec2(t.x, 0.0), 0.0, 1.0)).rgb;
    vec3 w = texture(uTexture, clamp(uv - vec2(t.x, 0.0), 0.0, 1.0)).rgb;
    return (c * 2.0 + n + s + e + w) / 6.0;
}

void main()
{
    vec2 uv = clamp(vTexCoord, 0.0, 1.0);
    vec2 t = context.resolution.zw;

    // Work in approximate display light space.
    vec3 color = max(sample_rgb(uv), vec3(0.0));
    color = pow(color, vec3(context.bloom.w));

    float y = uv.y * context.resolution.y;
    float row = fract(y);
    float beam = pow(sin(3.14159265 * row), 0.72);
    float scan = mix(1.0, beam, context.scanline.x);
    color *= scan;

    float tri = mod(floor(gl_FragCoord.x), 3.0);
    vec3 mask = (tri < 1.0) ? vec3(1.0, 0.90, 0.90) :
                (tri < 2.0) ? vec3(0.90, 1.0, 0.90) :
                              vec3(0.90, 0.90, 1.0);
    color *= mix(vec3(1.0), mask, context.scanline.z);
    color *= context.scanline.w;

    // Soft beam spread / bloom around bright areas.
    vec3 light = sample_light(uv);
    vec3 highlight = max(light - vec3(0.68), vec3(0.0));
    color += highlight * context.bloom.x * 0.35;

    vec2 p = uv - 0.5;
    float edge = smoothstep(context.display.z, 0.98, length(p) * 1.4142);
    color *= 1.0 - edge * context.display.y;

    color *= context.display.x;
    color = pow(max(color, vec3(0.0)), vec3(1.0 / max(context.bloom.w, 0.01)));
    FragColor = vec4(sat3(color), 1.0);
}
