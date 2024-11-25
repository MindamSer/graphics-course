#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable

layout(vertices = 4) out;

layout(push_constant) uniform params
{
  mat4 mProjView;
  vec3 cameraPos;
  uint instanceCount;
  uint relemCount;
} pushConstant;

void main()
{
	if(gl_InvocationID == 0)
	{
		vec3 ro = max(
		max(abs(gl_in[0].gl_Position),abs(gl_in[1].gl_Position)),
		max(abs(gl_in[2].gl_Position),abs(gl_in[3].gl_Position))).xyz;

		int maxDist = (int(max(ro.x, ro.z))) / 16;

		int k = 1;
		while(maxDist > 1)
		{
			--maxDist;
			k *= 2;
		}

		gl_TessLevelOuter[0] = 32 / k;
		gl_TessLevelOuter[1] = 32 / k;
		gl_TessLevelOuter[2] = 32 / k;
		gl_TessLevelOuter[3] = 32 / k;
		gl_TessLevelInner[0] = 32 / k;
		gl_TessLevelInner[1] = 32 / k;
	}

	gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}