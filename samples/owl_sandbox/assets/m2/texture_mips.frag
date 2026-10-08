#version 450

layout(location = 0) in vec2 vertexUv;
layout(set = 0, binding = 0) uniform sampler2D textureSampler;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform MipDraw
{
    float scale;
    float lod;
} draw;

void main()
{
    outColor = draw.lod < 0.0
        ? texture(textureSampler, vertexUv)
        : textureLod(textureSampler, vertexUv, draw.lod);
}
