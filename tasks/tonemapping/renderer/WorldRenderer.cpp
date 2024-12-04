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

  HDRImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "HDR_image",
    .format = vk::Format::eB10G11R11UfloatPack32,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | 
                  vk::ImageUsageFlagBits::eSampled |
                  vk::ImageUsageFlagBits::eStorage,
  });

  HDRSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .addressMode = vk::SamplerAddressMode::eRepeat, .name = "HDRSampler"});

  maxLuminanceBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = 1 * sizeof(float),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "luminanceBuffer",
  });

  luminanceHistBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = 256 * sizeof(float),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "luminanceBuffer",
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
    "culling_shader", 
    {TONEMAPPING_RENDERER_SHADERS_ROOT "gpu_culling.comp.spv"});

  etna::create_program(
    "tonmap_shader0", 
    {TONEMAPPING_RENDERER_SHADERS_ROOT "tonmap0.comp.spv"});

  etna::create_program(
    "tonmap_shader1", 
    {TONEMAPPING_RENDERER_SHADERS_ROOT "tonmap1.comp.spv"});

  etna::create_program(
    "tonmap_shader2", 
    {TONEMAPPING_RENDERER_SHADERS_ROOT "tonmap2.comp.spv"});


  etna::create_program(
    "static_mesh_material",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program(
    "static_mesh", 
    {TONEMAPPING_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});

  etna::create_program(
    "terrain_shader",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.vert.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.tese.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.frag.spv"});


  etna::create_program(
    "HDR_to_LDR_shader", 
    {TONEMAPPING_RENDERER_SHADERS_ROOT "HDR_to_LDR.vert.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "HDR_to_LDR.frag.spv"});
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

  tonmap0Pipeline = {};
  tonmap0Pipeline = pipelineManager.createComputePipeline("tonmap_shader0", {});

  tonmap1Pipeline = {};
  tonmap1Pipeline = pipelineManager.createComputePipeline("tonmap_shader1", {});

  tonmap2Pipeline = {};
  tonmap2Pipeline = pipelineManager.createComputePipeline("tonmap_shader2", {});


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
          .colorAttachmentFormats = {vk::Format::eB10G11R11UfloatPack32},
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
          .colorAttachmentFormats = {vk::Format::eB10G11R11UfloatPack32},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });


  HDRtoLDRPipeline = {};
  HDRtoLDRPipeline = pipelineManager.createGraphicsPipeline(
    "HDR_to_LDR_shader",
    etna::GraphicsPipeline::CreateInfo{
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

void WorldRenderer::cullScene(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
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
    vk::ShaderStageFlagBits::eTessellationEvaluation,
    0,
    sizeof(PushConstants),
    &pushConstMC);

  cmd_buf.draw(4, 64*64, 0, 0);
}

void WorldRenderer::postProcess(vk::CommandBuffer cmd_buf)
{
  {
    vk::BufferMemoryBarrier2 barriers[] = {{}, {}};

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
      .buffer = maxLuminanceBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
      .buffer = luminanceHistBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    {
      vk::DependencyInfo depInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 2,
        .pBufferMemoryBarriers = barriers,
      };

      cmd_buf.pipelineBarrier2(depInfo);
    }

    cmd_buf.fillBuffer(
      maxLuminanceBuffer.get(), 0, vk::WholeSize, std::bit_cast<std::uint32_t>(0.f));
    cmd_buf.fillBuffer(
      luminanceHistBuffer.get(), 0, vk::WholeSize, std::bit_cast<std::uint32_t>(0.f));

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = maxLuminanceBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = luminanceHistBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    {
      vk::DependencyInfo depInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 2,
        .pBufferMemoryBarriers = barriers,
      };

      cmd_buf.pipelineBarrier2(depInfo);
    }
  }


  {
    etna::set_state(
      cmd_buf,
      HDRImage.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageRead,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(cmd_buf);

    vk::BufferMemoryBarrier2 barriers[] = {{}};

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = maxLuminanceBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    {
      vk::DependencyInfo depInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = barriers,
      };

      cmd_buf.pipelineBarrier2(depInfo);
    }
  }


  {
    auto simpleComputeInfo = etna::get_shader_program("tonmap_shader0");

    auto set = etna::create_descriptor_set(
      simpleComputeInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {
        etna::Binding{0, HDRImage.genBinding(HDRSampler.get(), vk::ImageLayout::eGeneral)},
        etna::Binding{1, maxLuminanceBuffer.genBinding()},
      });

    vk::DescriptorSet vkSet = set.getVkSet();

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, tonmap0Pipeline.getVkPipeline());

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute, tonmap0Pipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

    cmd_buf.pushConstants(
      tonmap0Pipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(glm::uvec2), &resolution);

    etna::flush_barriers(cmd_buf);

    cmd_buf.dispatch((resolution.x + 31) / 32, (resolution.y + 31) / 32, 1);
  }


  {
    vk::BufferMemoryBarrier2 barriers[] = {{}, {}};

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = maxLuminanceBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = luminanceHistBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    {
      vk::DependencyInfo depInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 2,
        .pBufferMemoryBarriers = barriers,
      };

      cmd_buf.pipelineBarrier2(depInfo);
    }
  }


  {
    auto simpleComputeInfo = etna::get_shader_program("tonmap_shader1");

    auto set = etna::create_descriptor_set(
      simpleComputeInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {
        etna::Binding{0, HDRImage.genBinding(HDRSampler.get(), vk::ImageLayout::eGeneral)},
        etna::Binding{1, maxLuminanceBuffer.genBinding()},
        etna::Binding{2, luminanceHistBuffer.genBinding()},
      });

    vk::DescriptorSet vkSet = set.getVkSet();

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, tonmap1Pipeline.getVkPipeline());

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute, tonmap1Pipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

    cmd_buf.pushConstants(
      tonmap1Pipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(glm::uvec2), &resolution);

    etna::flush_barriers(cmd_buf);

    cmd_buf.dispatch((resolution.x + 31) / 32, (resolution.y + 31) / 32, 1);
  }


  {
    vk::BufferMemoryBarrier2 barriers[] = {{}};

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = luminanceHistBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    {
      vk::DependencyInfo depInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = barriers,
      };

      cmd_buf.pipelineBarrier2(depInfo);
    }
  }


  {
    auto simpleComputeInfo = etna::get_shader_program("tonmap_shader2");

    auto set = etna::create_descriptor_set(
      simpleComputeInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {
        etna::Binding{0, luminanceHistBuffer.genBinding()},
      });

    vk::DescriptorSet vkSet = set.getVkSet();

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, tonmap2Pipeline.getVkPipeline());

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute, tonmap2Pipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

    etna::flush_barriers(cmd_buf);

    cmd_buf.dispatch(1, 1, 1);
  }


  {
    vk::BufferMemoryBarrier2 barriers[] = {{}, {}};

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = maxLuminanceBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = luminanceHistBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    {
      vk::DependencyInfo depInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = 2,
        .pBufferMemoryBarriers = barriers,
      };

      cmd_buf.pipelineBarrier2(depInfo);
    }
  }
}

