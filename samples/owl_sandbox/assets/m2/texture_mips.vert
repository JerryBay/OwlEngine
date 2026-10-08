#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 0) out vec2 vertexUv;

layout(push_constant) uniform MipDraw
{
    float scale;
    float lod;
} draw;

void main()
{
    gl_Position = vec4(inPosition * draw.scale, 0.0, 1.0);
    vertexUv = inUv;
}
