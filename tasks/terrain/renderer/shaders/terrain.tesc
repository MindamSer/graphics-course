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
  int tileCount = 64;
  float tileSize = 16.;

  if(gl_InvocationID == 0)
  {
  	vec2 maxPoint = max(
  	max(gl_in[0].gl_Position.xz,gl_in[1].gl_Position.xz),
  	max(gl_in[2].gl_Position.xz,gl_in[3].gl_Position.xz));
  	vec2 minPoint = min(
  	min(gl_in[0].gl_Position.xz,gl_in[1].gl_Position.xz),
  	min(gl_in[2].gl_Position.xz,gl_in[3].gl_Position.xz));
  	vec2 farPoint = max(abs(maxPoint), abs(minPoint));
  	vec2 nearPoint = min(abs(maxPoint), abs(minPoint));
  
  	float maxDist = max(farPoint.x, farPoint.y);
  	
  	int k = 1;
  	while(maxDist > 4 * tileSize)
  	{
  	  maxDist -= 4 * tileSize;
  	  k *= 2;
  	}
  	
  	gl_TessLevelOuter[0] = 64 / min(k, 64);
  	gl_TessLevelOuter[1] = 64 / min(k, 64);
  	gl_TessLevelOuter[2] = 64 / min(k, 64);
  	gl_TessLevelOuter[3] = 64 / min(k, 64);
  	gl_TessLevelInner[0] = 64 / min(k, 64);
  	gl_TessLevelInner[1] = 64 / min(k, 64);
  }
  
  gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}