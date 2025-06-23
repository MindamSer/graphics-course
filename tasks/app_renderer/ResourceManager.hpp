#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <glm/ext.hpp>


struct Gbuffer
{
  etna::Image albedo;
  etna::Image normal;
  etna::Image metRou;
  etna::Image depth;
};

struct SSAOresources
{
  etna::Buffer kernelPositions;
  etna::Image noiseImage;
  etna::Image ssaoImage;
  etna::Image bluredSSAOImage;
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
  SSAOresources &getSSAOresources() { return ssaoResources; }
  HDRresources &getHDRresources() { return hdrResources; }

private:
  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  etna::BlockingTransferHelper transferHelper;

  Gbuffer gBuffer;
  SSAOresources ssaoResources;
  HDRresources hdrResources;

};