struct PSInput
{
    float4 vColor : TEXCOORD0;
    float2 vTexCoord : TEXCOORD1;
    float4 position : SV_Position;
};

Texture2D<float4> uTexture : register(t0, space2);
SamplerState uTextureSampler : register(s0, space2);

cbuffer Context : register(b0, space3)
{
    float4 resolution;
    float4 scanline;
    float4 bloom;
    float4 display;
};

float sat(float x)
{
    return clamp(x, 0.0, 1.0);
}

float3 sat3(float3 x)
{
    return clamp(x, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));
}

float3 sample_rgb(float2 uv, float4 vColor)
{
    float2 d = float2(bloom.z * resolution.z, 0.0);
    float r = uTexture.Sample(uTextureSampler, clamp(uv + d, 0.0, 1.0)).r;
    float g = uTexture.Sample(uTextureSampler, clamp(uv, 0.0, 1.0)).g;
    float b = uTexture.Sample(uTextureSampler, clamp(uv - d, 0.0, 1.0)).b;
    return float3(r, g, b) * vColor.rgb;
}

float3 sample_light(float2 uv)
{
    float2 t = resolution.zw;
    float3 c = uTexture.Sample(uTextureSampler, clamp(uv, 0.0, 1.0)).rgb;
    float3 n = uTexture.Sample(uTextureSampler, clamp(uv + float2(0.0, t.y), 0.0, 1.0)).rgb;
    float3 s = uTexture.Sample(uTextureSampler, clamp(uv - float2(0.0, t.y), 0.0, 1.0)).rgb;
    float3 e = uTexture.Sample(uTextureSampler, clamp(uv + float2(t.x, 0.0), 0.0, 1.0)).rgb;
    float3 w = uTexture.Sample(uTextureSampler, clamp(uv - float2(t.x, 0.0), 0.0, 1.0)).rgb;
    return (c * 2.0 + n + s + e + w) / 6.0;
}

float4 main(PSInput input) : SV_Target
{
    float2 uv = clamp(input.vTexCoord, 0.0, 1.0);
    float2 t = resolution.zw;

    float3 color = max(sample_rgb(uv, input.vColor), float3(0.0, 0.0, 0.0));
    color = pow(color, float3(bloom.w, bloom.w, bloom.w));

    float y = uv.y * resolution.y;
    float row = frac(y);
    float beam = pow(sin(3.14159265 * row), 0.72);
    float scan = lerp(1.0, beam, scanline.x);
    color *= scan;

    float tri = fmod(floor(input.position.x), 3.0);
    float3 mask;

    if (tri < 1.0)
    {
        mask = float3(1.0, 0.90, 0.90);
    }
    else if (tri < 2.0)
    {
        mask = float3(0.90, 1.0, 0.90);
    }
    else
    {
        mask = float3(0.90, 0.90, 1.0);
    }

    color *= lerp(float3(1.0, 1.0, 1.0), mask, scanline.z);
    color *= scanline.w;

    float3 light = sample_light(uv);
    float3 highlight = max(light - float3(0.68, 0.68, 0.68), float3(0.0, 0.0, 0.0));
    color += highlight * bloom.x * 0.35;

    float2 p = uv - float2(0.5, 0.5);
    float edge = smoothstep(display.z, 0.98, length(p) * 1.4142);
    color *= 1.0 - edge * display.y;

    color *= display.x;
    float gammaValue = max(bloom.w, 0.01);
    color = pow(max(color, float3(0.0, 0.0, 0.0)),
                float3(1.0 / gammaValue, 1.0 / gammaValue, 1.0 / gammaValue));

    return float4(sat3(color), 1.0);
}
