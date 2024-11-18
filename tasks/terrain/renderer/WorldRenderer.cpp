#include "WorldRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>
#include <glm/ext.hpp>


WorldRenderer::WorldRenderer()
  : sceneMgr{std::make_unique<SceneManager>()}
{
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();

  mainViewDepth = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "main_view_depth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
  });
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  sceneMgr->selectScene(path);

  pushConstMC.instanceCount = static_cast<std::uint32_t>(sceneMgr->getInstanceMatrices().size());
  pushConstMC.relemCount = static_cast<std::uint32_t>(sceneMgr->getRenderElements().size());
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "static_mesh_material",
    {TERRAIN_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     TERRAIN_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("static_mesh", {TERRAIN_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("culling_shader", {TERRAIN_RENDERER_SHADERS_ROOT "gpu_culling.comp.spv"});
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = sceneMgr->getVertexFormatDescription(),
    }},
  };

  auto& pipelineManager = etna::get_context().getPipelineManager();

  staticMeshPipeline = {};
  staticMeshPipeline = pipelineManager.createGraphicsPipeline(
    "static_mesh_material",
    etna::GraphicsPipeline::CreateInfo{
      .vertexShaderInput = sceneVertexInputDesc,
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {swapchain_format},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  cullingPipeline = pipelineManager.createComputePipeline("culling_shader", {});
}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  // calc camera matrix
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
  }
}

void WorldRenderer::cullScene(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  auto simpleComputeInfo = etna::get_shader_program("culling_shader");

  auto set = etna::create_descriptor_set(
    simpleComputeInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{0, sceneMgr->getRelemBoxBuffer()->genBinding()},
      etna::Binding{1, sceneMgr->getMeshBuffer()->genBinding()},
      etna::Binding{2, sceneMgr->getMatricesBuffer()->genBinding()},
      etna::Binding{3, sceneMgr->getIMeshesBuffer()->genBinding()},
      etna::Binding{4, sceneMgr->getDrawCmdBuffer()->genBinding()},
      etna::Binding{5, sceneMgr->getDrawMatricesIndBuffer()->genBinding()},
      etna::Binding{6, sceneMgr->getMatricesOffsetsIndBuffer()->genBinding()},
    });

  vk::DescriptorSet vkSet = set.getVkSet();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cullingPipeline.getVkPipeline());

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 
    0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants(
    pipeline_layout, vk::ShaderStageFlagBits::eCompute, 
    0, sizeof(PushConstants), &pushConstMC);

  etna::flush_barriers(cmd_buf);

  cmd_buf.dispatch((pushConstMC.instanceCount + 255) / 256, 1, 1);
}

void WorldRenderer::renderScene(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  if (!sceneMgr->getVertexBuffer())
    return;

  auto simpleGraphicsInfo = etna::get_shader_program("static_mesh_material");

  auto set = etna::create_descriptor_set(
    simpleGraphicsInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{0, sceneMgr->getMatricesBuffer()->genBinding()},
      etna::Binding{1, sceneMgr->getDrawMatricesIndBuffer()->genBinding()},
    });

  vk::DescriptorSet vkSet = set.getVkSet();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());

  cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, 
    pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants(
    pipeline_layout, vk::ShaderStageFlagBits::eVertex, 
    0, sizeof(PushConstants), &pushConstMC);

  etna::flush_barriers(cmd_buf);

  cmd_buf.drawIndexedIndirect(
    sceneMgr->getDrawCmdBuffer()->get(), 0, static_cast<std::uint32_t>(sceneMgr->getRenderElements().size()), 0);
}

void WorldRenderer::renderTerrain(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  auto& t1 = cmd_buf;
  t1 = t1;
  auto& t2 = pipeline_layout;
  t2 = t2;
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    pushConstMC.projView = worldViewProj;

    cullScene(cmd_buf, cullingPipeline.getVkPipelineLayout());

    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {resolution.x, resolution.y}},
      {{.image = target_image, .view = target_image_view}},
      {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

    renderScene(cmd_buf, staticMeshPipeline.getVkPipelineLayout());

    // renderTerrain(cmd_buf, staticMeshPipeline.getVkPipelineLayout());
  }
}
