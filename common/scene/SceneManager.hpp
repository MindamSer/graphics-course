#pragma once

#include <cstdint>
#include <filesystem>

#include <glm/fwd.hpp>
#include <glm/glm.hpp>
#include <tiny_gltf.h>
#include <etna/Buffer.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/VertexInput.hpp>


struct Material
{
  glm::vec4 baseColorFactor;
  glm::vec4 metalRougFactor;
  int albedoTexIndex;
  int metRouTexIndex;
  int normalTexIndex;
  int _padding;
};


// A single render element (relem) corresponds to a single draw call
// of a certain pipeline with specific bindings (including material data)
struct RenderElement
{
  std::uint32_t vertexOffset;
  std::uint32_t indexOffset;
  std::uint32_t indexCount;
  int materialIndex;
};

struct RenderElementBoundingBox
{
  glm::vec4 max_pos;
  glm::vec4 min_pos;
};

// A mesh is a collection of relems. A scene may have the same mesh
// located in several different places, so a scene consists of **instances**,
// not meshes.
struct Mesh
{
  std::uint32_t firstRelem;
  std::uint32_t relemCount;
};

struct LightSource
{
  glm::vec4 pos;
  glm::vec4 dir;
  glm::vec4 color;
};


struct InstancingBuffers
{
  etna::Buffer instanceMatrices;
  etna::Buffer instanceMeshesIndicies;
  etna::Buffer meshes;
  etna::Buffer renderElements;
  etna::Buffer renderElementBoxes;
};

struct IndirectDrawBuffers
{
  etna::Buffer drawCommands;
  etna::Buffer drawMatricesIndicies;
};


class SceneManager
{
public:
  SceneManager();

  void selectScene(std::filesystem::path path);


  // Every instance is a mesh drawn with a certain transform
  // NOTE: maybe you can pass some additional data through unused matrix entries?
  std::span<const glm::mat4x4> getInstanceMatrices() { return instanceMatrices; }
  std::span<const std::uint32_t> getInstanceMeshes() { return instanceMeshIndicies; }

  // Every mesh is a collection of relems
  std::span<const Mesh> getMeshes() { return meshes; }
  std::span<const RenderElement> getRenderElements() { return renderElements; }
  std::span<const RenderElementBoundingBox> getRenderElementsBoxes() { return relemBoxes; }

  // Every material is set of 3 textures - albedo, roughness and normal map
  std::span<const Material> getMaterials() { return materials; }
  std::span<const etna::Image> getTextures() { return textures; }


  vk::Buffer getVertexBuffer() { return unifiedVbuf.get(); }
  vk::Buffer getIndexBuffer() { return unifiedIbuf.get(); }

  InstancingBuffers &getInstancingBuffers() { return instancingBuffers; }

  IndirectDrawBuffers &getIndirectDrawBuffers() { return indiretDrawBuffers; }

  etna::Buffer &getMaterialsBuffer() { return materialsBuffer; }

  etna::Image &getHieghtMapImage() { return hieghtMap; }
  etna::Sampler &getHieghtMapSampler() { return hieghtMapSampler; };

  std::vector<LightSource> &getLightSources() { return lights; }
  etna::Buffer &getLightSourcesBuffer() { return lightSourcesBuffer; }
  std::byte *getLightsBufferPtr() { return lightsBufferPtr; }

  etna::VertexByteStreamFormatDescription getVertexFormatDescription();


private:
  std::optional<tinygltf::Model> loadModel(std::filesystem::path path);

  void loadMaterials(const tinygltf::Model& model);

  struct ProcessedInstances
  {
    std::vector<glm::mat4x4> matrices;
    std::vector<std::uint32_t> meshes;
  };
  ProcessedInstances processInstances(const tinygltf::Model& model) const;

  struct Vertex
  {
    // First 3 floats are position, 4th float is a packed normal
    glm::vec4 positionAndNormal;
    // First 2 floats are tex coords, 3rd is a packed tangent, 4th is padding
    glm::vec4 texCoordAndTangentAndPadding;
  };
  static_assert(sizeof(Vertex) == sizeof(float) * 8);

  struct ProcessedMeshes
  {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Mesh> meshes;
    std::vector<RenderElement> relems;
    std::vector<RenderElementBoundingBox> relemBoxes;
  };
  ProcessedMeshes processMeshes(const tinygltf::Model& model) const;
  ProcessedMeshes processBakedMeshes(const tinygltf::Model& model) const;

  void uploadData(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices);
  void createCullingBuffers();
  void createIndirectDrawBuffers();
  void createHieghtMap();
  void createLightSources();


private:
  tinygltf::TinyGLTF loader;
  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  etna::BlockingTransferHelper transferHelper;


  std::vector<glm::mat4x4> instanceMatrices;
  std::vector<std::uint32_t> instanceMeshIndicies;
  std::vector<Mesh> meshes;
  std::vector<RenderElement> renderElements;
  std::vector<RenderElementBoundingBox> relemBoxes;

  std::vector<Material> materials;
  std::vector<etna::Image> textures;


  etna::Buffer unifiedVbuf;
  etna::Buffer unifiedIbuf;

  InstancingBuffers instancingBuffers;

  IndirectDrawBuffers indiretDrawBuffers;

  etna::Buffer materialsBuffer;

  etna::Image hieghtMap;
  etna::Sampler hieghtMapSampler;

  std::vector<LightSource> lights;
  etna::Buffer lightSourcesBuffer;
  std::byte *lightsBufferPtr;
};
