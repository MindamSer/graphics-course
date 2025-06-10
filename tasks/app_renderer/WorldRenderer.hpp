#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <glm/glm.hpp>
#include <memory>

#include "scene/SceneManager.hpp"
#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"

#include "ResourceManager.hpp"


class WorldRenderer
{
public:
  WorldRenderer();

  void loadScene(std::filesystem::path path);

  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupPipelines(vk::Format swapchain_format);

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(
    vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view);

private:
  void cullScene(vk::CommandBuffer cmd_buf);
  void renderScene(vk::CommandBuffer cmd_buf);
  void renderTerrain(vk::CommandBuffer cmd_buf);
  void deferredShading(vk::CommandBuffer cmd_buf);
  void postProcess(vk::CommandBuffer cmd_buf);
  void copyHDRtoLDR(vk::CommandBuffer cmd_buf);

private:
  std::unique_ptr<SceneManager> sceneMgr;
  std::unique_ptr<ResourceManager> resourceMgr;


  glm::uvec2 resolution;
  etna::Image mainViewDepth;
  etna::Sampler quadSampler;


  etna::ComputePipeline cullingPipeline;

  etna::GraphicsPipeline staticMeshPipeline;

  etna::GraphicsPipeline terrainPipeline;

  etna::GraphicsPipeline defferedShadingPipeline;

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

  struct DeferredPushConstants
  {
    glm::mat4x4 proj;
    glm::mat4x4 view;
    std::uint32_t lightsCount;
  } deferredPC;
};
