#include "ResourceManager.hpp"

ResourceManager::ResourceManager() {}

void ResourceManager::allocateResources(glm::uvec2 resolution)
{
  auto& ctx = etna::get_context();

  hdrResources.HDRImage = ctx.createImage(etna::Image::CreateInfo{
  .extent = vk::Extent3D{resolution.x, resolution.y, 1},
  .name = "HDRImage",
  .format = vk::Format::eB10G11R11UfloatPack32,
  .imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eStorage,
  });

  hdrResources.maxLuminanceBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = 2 * sizeof(float),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "luminanceBuffer",
  });

  hdrResources.luminanceHistBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = 256 * sizeof(float),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "luminanceBuffer",
  });

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