#pragma once

#include <filesystem>

#include <glm/glm.hpp>
#include <tiny_gltf.h>
#include <etna/Buffer.hpp>
#include <etna/Image.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/VertexInput.hpp>


// A single render element (relem) corresponds to a single draw call
// of a certain pipeline with specific bindings (including material data)
struct RenderElement
{
  std::uint32_t vertexOffset;
  std::uint32_t indexOffset;
  std::uint32_t indexCount;
  // Not implemented!
  // Material* material;
};

// A mesh is a collection of relems. A scene may have the same mesh
// located in several different places, so a scene consists of **instances**,
// not meshes.
struct Mesh
{
  std::uint32_t firstRelem;
  std::uint32_t relemCount;
};

struct RenderElementBoundingBox
{
  glm::vec3 max_pos;
  glm::vec3 min_pos;
};

class SceneManager
{
public:
  SceneManager();

  void selectScene(std::filesystem::path path);

  // Every instance is a mesh drawn with a certain transform
  // NOTE: maybe you can pass some additional data through unused matrix entries?
  std::span<const glm::mat4x4> getInstanceMatrices() { return instanceMatrices; }
  std::span<const std::uint32_t> getInstanceMeshes() { return instanceMeshes; }

  // Every mesh is a collection of relems
  std::span<const Mesh> getMeshes() { return meshes; }

  // Every relem is a single draw call
  std::span<const RenderElement> getRenderElements() { return renderElements; }

  vk::Buffer getVertexBuffer() { return unifiedVbuf.get(); }
  vk::Buffer getIndexBuffer() { return unifiedIbuf.get(); }

  etna::Buffer* getRelemBuffer() { return &unifiedRelemBuf; }
  etna::Buffer* getRelemBoxBuffer() { return &unifiedRelemBoxBuf; }
  etna::Buffer* getMeshBuffer() { return &unifiedMeshBuf; }
  etna::Buffer* getMatricesBuffer() { return &unifiedInstMatricesBuf; }
  etna::Buffer* getIMeshesBuffer() { return &unifiedInstMeshesBuf; }
  etna::Buffer* getDrawCmdBuffer() { return &unifiedDrawCmdBuf; }
  etna::Buffer* getDrawMatricesIndBuffer() { return &unifiedDrawMatricesIndBuf; }
  etna::Buffer* getMatricesOffsetsIndBuffer() { return &unifiedMatricesOffsetsIndBuf; }

  etna::Image* getHieghtMapImage() { return &hieghtMap; }

  etna::VertexByteStreamFormatDescription getVertexFormatDescription();

private:
  std::optional<tinygltf::Model> loadModel(std::filesystem::path path);

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
    std::vector<RenderElement> relems;
    std::vector<Mesh> meshes;
    std::vector<RenderElementBoundingBox> relemBoxes;
  };
  ProcessedMeshes processMeshes(const tinygltf::Model& model) const;
  ProcessedMeshes processBakedMeshes(const tinygltf::Model& model) const;
  void uploadData(std::span<const Vertex> vertices, std::span<const std::uint32_t>);

private:
  tinygltf::TinyGLTF loader;
  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  etna::BlockingTransferHelper transferHelper;

  std::vector<RenderElement> renderElements;
  std::vector<RenderElementBoundingBox> relemBoxes;
  std::vector<Mesh> meshes;
  std::vector<glm::mat4x4> instanceMatrices;
  std::vector<std::uint32_t> instanceMeshes;

  std::vector<VkDrawIndexedIndirectCommand> drawCmds;
  std::vector<std::uint32_t> matricesOffsetsInd;

  etna::Buffer unifiedVbuf;
  etna::Buffer unifiedIbuf;

  etna::Buffer unifiedRelemBuf;
  etna::Buffer unifiedRelemBoxBuf;
  etna::Buffer unifiedMeshBuf;
  etna::Buffer unifiedInstMatricesBuf;
  etna::Buffer unifiedInstMeshesBuf;

  etna::Buffer unifiedDrawCmdBuf;
  etna::Buffer unifiedDrawMatricesIndBuf;
  etna::Buffer unifiedMatricesOffsetsIndBuf;

  etna::Image hieghtMap;
  void genHieghtMap();
};
