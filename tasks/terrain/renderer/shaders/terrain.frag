#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable

layout(binding = 0) uniform sampler2D colorTex;

layout(location = 0) out vec4 color;

void main()
{
	color = vec4(1.0, 0, 0, 1.0);
}