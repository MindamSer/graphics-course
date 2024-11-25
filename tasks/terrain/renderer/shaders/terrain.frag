#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable

layout (location = 0 ) in VS_OUT
{
  vec3 pos;
  vec3 norm;
} surf;

layout(location = 0) out vec4 color;

void main()
{
	vec3 ligthPos = vec3(10, 10, 10);
	vec3 pl = normalize(ligthPos - surf.pos);
	float intens = max(0.0, dot(pl, surf.norm)) * 0.9 + 0.1;

	color = vec4(vec3(intens), 1.0);
}