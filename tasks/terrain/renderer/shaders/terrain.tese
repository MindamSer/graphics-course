#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable

layout(triangles, equal_spacing, ccw) in;

void main()
{
	vec4 position = (
	gl_TessCoord.x * gl_in[0].gl_Position +
	gl_TessCoord.y * gl_in[1].gl_Position +
	gl_TessCoord.z * gl_in[2].gl_Position);

	gl_Position = position;
}