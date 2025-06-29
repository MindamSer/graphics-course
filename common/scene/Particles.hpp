#pragma once

#include <glm/glm.hpp>


struct Particle
{
  glm::vec4 position;
  glm::vec4 velocity;
  glm::vec4 color;

  glm::uint32 textureId;
  float textureScale;
  float gravityScale;
  float lifetime;
};

struct ParticleEmitter
{
  glm::vec4 position = {};
  glm::vec4 initialVelocity = {};
  glm::vec4 particleColor = {};

  glm::uint32 textureId = 0;
  float particleScale = 1.0f;
  float particleGravityK = 1.0f;
  float particleLifetime = 1.0f;

  float spawnFreq = 1.0f;

  glm::uint32 _pad0 = 0;
  glm::uint32 _pad1 = 0;
  glm::uint32 _pad2 = 0;
};

struct ParticleEmitterRuntime
{
  glm::uint32 particleCount = 0;
  float cooldown = 0.0f;
};
