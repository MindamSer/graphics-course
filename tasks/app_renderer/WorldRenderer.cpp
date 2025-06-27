#include "WorldRenderer.hpp"
#include "ResourceManager.hpp"
#include "etna/DescriptorSet.hpp"
#include "etna/Etna.hpp"
#include "scene/SceneManager.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>
#include <glm/common.hpp>
#include <glm/ext.hpp>
#include <glm/fwd.hpp>
#include <memory>

#include <imgui.h>


WorldRenderer::WorldRenderer()
  : sceneMgr{std::make_unique<SceneManager>()}, resourceMgr{std::make_unique<ResourceManager>()} {}


void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  auto& ctx = etna::get_context();

  resolution = swapchain_resolution;

  mainViewDepth = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "mainViewDepth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
  });

  quadSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .addressMode = vk::SamplerAddressMode::eRepeat,
    .name = "quadSampler"
  });

  resourceMgr->allocateResources(swapchain_resolution);
}


void WorldRenderer::loadShaders()
{
  etna::create_program(
    "culling_shader",
    {APP_RENDERER_SHADERS_ROOT "gpu_culling.comp.spv"});

  etna::create_program(
    "ssao_blur_shader",
    {APP_RENDERER_SHADERS_ROOT "ssao_blur.comp.spv"});

  etna::create_program(
    "tonmap_shader0",
    {APP_RENDERER_SHADERS_ROOT "tonmap0.comp.spv"});

  etna::create_program(
    "tonmap_shader1",
    {APP_RENDERER_SHADERS_ROOT "tonmap1.comp.spv"});

  etna::create_program(
    "tonmap_shader2",
    {APP_RENDERER_SHADERS_ROOT "tonmap2.comp.spv"});


  etna::create_program(
    "static_mesh",
    {
      APP_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
      APP_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"
    });

  etna::create_program(
    "terrain_shader",
    {
      APP_RENDERER_SHADERS_ROOT "terrain.vert.spv",
      APP_RENDERER_SHADERS_ROOT "terrain.tesc.spv",
      APP_RENDERER_SHADERS_ROOT "terrain.tese.spv",
      APP_RENDERER_SHADERS_ROOT "terrain.frag.spv"
    });

  etna::create_program(
    "ssao_calculation_shader",
    {
      APP_RENDERER_SHADERS_ROOT "ssao_calculation.vert.spv",
      APP_RENDERER_SHADERS_ROOT "ssao_calculation.frag.spv"
    });

  etna::create_program(
    "deferred_shader",
    {
      APP_RENDERER_SHADERS_ROOT "deferred.vert.spv",
      APP_RENDERER_SHADERS_ROOT "deferred.frag.spv"
    });


  etna::create_program(
    "HDR_to_LDR_shader",
    {
      APP_RENDERER_SHADERS_ROOT "HDR_to_LDR.vert.spv",
      APP_RENDERER_SHADERS_ROOT "HDR_to_LDR.frag.spv"
    });
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

  ssaoBlurPipeline = {};
  ssaoBlurPipeline = pipelineManager.createComputePipeline("ssao_blur_shader", {});

  tonmap0Pipeline = {};
  tonmap0Pipeline = pipelineManager.createComputePipeline("tonmap_shader0", {});

  tonmap1Pipeline = {};
  tonmap1Pipeline = pipelineManager.createComputePipeline("tonmap_shader1", {});

  tonmap2Pipeline = {};
  tonmap2Pipeline = pipelineManager.createComputePipeline("tonmap_shader2", {});


  etna::GraphicsPipeline::CreateInfo::Blending blendingInfo =
  {
    .attachments =
      {{.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,},
       {.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,},
       {.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,}},
    .logicOp = {},
  };

  etna::GraphicsPipeline::CreateInfo::FragmentShaderOutputDescription fsOutputInfo =
  {
    .colorAttachmentFormats = {
      vk::Format::eR8G8B8A8Unorm,
      vk::Format::eR8G8B8A8Snorm,
      vk::Format::eR8G8B8A8Unorm,
    },
    .depthAttachmentFormat = vk::Format::eD32Sfloat,
  };

  staticMeshPipeline = {};
  staticMeshPipeline = pipelineManager.createGraphicsPipeline(
    "static_mesh",
    etna::GraphicsPipeline::CreateInfo{
      .vertexShaderInput = sceneVertexInputDesc,
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .blendingConfig = blendingInfo,
      .fragmentShaderOutput = fsOutputInfo,
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
      .blendingConfig = blendingInfo,
      .fragmentShaderOutput = fsOutputInfo,
    });

  ssaoCalculationPipeline = {};
  ssaoCalculationPipeline = pipelineManager.createGraphicsPipeline(
    "ssao_calculation_shader",
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
          .colorAttachmentFormats = {vk::Format::eR32Sfloat},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  defferedShadingPipeline = {};
  defferedShadingPipeline = pipelineManager.createGraphicsPipeline(
    "deferred_shader",
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


void WorldRenderer::loadScene(std::filesystem::path path)
{
  sceneMgr->selectScene(path);

  renderConstants.instanceCount = static_cast<std::uint32_t>(sceneMgr->getInstanceMatrices().size());
  renderConstants.relemCount = static_cast<std::uint32_t>(sceneMgr->getRenderElements().size());
  renderConstants.lightsCount = static_cast<std::uint32_t>(sceneMgr->getLightSources().size());

  auto &instancingBuffers = sceneMgr->getInstancingBuffers();
  auto &indirectDrawBuffers = sceneMgr->getIndirectDrawBuffers();
  auto textures = sceneMgr->getTextures();

  auto &gBuffer = resourceMgr->getGbuffer();
  auto &ssaoResources = resourceMgr->getSSAOresources();

  // creating descroptor set for culling
  {
    std::vector<const etna::Buffer*> buffersToBind =
    {
      &instancingBuffers.instanceMatrices,
      &instancingBuffers.instanceMeshesIndicies,
      &instancingBuffers.meshes,
      &instancingBuffers.renderElements,
      &instancingBuffers.renderElementBoxes,

      &indirectDrawBuffers.drawCommands,
      &indirectDrawBuffers.drawMatricesIndicies,
    };


    std::vector<etna::Binding> bindings = {};
    bindings.reserve(buffersToBind.size());
    for (uint32_t i = 0; i < buffersToBind.size(); ++i)
    {
      bindings.push_back(
        etna::Binding{i, buffersToBind[i]->genBinding()}
      );
    }


    cullingDescSet = etna::create_persistent_descriptor_set(
      etna::get_shader_program("culling_shader").getDescriptorLayoutId(0),
      bindings
    );
  }

  // creating descroptor set for rendering models with bundless textures
  {
    std::vector<const etna::Buffer*> buffersToBind =
    {
      &instancingBuffers.instanceMatrices,
      &instancingBuffers.renderElements,
      &indirectDrawBuffers.drawMatricesIndicies,
      &sceneMgr->getMaterialsBuffer(),
    };


    std::vector<etna::Binding> bindings = {};
    bindings.reserve(buffersToBind.size());
    for (uint32_t i = 0; i < buffersToBind.size(); ++i)
    {
      bindings.push_back(
        etna::Binding{i, buffersToBind[i]->genBinding()}
      );
    }

    uint32_t texturesBindPoint = bindings.size();
    for (uint32_t i = 0; i < textures.size(); ++i)
    {
      bindings.push_back(etna::Binding{texturesBindPoint, textures[i].genBinding(quadSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal), i});
    }


    staticMeshDescSet = etna::create_persistent_descriptor_set(
      etna::get_shader_program("static_mesh").getDescriptorLayoutId(0),
      bindings,
      true
    );
  }

  // creating descroptor sets for ssao
  {
    {
      std::vector<etna::Binding> bindings =
      {
        etna::Binding{0, gBuffer.normal.genBinding(quadSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{1, gBuffer.depth.genBinding(quadSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{2, ssaoResources.noiseImage.genBinding(quadSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{3, ssaoResources.kernelPositions.genBinding()},
      };

      ssaoCalculationDescSet = etna::create_persistent_descriptor_set(
        etna::get_shader_program("ssao_calculation_shader").getDescriptorLayoutId(0),
        bindings
      );
    }

    {
      std::vector<etna::Binding> bindings =
      {
        etna::Binding{0, ssaoResources.ssaoImage.genBinding(quadSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        etna::Binding{1, ssaoResources.bluredSSAOImage.genBinding({}, vk::ImageLayout::eGeneral)},
      };

      ssaoBlurDescSet = etna::create_persistent_descriptor_set(
        etna::get_shader_program("ssao_blur_shader").getDescriptorLayoutId(0),
        bindings
      );
    }
  }

  // creating descroptor set for deferred lighting
  {
    std::vector<etna::Binding> bindings =
    {
      etna::Binding{0, gBuffer.albedo.genBinding(quadSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{1, gBuffer.normal.genBinding(quadSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{2, gBuffer.metRou.genBinding(quadSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{3, gBuffer.depth.genBinding(quadSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{4, ssaoResources.bluredSSAOImage.genBinding(quadSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{5, sceneMgr->getLightSourcesBuffer().genBinding()},
    };


    deferredDescSet = etna::create_persistent_descriptor_set(
      etna::get_shader_program("deferred_shader").getDescriptorLayoutId(0),
      bindings
    );
  }
}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  // calc camera matrix
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    renderConstants.proj = packet.mainCam.projTm(aspect);
    renderConstants.view = packet.mainCam.viewTm();
    renderConstants.projView = renderConstants.proj * renderConstants.view;
    renderConstants.cameraPos = packet.mainCam.position;
  }

  // copy light data
  {
    auto &lights = sceneMgr->getLightSources();
    auto lightsBufferPtr = sceneMgr->getLightsBufferPtr();

    memcpy(lightsBufferPtr, lights.data(), lights.size() * sizeof(LightSource));
  }
}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::drawGui()
{
  ImGui::Begin("Scene Lights");

  auto *drawCmd = ImGui::GetForegroundDrawList();

  ImGui::Checkbox("Render scene", &scene);
  ImGui::Checkbox("Render terrain", &terrain);
  ImGui::Checkbox("Enable SSAO", &enableSSAO);
  ImGui::Checkbox("Show bounding boxes", &drawBoundingBoxes);
  ImGui::Checkbox("Show lights", &drawLights);

  // lights
  if (drawLights)
  {
    auto& lights = sceneMgr->getLightSources();
    for (size_t i = 0; i < lights.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));

        glm::vec4 lightScreenPos = renderConstants.projView * lights[i].pos;
        if (lightScreenPos.z > 0.0f)
        {
          lightScreenPos /= lightScreenPos.w;
          drawCmd->AddCircleFilled(
            {(lightScreenPos.x * 0.5f + 0.5f) * resolution.x, (lightScreenPos.y * 0.5f + 0.5f) * resolution.y},
            5.0f,
            ImColor{lights[i].color.r, lights[i].color.g, lights[i].color.b});
        }

        std::string header = "Light Source " + std::to_string(i);
        if (ImGui::CollapsingHeader(header.c_str())) {
            ImGui::SliderFloat3("Position", &lights[i].pos.x, -5.0f, 5.0f);
            ImGui::ColorEdit3("Color", &lights[i].color.x);
        }

        ImGui::PopID();
    }
  }

  // bounding boxes
  if (drawBoundingBoxes)
  {
    std::vector<glm::vec4> boxVertices;
    boxVertices.reserve(8);

    std::vector<ImVec2> pointsPositions;
    pointsPositions.reserve(8);
    std::pair<uint32_t, uint32_t> edgesPairs[12] = {
      std::pair<uint32_t, uint32_t>{0, 1},
      std::pair<uint32_t, uint32_t>{1, 3},
      std::pair<uint32_t, uint32_t>{3, 2},
      std::pair<uint32_t, uint32_t>{2, 0},
      std::pair<uint32_t, uint32_t>{4, 5},
      std::pair<uint32_t, uint32_t>{5, 7},
      std::pair<uint32_t, uint32_t>{7, 6},
      std::pair<uint32_t, uint32_t>{6, 4},
      std::pair<uint32_t, uint32_t>{0, 4},
      std::pair<uint32_t, uint32_t>{1, 5},
      std::pair<uint32_t, uint32_t>{3, 7},
      std::pair<uint32_t, uint32_t>{2, 6},
    };
    ImColor bbColor{0, 63, 0};
    std::array<bool, 6> allBehindPlane;


    const auto &instanceMatrices = sceneMgr->getInstanceMatrices();
    const auto &instanceMeshIndicies = sceneMgr->getInstanceMeshes();
    const auto &meshes = sceneMgr->getMeshes();
    const auto &relemBoundingBoxes = sceneMgr->getRenderElementsBoxes();

    for (uint32_t i = 0; i < instanceMatrices.size(); ++i)
    {
      glm::mat4x4 matrix = instanceMatrices[i];
      Mesh mesh = meshes[instanceMeshIndicies[i]];

      for (uint32_t j = 0; j < mesh.relemCount; ++j)
      {
        RenderElementBoundingBox boundingBox = relemBoundingBoxes[mesh.firstRelem + j];

        boxVertices = {
          glm::vec4(boundingBox.min_pos.x, boundingBox.min_pos.y, boundingBox.min_pos.z, 1.0f),
          glm::vec4(boundingBox.max_pos.x, boundingBox.min_pos.y, boundingBox.min_pos.z, 1.0f),
          glm::vec4(boundingBox.min_pos.x, boundingBox.max_pos.y, boundingBox.min_pos.z, 1.0f),
          glm::vec4(boundingBox.max_pos.x, boundingBox.max_pos.y, boundingBox.min_pos.z, 1.0f),
          glm::vec4(boundingBox.min_pos.x, boundingBox.min_pos.y, boundingBox.max_pos.z, 1.0f),
          glm::vec4(boundingBox.max_pos.x, boundingBox.min_pos.y, boundingBox.max_pos.z, 1.0f),
          glm::vec4(boundingBox.min_pos.x, boundingBox.max_pos.y, boundingBox.max_pos.z, 1.0f),
          glm::vec4(boundingBox.max_pos.x, boundingBox.max_pos.y, boundingBox.max_pos.z, 1.0f),
        };

        allBehindPlane = {
        true,
        true,
        true,
        true,
        true,
        true,
        };
        for (auto &vertex : boxVertices)
        {
          vertex = renderConstants.projView * (matrix * vertex);
          vertex /= vertex.w;

          allBehindPlane[0] &= vertex.x >  1.0f;
          allBehindPlane[1] &= vertex.x < -1.0f;
          allBehindPlane[2] &= vertex.y >  1.0f;
          allBehindPlane[3] &= vertex.y < -1.0f;
          allBehindPlane[4] &= vertex.z >  1.0f;
          allBehindPlane[5] &= vertex.z <  0.0f;
        }

        if (!(allBehindPlane[0] || allBehindPlane[1] || allBehindPlane[2] || allBehindPlane[3] || allBehindPlane[4] || allBehindPlane[5]))
        {
          pointsPositions.clear();

          for (auto vertex : boxVertices)
            pointsPositions.push_back({
              (vertex.x * 0.5f + 0.5f) * resolution.x,
              (vertex.y * 0.5f + 0.5f) * resolution.y
            });

          for (auto point : pointsPositions)
            drawCmd->AddCircleFilled(
              point,
              3.0f,
              bbColor
            );

          for (auto edge : edgesPairs)
            drawCmd->AddLine(
              pointsPositions[edge.first],
              pointsPositions[edge.second],
              bbColor
            );
        }
      }
    }
  }

  ImGui::End();
}



void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  auto &gBuffer = resourceMgr->getGbuffer();
  auto &ssaoResources = resourceMgr->getSSAOresources();
  auto &hdrResources = resourceMgr->getHDRresources();

  // draw final scene to screen
  {
    cullScene(cmd_buf);

    {
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {
          {.image = gBuffer.albedo.get(), .view = gBuffer.albedo.getView({})},
          {.image = gBuffer.normal.get(), .view = gBuffer.normal.getView({})},
          {.image = gBuffer.metRou.get(), .view = gBuffer.metRou.getView({})},
        },
        {.image = gBuffer.depth.get(), .view = gBuffer.depth.getView({})});

      if (scene)
        renderScene(cmd_buf);

      if (terrain)
        renderTerrain(cmd_buf);
    }

    computeSSAO(cmd_buf);

    {
      etna::set_state(
        cmd_buf,
        gBuffer.albedo.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::set_state(
        cmd_buf,
        gBuffer.normal.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::set_state(
        cmd_buf,
        gBuffer.metRou.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::set_state(
        cmd_buf,
        gBuffer.depth.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eDepth);

      etna::set_state(
        cmd_buf,
        ssaoResources.bluredSSAOImage.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(cmd_buf);
    }

    {
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = hdrResources.HDRImage.get(), .view = hdrResources.HDRImage.getView({})}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

      deferredShading(cmd_buf);
    }

    postProcess(cmd_buf);

    {
      etna::set_state(
        cmd_buf,
        hdrResources.HDRImage.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(cmd_buf);
    }

    {
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = target_image, .view = target_image_view}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

      copyHDRtoLDR(cmd_buf);
    }
  }
}



void WorldRenderer::cullScene(vk::CommandBuffer cmd_buf)
{
  ETNA_PROFILE_GPU(cmd_buf, cullScene);

  auto &instancingBuffers = sceneMgr->getInstancingBuffers();
  auto &indirectDrawBufers = sceneMgr->getIndirectDrawBuffers();

  {
    vk::BufferMemoryBarrier2 barriers[] = {{}, {}, {}};

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = instancingBuffers.instanceMatrices.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = indirectDrawBufers.drawMatricesIndicies.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[2] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
      .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = indirectDrawBufers.drawCommands.get(),
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

  cullingDescSet.processBarriers(cmd_buf);
  vk::DescriptorSet vkSet = cullingDescSet.getVkSet();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cullingPipeline.getVkPipeline());

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, cullingPipeline.getVkPipelineLayout(),
    0, 1, &vkSet, 0, nullptr);

  cullingPC =
  {
    .projView = renderConstants.projView,
    .instanceCount = renderConstants.instanceCount,
    .relemCount = renderConstants.relemCount,
  };

  cmd_buf.pushConstants(
    cullingPipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eCompute,
    0, sizeof(CullingPushConstants), &cullingPC);

  etna::flush_barriers(cmd_buf);

  cmd_buf.dispatch((cullingPC.instanceCount + 255) / 256, 1, 1);

  {
    vk::BufferMemoryBarrier2 barriers[] = {{}, {}, {}};

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = instancingBuffers.instanceMatrices.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = indirectDrawBufers.drawMatricesIndicies.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[2] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
      .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
      .buffer = indirectDrawBufers.drawCommands.get(),
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


void WorldRenderer::renderScene(vk::CommandBuffer cmd_buf)
{
  ETNA_PROFILE_GPU(cmd_buf, renderScene);

  if (!sceneMgr->getVertexBuffer())
    return;

  auto &indirectDrawBufers = sceneMgr->getIndirectDrawBuffers();

  staticMeshDescSet.processBarriers(cmd_buf);
  vk::DescriptorSet vkSet = staticMeshDescSet.getVkSet();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());

  cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics,
    staticMeshPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

  scenePC = {
    .projView = renderConstants.projView,
  };

  cmd_buf.pushConstants(
    staticMeshPipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eVertex,
    0, sizeof(ScenePushConstants), &scenePC);

  cmd_buf.drawIndexedIndirect(
    indirectDrawBufers.drawCommands.get(), 0, static_cast<std::uint32_t>(sceneMgr->getRenderElements().size()), sizeof(VkDrawIndexedIndirectCommand));
}


void WorldRenderer::renderTerrain(vk::CommandBuffer cmd_buf)
{
  ETNA_PROFILE_GPU(cmd_buf, renderTerrain);

  auto simpleGraphicsInfo = etna::get_shader_program("terrain_shader");

  auto set = etna::create_descriptor_set(
    simpleGraphicsInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{ 0, sceneMgr->getHieghtMapImage().genBinding(sceneMgr->getHieghtMapSampler().get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
    });

  vk::DescriptorSet vkSet = set.getVkSet();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainPipeline.getVkPipeline());

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, terrainPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

  terrainPC =
  {
    .projView = renderConstants.projView,
    .cameraPos = renderConstants.cameraPos,
  };

  cmd_buf.pushConstants(
    terrainPipeline.getVkPipelineLayout(),
    vk::ShaderStageFlagBits::eTessellationEvaluation,
    0, sizeof(TerrainPushConstants), &terrainPC);

  cmd_buf.draw(4, 64*64, 0, 0);
}

void WorldRenderer::computeSSAO(vk::CommandBuffer cmd_buf)
{
  ETNA_PROFILE_GPU(cmd_buf, SSAO);

  auto &gBuffer = resourceMgr->getGbuffer();
  auto &ssaoResources = resourceMgr->getSSAOresources();

  {
    etna::set_state(
      cmd_buf,
      ssaoResources.ssaoImage.get(),
      vk::PipelineStageFlagBits2::eClear,
      vk::AccessFlagBits2::eTransferWrite,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);


    etna::set_state(
      cmd_buf,
      ssaoResources.bluredSSAOImage.get(),
      vk::PipelineStageFlagBits2::eClear,
      vk::AccessFlagBits2::eTransferWrite,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(cmd_buf);
  }

  cmd_buf.clearColorImage(
    ssaoResources.ssaoImage.get(), vk::ImageLayout::eGeneral,
    vk::ClearColorValue{1.0f, 0.0f, 0.0f, 0.0f},
    {vk::ImageSubresourceRange{
      .aspectMask = vk::ImageAspectFlagBits::eColor,
      .levelCount = 1,
      .layerCount = 1,
    }}
  );

  cmd_buf.clearColorImage(
    ssaoResources.bluredSSAOImage.get(), vk::ImageLayout::eGeneral,
    vk::ClearColorValue{1.0f, 0.0f, 0.0f, 0.0f},
    {vk::ImageSubresourceRange{
      .aspectMask = vk::ImageAspectFlagBits::eColor,
      .levelCount = 1,
      .layerCount = 1,
    }}
  );

  if (enableSSAO)
  {
    {
      etna::set_state(
        cmd_buf,
        gBuffer.normal.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::set_state(
        cmd_buf,
        gBuffer.depth.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eDepth);

      etna::set_state(
        cmd_buf,
        ssaoResources.noiseImage.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(cmd_buf);
    }

    ssaoPC = {
      .proj = renderConstants.proj,
      .view = renderConstants.view,
      .res = resolution,
    };

    {
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {
          {.image = ssaoResources.ssaoImage.get(), .view = ssaoResources.ssaoImage.getView({})},
        },
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

      ETNA_PROFILE_GPU(cmd_buf, calculateSSAO);

      ssaoCalculationDescSet.processBarriers(cmd_buf);
      vk::DescriptorSet vkSet = ssaoCalculationDescSet.getVkSet();

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, ssaoCalculationPipeline.getVkPipeline());

      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, ssaoCalculationPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

      cmd_buf.pushConstants(
        ssaoCalculationPipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eFragment, 0, sizeof(ssaoPC), &ssaoPC);

      etna::flush_barriers(cmd_buf);

      cmd_buf.draw(3, 1, 0, 0);
    }

    {
      etna::set_state(
        cmd_buf,
        ssaoResources.ssaoImage.get(),
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::set_state(
        cmd_buf,
        ssaoResources.bluredSSAOImage.get(),
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderWrite,
        vk::ImageLayout::eGeneral,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(cmd_buf);
    }

    {
      ETNA_PROFILE_GPU(cmd_buf, blurSSAO);

      ssaoBlurDescSet.processBarriers(cmd_buf);
      vk::DescriptorSet vkSet = ssaoBlurDescSet.getVkSet();

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, ssaoBlurPipeline.getVkPipeline());

      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute, ssaoBlurPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

      cmd_buf.pushConstants(
        ssaoBlurPipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(ssaoPC), &ssaoPC);

      etna::flush_barriers(cmd_buf);

      cmd_buf.dispatch((resolution.x + 31) / 32, (resolution.y + 31) / 32, 1);
    }
  }
}

void WorldRenderer::deferredShading(vk::CommandBuffer cmd_buf)
{
  ETNA_PROFILE_GPU(cmd_buf, deferredShading);

  deferredDescSet.processBarriers(cmd_buf);
  vk::DescriptorSet vkSet = deferredDescSet.getVkSet();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, defferedShadingPipeline.getVkPipeline());

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, defferedShadingPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

  deferredPC = {
    .proj = renderConstants.proj,
    .view = renderConstants.view,
    .lightsCount = renderConstants.lightsCount,
  };

  cmd_buf.pushConstants(
    defferedShadingPipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eFragment,
    0, sizeof(DeferredPushConstants), &deferredPC);

  cmd_buf.draw(3, 1, 0, 0);
}


void WorldRenderer::postProcess(
  vk::CommandBuffer cmd_buf)
{
  ETNA_PROFILE_GPU(cmd_buf, postProcess);

  auto &hdrResources = resourceMgr->getHDRresources();

  {
    ETNA_PROFILE_GPU(cmd_buf, fillingBuffers);

    vk::BufferMemoryBarrier2 barriers[] = {{}, {}};

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
      .buffer = hdrResources.maxLuminanceBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
      .buffer = hdrResources.luminanceHistBuffer.get(),
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
      hdrResources.maxLuminanceBuffer.get(), 0, vk::WholeSize, std::bit_cast<std::uint32_t>(0.f));
    cmd_buf.fillBuffer(
      hdrResources.luminanceHistBuffer.get(), 0, vk::WholeSize, std::bit_cast<std::uint32_t>(0.f));

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
      .buffer = hdrResources.maxLuminanceBuffer.get(),
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

    cmd_buf.fillBuffer(
      hdrResources.maxLuminanceBuffer.get(), 0, sizeof(float), 0x7FFFFFFF);

    barriers[0] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = hdrResources.maxLuminanceBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
      .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = hdrResources.luminanceHistBuffer.get(),
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
      hdrResources.HDRImage.get(),
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
      .buffer = hdrResources.maxLuminanceBuffer.get(),
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
    ETNA_PROFILE_GPU(cmd_buf, minMaxCalc);

    auto simpleComputeInfo = etna::get_shader_program("tonmap_shader0");

    auto set = etna::create_descriptor_set(
      simpleComputeInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {
        etna::Binding{0, hdrResources.HDRImage.genBinding(quadSampler.get(), vk::ImageLayout::eGeneral)},
        etna::Binding{1, hdrResources.maxLuminanceBuffer.genBinding()},
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
      .buffer = hdrResources.maxLuminanceBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .buffer = hdrResources.luminanceHistBuffer.get(),
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
    ETNA_PROFILE_GPU(cmd_buf, histCalc);

    auto simpleComputeInfo = etna::get_shader_program("tonmap_shader1");

    auto set = etna::create_descriptor_set(
      simpleComputeInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {
        etna::Binding{0, hdrResources.HDRImage.genBinding(quadSampler.get(), vk::ImageLayout::eGeneral)},
        etna::Binding{1, hdrResources.maxLuminanceBuffer.genBinding()},
        etna::Binding{2, hdrResources.luminanceHistBuffer.genBinding()},
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
      .buffer = hdrResources.luminanceHistBuffer.get(),
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
    ETNA_PROFILE_GPU(cmd_buf, probFuncCalc);

    auto simpleComputeInfo = etna::get_shader_program("tonmap_shader2");

    auto set = etna::create_descriptor_set(
      simpleComputeInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {
        etna::Binding{0, hdrResources.maxLuminanceBuffer.genBinding()},
        etna::Binding{1, hdrResources.luminanceHistBuffer.genBinding()},
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
      .buffer = hdrResources.maxLuminanceBuffer.get(),
      .offset = 0,
      .size = vk::WholeSize,
    };

    barriers[1] = vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
      .buffer = hdrResources.luminanceHistBuffer.get(),
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


void WorldRenderer::copyHDRtoLDR(vk::CommandBuffer cmd_buf)
{
  ETNA_PROFILE_GPU(cmd_buf, copyHDRtoLDR);

  auto &hdrResources = resourceMgr->getHDRresources();

  auto simpleGraphicsInfo = etna::get_shader_program("HDR_to_LDR_shader");

  auto set = etna::create_descriptor_set(
    simpleGraphicsInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{0, hdrResources.HDRImage.genBinding(quadSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
      etna::Binding{1, hdrResources.maxLuminanceBuffer.genBinding()},
      etna::Binding{2, hdrResources.luminanceHistBuffer.genBinding()},
    });

  vk::DescriptorSet vkSet = set.getVkSet();

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, HDRtoLDRPipeline.getVkPipeline());

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, HDRtoLDRPipeline.getVkPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

  cmd_buf.draw(3, 1, 0, 0);
}
