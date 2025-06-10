#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <glm/ext.hpp>


struct Gbuffer
{
  etna::Image Albedo;
  etna::Image Normal;
  etna::Image Depth;
};

struct HDRresources
{
  etna::Buffer maxLuminanceBuffer;
  etna::Buffer luminanceHistBuffer;
  etna::Image HDRImage;
};


class ResourceManager
{
public:
  ResourceManager();

  void allocateResources(glm::uvec2 swapchain_resolution);

  Gbuffer &getGbuffer() { return gBuffer; }
  HDRresources &getHDRresources() { return hdrResources; }

private:
  Gbuffer gBuffer;
  HDRresources hdrResources;

};