void WorldRenderer::copyHDRtoLDR(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  auto simpleGraphicsInfo = etna::get_shader_program("HDR_to_LDR_shader");

  auto set = etna::create_descriptor_set(
    simpleGraphicsInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{0, HDRImage.genBinding(HDRSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{1, maxLuminanceBuffer.genBinding()},
      etna::Binding{2, luminanceHistBuffer.genBinding()},
    });

  vk::DescriptorSet vkSet = set.getVkSet();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, HDRtoLDRPipeline.getVkPipeline());

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.draw(3, 1, 0, 0);
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    cullScene(cmd_buf, cullingPipeline.getVkPipelineLayout());

    etna::set_state(
      cmd_buf,
      HDRImage.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);
    
    etna::flush_barriers(cmd_buf);

    {
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = HDRImage.get(), .view = HDRImage.getView({})}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

      renderScene(cmd_buf, staticMeshPipeline.getVkPipelineLayout());

      renderTerrain(cmd_buf, terrainPipeline.getVkPipelineLayout());
    }

    postProcess(cmd_buf);

    etna::set_state(
      cmd_buf,
      HDRImage.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(cmd_buf);

    {
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = target_image, .view = target_image_view}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

      copyHDRtoLDR(cmd_buf, HDRtoLDRPipeline.getVkPipelineLayout());
    }
  }
}
