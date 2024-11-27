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

  etna::create_program(
      "terrain_shader",
      {TERRAIN_RENDERER_SHADERS_ROOT "terrain.vert.spv",
       TERRAIN_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
       TERRAIN_RENDERER_SHADERS_ROOT "terrain.tese.spv",
       TERRAIN_RENDERER_SHADERS_ROOT "terrain.frag.spv"});
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = sceneMgr->getVertexFormatDescription(),
    }},
  };

  auto& pipelineManager = etna::get_context().getPipelineManager();

  cullingPipeline = {};
  cullingPipeline = pipelineManager.createComputePipeline("culling_shader", {});

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

  terrainPipeline = {};
  terrainPipeline = pipelineManager.createGraphicsPipeline(
    "terrain_shader",
    etna::GraphicsPipeline::CreateInfo{
      .inputAssemblyConfig = {.topology = vk::PrimitiveTopology::ePatchList},
      .tessellationConfig = {.patchControlPoints = 4},
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
}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  // calc camera matrix
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    pushConstMC.projView = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
    pushConstMC.cameraPos = packet.mainCam.position;
  }
}

void WorldRenderer::cullScene(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  {
    vk::BufferMemoryBarrier2 barriers[] = {{}, {}, {}};

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = sceneMgr->getMatricesBuffer()->get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = sceneMgr->getDrawMatricesIndBuffer()->get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[2] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
      .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = sceneMgr->getDrawCmdBuffer()->get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    vk::DependencyInfo depInfo{
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = 3,
      .pBufferMemoryBarriers = barriers,
    };

    cmd_buf.pipelineBarrier2(depInfo);
  }

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

  {
    vk::BufferMemoryBarrier2 barriers[] = {{}, {}, {}};

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = sceneMgr->getMatricesBuffer()->get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = sceneMgr->getDrawMatricesIndBuffer()->get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[2] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
      .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
      .buffer = sceneMgr->getDrawCmdBuffer()->get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    vk::DependencyInfo depInfo{
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = 3,
      .pBufferMemoryBarriers = barriers,
    };

    cmd_buf.pipelineBarrier2(depInfo);
  }
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
  auto simpleGraphicsInfo = etna::get_shader_program("terrain_shader");

  auto set = etna::create_descriptor_set(
    simpleGraphicsInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{
        0,
        sceneMgr->getHieghtMapImage()->genBinding(
        sceneMgr->getHieghtMapSampler()->get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
    });

  vk::DescriptorSet vkSet = set.getVkSet();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainPipeline.getVkPipeline());

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants(
    pipeline_layout,
    vk::ShaderStageFlagBits::eTessellationEvaluation |
    vk::ShaderStageFlagBits::eTessellationControl,
    0,
    sizeof(PushConstants),
    &pushConstMC);

  cmd_buf.draw(4, 64*64, 0, 0);
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    cullScene(cmd_buf, cullingPipeline.getVkPipelineLayout());

    {
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = target_image, .view = target_image_view}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

      renderScene(cmd_buf, staticMeshPipeline.getVkPipelineLayout());
    }

    {
      vk::ImageMemoryBarrier2 barriers[] = {{}};

      barriers[0] = vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .image = target_image,
        .subresourceRange = {
          .aspectMask = vk::ImageAspectFlagBits::eColor, 
          .levelCount = 1,
          .layerCount = 1,
        }
      };

      vk::DependencyInfo depInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = barriers,
      };

      cmd_buf.pipelineBarrier2(depInfo);
    }

    {
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = target_image,
          .view = target_image_view,
          .loadOp = vk::AttachmentLoadOp::eLoad}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

      renderTerrain(cmd_buf, terrainPipeline.getVkPipelineLayout());
    }
  }
}
