#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <glm/ext.hpp>


struct Gbuffer
{
  etna::Image albedo;
  etna::Image normal;
  etna::Image metRou;
  etna::Image depth;
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