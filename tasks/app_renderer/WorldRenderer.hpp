#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/GraphicsPipeline.hpp>
#include "etna/DescriptorSet.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <chrono>

#include "scene/SceneManager.hpp"
#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"

#include "ResourceManager.hpp"


class WorldRenderer
{
public:
  WorldRenderer();

  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupPipelines(vk::Format swapchain_format);

  void loadScene(std::filesystem::path path);
  void update(const FramePacket& packet);
  void debugInput(const Keyboard& kb);
  void drawGui();

  void renderWorld(
    vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view);

private:
  void cullScene(vk::CommandBuffer cmd_buf);
  void updateParticles(vk::CommandBuffer cmd_buf);
  void renderScene(vk::CommandBuffer cmd_buf);
  void renderTerrain(vk::CommandBuffer cmd_buf);
  void renderParticles(vk::CommandBuffer cmd_buf);
  void computeSSAO(vk::CommandBuffer cmd_buf);
  void deferredShading(vk::CommandBuffer cmd_buf);
  void postProcess(vk::CommandBuffer cmd_buf);
  void copyHDRtoLDR(vk::CommandBuffer cmd_buf);

  float getDeltaTime();

private:
  std::unique_ptr<SceneManager> sceneMgr;
  std::unique_ptr<ResourceManager> resourceMgr;

  bool enableScene = true;
  bool enableTerrain = false;
  bool enableParticles = true;
  bool enableSSAO = false;

  bool drawBoundingBoxes = false;
  bool drawLights = false;
  bool drawEmitters = false;


  glm::uvec2 resolution;
  etna::Image mainViewDepth;
  etna::Sampler quadSampler;
  std::chrono::system_clock::time_point lastTime;


  etna::ComputePipeline cullingPipeline;
  etna::PersistentDescriptorSet cullingDescSet;

  etna::GraphicsPipeline staticMeshPipeline;
  etna::PersistentDescriptorSet staticMeshDescSet;

  etna::GraphicsPipeline terrainPipeline;

  etna::ComputePipeline particlesUpdatePipeline;
  etna::GraphicsPipeline particlesDrawPipeline;
  etna::PersistentDescriptorSet particlesUpdateDescSet;
  etna::PersistentDescriptorSet particlesDrawDescSet;

  etna::GraphicsPipeline ssaoCalculationPipeline;
  etna::ComputePipeline ssaoBlurPipeline;
  etna::PersistentDescriptorSet ssaoCalculationDescSet;
  etna::PersistentDescriptorSet ssaoBlurDescSet;

  etna::GraphicsPipeline defferedShadingPipeline;
  etna::PersistentDescriptorSet deferredDescSet;

  etna::ComputePipeline tonmap0Pipeline;
  etna::ComputePipeline tonmap1Pipeline;
  etna::ComputePipeline tonmap2Pipeline;

  etna::GraphicsPipeline HDRtoLDRPipeline;


  struct RenderConstants
  {
    glm::mat4x4 proj;
    glm::mat4x4 view;
    glm::mat4x4 projView;
    std::uint32_t instanceCount;
    std::uint32_t relemCount;
    std::uint32_t lightsCount;
    std::uint32_t emittersCount;
    glm::vec3 cameraPos;
  } renderConstants;

  struct CullingPushConstants
  {
    glm::mat4x4 projView;
    std::uint32_t instanceCount;
    std::uint32_t relemCount;
  } cullingPC;

  struct ScenePushConstants
  {
    glm::mat4x4 projView;
  } scenePC;

  struct TerrainPushConstants
  {
    glm::mat4x4 projView;
    glm::vec3 cameraPos;
  } terrainPC;

  struct ParticlesPushConstants
  {
    glm::mat4x4 projView;
    glm::vec4 cameraPos;
    std::uint32_t emittersCount;
    float dt;
  } particlesPC;

  struct SSAOPushConstants
  {
    glm::mat4x4 proj;
    glm::mat4x4 view;
    glm::uvec2 res;
  } ssaoPC;

  struct DeferredPushConstants
  {
    glm::mat4x4 proj;
    glm::mat4x4 view;
    std::uint32_t lightsCount;
  } deferredPC;
};
