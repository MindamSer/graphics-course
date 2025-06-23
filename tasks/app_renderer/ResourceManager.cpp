#include "ResourceManager.hpp"
#include <cstdint>
#include <glm/fwd.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/random.hpp>

ResourceManager::ResourceManager()
  : oneShotCommands{etna::get_context().createOneShotCmdMgr()}
  , transferHelper{etna::BlockingTransferHelper::CreateInfo{4096}}
{
}

void ResourceManager::allocateResources(glm::uvec2 resolution)
{
  auto& ctx = etna::get_context();

  hdrResources = {
    .maxLuminanceBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = 2 * sizeof(float),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "luminanceBuffer",
    }),

    .luminanceHistBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = 256 * sizeof(float),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "luminanceBuffer",
    }),

    .HDRImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "HDRImage",
    .format = vk::Format::eB10G11R11UfloatPack32,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                  vk::ImageUsageFlagBits::eSampled |
                  vk::ImageUsageFlagBits::eStorage,
    }),
  };

  {
    const int kernelSize = 64;

    ssaoResources = {
      .kernelPositions = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
        .size = kernelSize * sizeof(glm::vec4),
        .bufferUsage = vk::BufferUsageFlagBits::eTransferDst |
                       vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
        .name = "kernelPositionsBuffer",
      }),

      .noiseImage = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{4, 4, 1},
        .name = "ssaoNoiseImage",
        .format = vk::Format::eR32G32B32A32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eSampled,
      }),

      .ssaoImage = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{resolution.x, resolution.y, 1},
        .name = "ssaoImage",
        .format = vk::Format::eR32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eColorAttachment |
                      vk::ImageUsageFlagBits::eSampled,
      }),

      .bluredSSAOImage = ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{resolution.x, resolution.y, 1},
        .name = "bluredSSAOImage",
        .format = vk::Format::eR32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eSampled |
                      vk::ImageUsageFlagBits::eStorage,
      }),
    };

    {
      std::vector<glm::vec4> kernelPositions;
      kernelPositions.resize(kernelSize);
      for (uint32_t i = 0; i < kernelSize; ++i)
      {
        glm::vec3 random = glm::sphericalRand(1.0f);
        random.z = abs(random.z);
        float scale = float(i) / float(kernelSize);
        scale = 0.1f + 0.9f * scale * scale;
        random *= scale;
        kernelPositions[i] = glm::vec4{random, 1.0f};
      }

      transferHelper.uploadBuffer<glm::vec4>(*oneShotCommands, ssaoResources.kernelPositions, 0, kernelPositions);
    }

    {
      glm::vec4* noizeImageData = new glm::vec4[4096 * 4096];

      for (int i = 0; i < 4; ++i)
      {
        for (int j = 0; j < 4; ++j)
        {
          glm::vec3 random = glm::sphericalRand(1.0f);
          noizeImageData[i * 4 + j] = glm::vec4{glm::normalize(glm::vec3{random.x, random.y, 0.f}), 1.0f};
        }
      }

      transferHelper.uploadImage(
        *oneShotCommands, ssaoResources.noiseImage, 0, 0,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(noizeImageData), 4 * 4 * sizeof(glm::vec4)));

      delete[] noizeImageData;
    }
  }


  gBuffer = {
    .albedo = ctx.createImage(etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "GbufferAlbedo",
      .format = vk::Format::eR8G8B8A8Unorm,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
    }),
    .normal = ctx.createImage(etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "GbufferNormal",
      .format = vk::Format::eR8G8B8A8Snorm,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
    }),
    .metRou = ctx.createImage(etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "GbufferMetallicRoughness",
      .format = vk::Format::eR8G8B8A8Unorm,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
    }),
    .depth = ctx.createImage(etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "GbufferDepth",
      .format = vk::Format::eD32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
                    vk::ImageUsageFlagBits::eSampled,
    }),
  };
}