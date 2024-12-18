#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <glm/glm.hpp>

#include "scene/SceneManager.hpp"
#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"


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
  void cullScene(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);
  void renderScene(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);
  void renderTerrain(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);
  void deferredShading(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);
  void postProcess(
    vk::CommandBuffer cmd_buf);
  void copyHDRtoLDR(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);


private:
  std::unique_ptr<SceneManager> sceneMgr;

  etna::Image mainViewDepth;
  etna::Buffer constants;

  struct renderConstants
  {
    glm::uvec2 res;
    glm::mat4x4 projView;
    glm::vec3 cameraPos;
    std::uint32_t instanceCount;
    std::uint32_t relemCount;
  } renderConstants;

  struct cullingPushConstants
  {
    glm::mat4x4 mProjView;
    std::uint32_t instanceCount;
    std::uint32_t relemCount;
  } cullingPC;

  struct terrainTesEvalPushConstants
  {
    glm::mat4x4 mProjView;
    glm::vec3 cameraPos;
  } terranTesPC;

  glm::mat4x4 worldViewProj;
  glm::mat4x4 lightMatrix;

  etna::ComputePipeline cullingPipeline{};
  etna::GraphicsPipeline staticMeshPipeline{};
  etna::GraphicsPipeline terrainPipeline{};

  glm::uvec2 resolution;

  etna::Image HDRImage;
  etna::Sampler HDRSampler;
  etna::GraphicsPipeline HDRtoLDRPipeline{};

  etna::ComputePipeline tonmap0Pipeline;
  etna::ComputePipeline tonmap1Pipeline;
  etna::ComputePipeline tonmap2Pipeline;
  etna::Buffer maxLuminanceBuffer;
  etna::Buffer luminanceHistBuffer;

  struct GBuffer
  {
    etna::Image Albedo;
    etna::Image Normal;
    etna::Image Depth;
  } gBuffer;

  etna::GraphicsPipeline defferedShadingPipeline{};
};
