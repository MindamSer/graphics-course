#include "SceneManager.hpp"

#include <cstdint>
#include <memory>
#include <stack>

#include <spdlog/spdlog.h>
#include <fmt/std.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <etna/Image.hpp>

#include "Particles.hpp"
#include "perlin_noise.hpp"
#include "stb_image.h"


static std::uint32_t encode_normal(glm::vec3 normal)
{
  const std::int32_t x = static_cast<std::int32_t>(normal.x * 32767.0f);
  const std::int32_t y = static_cast<std::int32_t>(normal.y * 32767.0f);

  const std::uint32_t sign = normal.z >= 0 ? 0 : 1;
  const std::uint32_t sx = static_cast<std::uint32_t>(x & 0xfffe) | sign;
  const std::uint32_t sy = static_cast<std::uint32_t>(y & 0xffff) << 16;

  return sx | sy;
}


SceneManager::SceneManager()
  : oneShotCommands{etna::get_context().createOneShotCmdMgr()}
  , transferHelper{etna::BlockingTransferHelper::CreateInfo{.stagingSize = 4096 * 4096 * 4}}
{
}

std::optional<tinygltf::Model> SceneManager::loadModel(std::filesystem::path path)
{
  tinygltf::Model model;

  std::string error;
  std::string warning;
  bool success = false;

  auto ext = path.extension();
  if (ext == ".gltf")
    success = loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
  else if (ext == ".glb")
    success = loader.LoadBinaryFromFile(&model, &error, &warning, path.string());
  else
  {
    spdlog::error("glTF: Unknown glTF file extension: '{}'. Expected .gltf or .glb.", ext);
    return std::nullopt;
  }

  if (!success)
  {
    spdlog::error("glTF: Failed to load model!");
    if (!error.empty())
      spdlog::error("glTF: {}", error);
    return std::nullopt;
  }

  if (!warning.empty())
    spdlog::warn("glTF: {}", warning);

  if (
    !model.extensions.empty() || !model.extensionsRequired.empty() || !model.extensionsUsed.empty())
    spdlog::warn("glTF: No glTF extensions are currently implemented!");

  return model;
}

SceneManager::ProcessedInstances SceneManager::processInstances(const tinygltf::Model& model) const
{
  std::vector nodeTransforms(model.nodes.size(), glm::identity<glm::mat4x4>());

  for (std::size_t nodeIdx = 0; nodeIdx < model.nodes.size(); ++nodeIdx)
  {
    const auto& node = model.nodes[nodeIdx];
    auto& transform = nodeTransforms[nodeIdx];

    if (!node.matrix.empty())
    {
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
          transform[i][j] = static_cast<float>(node.matrix[4 * i + j]);
    }
    else
    {
      if (!node.scale.empty())
        transform = scale(
          transform,
          glm::vec3(
            static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2])));

      if (!node.rotation.empty())
        transform *= mat4_cast(glm::quat(
          static_cast<float>(node.rotation[3]),
          static_cast<float>(node.rotation[0]),
          static_cast<float>(node.rotation[1]),
          static_cast<float>(node.rotation[2])));

      if (!node.translation.empty())
        transform = translate(
          transform,
          glm::vec3(
            static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2])));
    }
  }

  std::stack<std::size_t> vertices;
  for (auto vert : model.scenes[model.defaultScene].nodes)
    vertices.push(vert);

  while (!vertices.empty())
  {
    auto vert = vertices.top();
    vertices.pop();

    for (auto child : model.nodes[vert].children)
    {
      nodeTransforms[child] = nodeTransforms[vert] * nodeTransforms[child];
      vertices.push(child);
    }
  }

  ProcessedInstances result;

  // Don't overallocate matrices, they are pretty chonky.
  {
    std::size_t totalNodesWithMeshes = 0;
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
      if (model.nodes[i].mesh >= 0)
        ++totalNodesWithMeshes;
    result.matrices.reserve(totalNodesWithMeshes);
    result.meshes.reserve(totalNodesWithMeshes);
  }

  for (std::size_t i = 0; i < model.nodes.size(); ++i)
    if (model.nodes[i].mesh >= 0)
    {
      result.matrices.push_back(nodeTransforms[i]);
      result.meshes.push_back(model.nodes[i].mesh);
    }

  return result;
}

SceneManager::ProcessedMeshes SceneManager::processMeshes(const tinygltf::Model& model) const
{
  // NOTE: glTF assets can have pretty wonky data layouts which are not appropriate
  // for real-time rendering, so we have to press the data first. In serious engines
  // this is mitigated by storing assets on the disc in an engine-specific format that
  // is appropriate for GPU upload right after reading from disc.

  ProcessedMeshes result;

  // Pre-allocate enough memory so as not to hit the
  // allocator on the memcpy hotpath
  {
    std::size_t vertexBytes = 0;
    std::size_t indexBytes = 0;
    for (const auto& bufView : model.bufferViews)
    {
      switch (bufView.target)
      {
      case TINYGLTF_TARGET_ARRAY_BUFFER:
        vertexBytes += bufView.byteLength;
        break;
      case TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER:
        indexBytes += bufView.byteLength;
        break;
      default:
        break;
      }
    }
    result.vertices.reserve(vertexBytes / sizeof(Vertex));
    result.indices.reserve(indexBytes / sizeof(std::uint32_t));
  }

  {
    std::size_t totalPrimitives = 0;
    for (const auto& mesh : model.meshes)
      totalPrimitives += mesh.primitives.size();
    result.relems.reserve(totalPrimitives);
  }

  result.meshes.reserve(model.meshes.size());

  for (const auto& mesh : model.meshes)
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(mesh.primitives.size()),
    });

    for (const auto& prim : mesh.primitives)
    {
      if (prim.mode != TINYGLTF_MODE_TRIANGLES)
      {
        spdlog::warn(
          "Encountered a non-triangles primitive, these are not supported for now, skipping it!");
        --result.meshes.back().relemCount;
        continue;
      }

      const auto normalIt = prim.attributes.find("NORMAL");
      const auto tangentIt = prim.attributes.find("TANGENT");
      const auto texcoordIt = prim.attributes.find("TEXCOORD_0");

      const bool hasNormals = normalIt != prim.attributes.end();
      const bool hasTangents = tangentIt != prim.attributes.end();
      const bool hasTexcoord = texcoordIt != prim.attributes.end();
      std::array accessorIndices{
        prim.indices,
        prim.attributes.at("POSITION"),
        hasNormals ? normalIt->second : -1,
        hasTangents ? tangentIt->second : -1,
        hasTexcoord ? texcoordIt->second : -1,
      };

      std::array accessors{
        &model.accessors[prim.indices],
        &model.accessors[accessorIndices[1]],
        hasNormals ? &model.accessors[accessorIndices[2]] : nullptr,
        hasTangents ? &model.accessors[accessorIndices[3]] : nullptr,
        hasTexcoord ? &model.accessors[accessorIndices[4]] : nullptr,
      };

      std::array bufViews{
        &model.bufferViews[accessors[0]->bufferView],
        &model.bufferViews[accessors[1]->bufferView],
        hasNormals ? &model.bufferViews[accessors[2]->bufferView] : nullptr,
        hasTangents ? &model.bufferViews[accessors[3]->bufferView] : nullptr,
        hasTexcoord ? &model.bufferViews[accessors[4]->bufferView] : nullptr,
      };

      result.relems.push_back(RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = static_cast<std::uint32_t>(accessors[0]->count),
        .materialIndex = prim.material
      });

      result.relemBoxes.push_back(RenderElementBoundingBox{
        {
          accessors[1]->maxValues[0],
          accessors[1]->maxValues[1],
          accessors[1]->maxValues[2],
          0.0f
        },
        {
          accessors[1]->minValues[0],
          accessors[1]->minValues[1],
          accessors[1]->minValues[2],
          0.0f
        }
      });

      const std::size_t vertexCount = accessors[1]->count;

      std::array ptrs{
        reinterpret_cast<const std::byte*>(model.buffers[bufViews[0]->buffer].data.data()) +
          bufViews[0]->byteOffset + accessors[0]->byteOffset,
        reinterpret_cast<const std::byte*>(model.buffers[bufViews[1]->buffer].data.data()) +
          bufViews[1]->byteOffset + accessors[1]->byteOffset,
        hasNormals
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[2]->buffer].data.data()) +
            bufViews[2]->byteOffset + accessors[2]->byteOffset
          : nullptr,
        hasTangents
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[3]->buffer].data.data()) +
            bufViews[3]->byteOffset + accessors[3]->byteOffset
          : nullptr,
        hasTexcoord
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[4]->buffer].data.data()) +
            bufViews[4]->byteOffset + accessors[4]->byteOffset
          : nullptr,
      };

      std::array strides{
        bufViews[0]->byteStride != 0
          ? bufViews[0]->byteStride
          : tinygltf::GetComponentSizeInBytes(accessors[0]->componentType) *
            tinygltf::GetNumComponentsInType(accessors[0]->type),
        bufViews[1]->byteStride != 0
          ? bufViews[1]->byteStride
          : tinygltf::GetComponentSizeInBytes(accessors[1]->componentType) *
            tinygltf::GetNumComponentsInType(accessors[1]->type),
        hasNormals ? (bufViews[2]->byteStride != 0
                        ? bufViews[2]->byteStride
                        : tinygltf::GetComponentSizeInBytes(accessors[2]->componentType) *
                          tinygltf::GetNumComponentsInType(accessors[2]->type))
                   : 0,
        hasTangents ? (bufViews[3]->byteStride != 0
                         ? bufViews[3]->byteStride
                         : tinygltf::GetComponentSizeInBytes(accessors[3]->componentType) *
                           tinygltf::GetNumComponentsInType(accessors[3]->type))
                    : 0,
        hasTexcoord ? (bufViews[4]->byteStride != 0
                         ? bufViews[4]->byteStride
                         : tinygltf::GetComponentSizeInBytes(accessors[4]->componentType) *
                           tinygltf::GetNumComponentsInType(accessors[4]->type))
                    : 0,
      };

      for (std::size_t i = 0; i < vertexCount; ++i)
      {
        auto& vtx = result.vertices.emplace_back();
        glm::vec3 pos;
        // Fall back to 0 in case we don't have something.
        // NOTE: if tangents are not available, one could use http://mikktspace.com/
        // NOTE: if normals are not available, reconstructing them is possible but will look ugly
        glm::vec3 normal{0};
        glm::vec3 tangent{0};
        glm::vec2 texcoord{0};
        std::memcpy(&pos, ptrs[1], sizeof(pos));

        // NOTE: it's faster to do a template here with specializations for all combinations than to
        // do ifs at runtime. Also, SIMD should be used. Try implementing this!
        if (hasNormals)
          std::memcpy(&normal, ptrs[2], sizeof(normal));
        if (hasTangents)
          std::memcpy(&tangent, ptrs[3], sizeof(tangent));
        if (hasTexcoord)
          std::memcpy(&texcoord, ptrs[4], sizeof(texcoord));


        vtx.positionAndNormal = glm::vec4(pos, std::bit_cast<float>(encode_normal(normal)));
        vtx.texCoordAndTangentAndPadding =
          glm::vec4(texcoord, std::bit_cast<float>(encode_normal(tangent)), 0);

        ptrs[1] += strides[1];
        if (hasNormals)
          ptrs[2] += strides[2];
        if (hasTangents)
          ptrs[3] += strides[3];
        if (hasTexcoord)
          ptrs[4] += strides[4];
      }

      // Indices are guaranteed to have no stride
      ETNA_VERIFY(bufViews[0]->byteStride == 0);
      const std::size_t indexCount = accessors[0]->count;
      if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
      {
        for (std::size_t i = 0; i < indexCount; ++i)
        {
          std::uint16_t index;
          std::memcpy(&index, ptrs[0], sizeof(index));
          result.indices.push_back(index);
          ptrs[0] += 2;
        }
      }
      else if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
      {
        const std::size_t lastTotalIndices = result.indices.size();
        result.indices.resize(lastTotalIndices + indexCount);
        std::memcpy(
          result.indices.data() + lastTotalIndices,
          ptrs[0],
          sizeof(result.indices[0]) * indexCount);
      }
    }
  }

  return result;
}

SceneManager::ProcessedMeshes SceneManager::processBakedMeshes(const tinygltf::Model& model) const
{

  ProcessedMeshes result;

  result.indices.resize(model.bufferViews[0].byteLength / sizeof(uint32_t));
  result.vertices.resize(model.bufferViews[1].byteLength / sizeof(Vertex));

  std::memcpy(
    result.indices.data(),
    model.buffers[0].data.data() + model.bufferViews[0].byteOffset,
    model.bufferViews[0].byteLength);
  std::memcpy(
    result.vertices.data(),
    model.buffers[0].data.data() + model.bufferViews[1].byteOffset,
    model.bufferViews[1].byteLength);

  {
    std::size_t totalPrimitives = 0;
    for (const auto& mesh : model.meshes)
      totalPrimitives += mesh.primitives.size();
    result.relems.reserve(totalPrimitives);
  }

  result.meshes.reserve(model.meshes.size());

  for (const auto& mesh : model.meshes)
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(mesh.primitives.size()),
    });

    for (const auto& prim : mesh.primitives)
    {
      auto& indiciesAccessor = model.accessors[prim.indices];
      auto& positionAccessor = model.accessors[prim.attributes.at("POSITION")];

      result.relems.push_back(RenderElement{
        static_cast<std::uint32_t>(positionAccessor.byteOffset / sizeof(Vertex)),
        static_cast<std::uint32_t>(indiciesAccessor.byteOffset / sizeof(std::uint32_t)),
        static_cast<std::uint32_t>(indiciesAccessor.count),
        prim.material
      });

      result.relemBoxes.push_back(RenderElementBoundingBox{
        {
          positionAccessor.maxValues[0],
          positionAccessor.maxValues[1],
          positionAccessor.maxValues[2],
          0.0f
        },
        {
          positionAccessor.minValues[0],
          positionAccessor.minValues[1],
          positionAccessor.minValues[2],
          0.0f
        }
      });
    }
  }

  return result;
}


void SceneManager::selectScene(std::filesystem::path path)
{
  auto maybeModel = loadModel(path);
  if (!maybeModel.has_value())
    return;

  auto model = std::move(*maybeModel);

  // By aggregating all SceneManager fields mutations here,
  // we guarantee that we don't forget to clear something
  // when re-loading a scene.

  // NOTE: you might want to store these on the GPU for GPU-driven rendering.
  auto [instMats, instMeshes] = processInstances(model);
  instanceMatrices = std::move(instMats);
  instanceMeshIndicies = std::move(instMeshes);

  loadMaterials(model);

  ProcessedMeshes resultMeshes;

  if (path.stem().string().ends_with("_baked"))
    resultMeshes = processBakedMeshes(model);
  else
    resultMeshes = processMeshes(model);

  renderElements = std::move(resultMeshes.relems);
  relemBoxes = std::move(resultMeshes.relemBoxes);
  meshes = std::move(resultMeshes.meshes);

  uploadData(resultMeshes.vertices, resultMeshes.indices);

  createCullingBuffers();

  createIndirectDrawBuffers();

  createHieghtMap();

  createLightSources();

  createParticleElements();
}

void SceneManager::loadMaterials(const tinygltf::Model& model)
{
  auto& ctx = etna::get_context();

  std::vector<vk::Format> imageFormats(model.images.size(), vk::Format::eR8G8B8A8Srgb);

  materials.reserve(model.materials.size());
  for (const auto& gltfMaterial : model.materials)
  {
    materials.push_back(Material{
      {
        gltfMaterial.pbrMetallicRoughness.baseColorFactor[0],
        gltfMaterial.pbrMetallicRoughness.baseColorFactor[1],
        gltfMaterial.pbrMetallicRoughness.baseColorFactor[2],
        gltfMaterial.pbrMetallicRoughness.baseColorFactor[3],
      },
      {
        gltfMaterial.pbrMetallicRoughness.metallicFactor,
        gltfMaterial.pbrMetallicRoughness.roughnessFactor,
        0.0f,
        0.0f
      },
      gltfMaterial.pbrMetallicRoughness.baseColorTexture.index,
      gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index,
      gltfMaterial.normalTexture.index,
      0
    });

    if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
    {
      imageFormats[gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index] = vk::Format::eR8G8B8A8Unorm;
    }
  }

  textures.reserve(model.images.size());
  for (uint32_t i = 0; i < model.images.size(); ++i)
  {
    const auto& gltfImage = model.images[i];

    auto &newImage = textures.emplace_back(
      ctx.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{static_cast<uint32_t>(gltfImage.width), static_cast<uint32_t>(gltfImage.height), 1},
        .name = gltfImage.name,
        .format = imageFormats[i],
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
      })
    );

    transferHelper.uploadImage(*oneShotCommands, newImage, 0, 0,
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(gltfImage.image.data()), gltfImage.width * gltfImage.height * 4));
  }

  materialsBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = materials.size() * sizeof(Material),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "materialsBuffer",
  });

  transferHelper.uploadBuffer<Material>(*oneShotCommands, materialsBuffer, 0, materials);
}

void SceneManager::uploadData(
  std::span<const Vertex> vertices, std::span<const std::uint32_t> indices)
{
  unifiedVbuf = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = vertices.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "unifiedVbuf",
  });

  unifiedIbuf = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = indices.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "unifiedIbuf",
  });

  transferHelper.uploadBuffer<Vertex>(*oneShotCommands, unifiedVbuf, 0, vertices);
  transferHelper.uploadBuffer<std::uint32_t>(*oneShotCommands, unifiedIbuf, 0, indices);
}

void SceneManager::createCullingBuffers()
{
  instancingBuffers = {
    .instanceMatrices = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = instanceMatrices.size() * sizeof(glm::mat4x4),
      .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "instanceMatricesBuffer",
    }),
    .instanceMeshesIndicies = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = instanceMeshIndicies.size() * sizeof(std::uint32_t),
      .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "instanceMeshesIndiciesBuffer",
    }),
    .meshes = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = meshes.size() * sizeof(Mesh),
      .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "meshesBuffer",
    }),
    .renderElements = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = renderElements.size() * sizeof(RenderElement),
      .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "renderElementsBuffer",
    }),
    .renderElementBoxes = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = relemBoxes.size() * sizeof(RenderElementBoundingBox),
      .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "renderElementBoxesBuffer",
    }),
  };

  transferHelper.uploadBuffer<glm::mat4x4>(
    *oneShotCommands, instancingBuffers.instanceMatrices, 0, instanceMatrices);
  transferHelper.uploadBuffer<std::uint32_t>(
    *oneShotCommands, instancingBuffers.instanceMeshesIndicies, 0, instanceMeshIndicies);
  transferHelper.uploadBuffer<Mesh>(
    *oneShotCommands, instancingBuffers.meshes, 0, meshes);
  transferHelper.uploadBuffer<RenderElement>(
    *oneShotCommands, instancingBuffers.renderElements, 0, renderElements);
  transferHelper.uploadBuffer<RenderElementBoundingBox>(
    *oneShotCommands, instancingBuffers.renderElementBoxes, 0, relemBoxes);
}

void SceneManager::createIndirectDrawBuffers()
{
  std::vector<std::uint32_t> matricesOffsetsInd = {};
  matricesOffsetsInd.resize(renderElements.size());

  for (const auto& meshIndex : instanceMeshIndicies)
  {
    const Mesh mesh = meshes[meshIndex];

    for (std::uint32_t i = 0; i < mesh.relemCount; ++i)
    {
      ++matricesOffsetsInd[mesh.firstRelem + i];
    }
  }
  for (std::uint32_t i = 0; i < renderElements.size() - 1; ++i)
  {
    matricesOffsetsInd[i + 1] += matricesOffsetsInd[i];
  }
  for (std::uint32_t i = 1; i < renderElements.size(); ++i)
  {
    matricesOffsetsInd[renderElements.size() - i] = matricesOffsetsInd[renderElements.size() - i - 1];
  }
  matricesOffsetsInd[0] = 0;

  std::vector<VkDrawIndexedIndirectCommand> drawCmds = {};
  drawCmds.reserve(renderElements.size());

  for (std::uint32_t i = 0; i < renderElements.size(); ++i)
  {
    drawCmds.push_back(VkDrawIndexedIndirectCommand{
      .indexCount = renderElements[i].indexCount,
      .instanceCount = 0,
      .firstIndex = renderElements[i].indexOffset,
      .vertexOffset = (int)renderElements[i].vertexOffset,
      .firstInstance = matricesOffsetsInd[i],
    });
  }

  indiretDrawBuffers = {
    .drawCommands = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = renderElements.size() * sizeof(VkDrawIndexedIndirectCommand),
      .bufferUsage = vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "drawCommandsBuffer",
    }),
    .drawMatricesIndicies = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = instanceMatrices.size() * sizeof(std::uint32_t),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "drawMatricesIndiciesBuffer",
    }),
  };

  transferHelper.uploadBuffer<VkDrawIndexedIndirectCommand>(
    *oneShotCommands, indiretDrawBuffers.drawCommands, 0, drawCmds);
}

void SceneManager::createHieghtMap()
{
  hieghtMap = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{4096, 4096, 1},
    .name = "terrainHeightMap",
    .format = vk::Format::eR32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled});

  hieghtMapSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .addressMode = vk::SamplerAddressMode::eMirroredRepeat, .name = "hieghtMapSampler"});

  float* hieghtMapData = new float[4096 * 4096];

  for (int i = 0; i < 4096; ++i)
  {
    for (int j = 0; j < 4096; ++j)
    {
      hieghtMapData[i * 4096 + j] = 0;
    }
  }

  {
    int octaveNum = 1;

    float a = 1.0f;
    float f = 1.0f / 4096.0f;
    float d = 0.0f;
    for (int k = 0; k < octaveNum; ++k)
    {
      for (int i = 0; i < 4096; ++i)
      {
        for (int j = 0; j < 4096; ++j)
        {
          hieghtMapData[i * 4096 + j] += a * perlin(j * f + d, i * f + d);
        }
      }
      a /= 2.0f;
      f *= 2.0f;
      d += 0.031f;
    }

    for (int i = 0; i < 4096; ++i)
    {
      for (int j = 0; j < 4096; ++j)
      {
        hieghtMapData[i * 4096 + j] = (hieghtMapData[i * 4096 + j] + 1.0f) * 0.5f;
      }
    }
  }

  transferHelper.uploadImage(
    *oneShotCommands, hieghtMap, 0, 0,
    std::span<const std::byte>(reinterpret_cast<const std::byte*>(hieghtMapData), 4096 * 4096 * 4));

  delete[] hieghtMapData;
}

void SceneManager::createLightSources()
{
  lights =
  {
    LightSource{
      .pos = {-1.f, 2.f, 2.f, 1.f},
      .dir = {},
      .color = {1.f, 0.f, 0.f, 1.f},
    },
    LightSource{
      .pos = {0.f, 2.f, 2.f, 1.f},
      .dir = {},
      .color = {0.f, 1.f, 0.f, 1.f},
    },
    LightSource{
      .pos = {1.f, 2.f, 2.f, 1.f},
      .dir = {},
      .color = {0.f, 0.f, 1.f, 1.f},
    },
  };

  lightSourcesBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = lights.size() * sizeof(LightSource),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU,
    .name = "lightSourcesBuffer",
  });

  transferHelper.uploadBuffer<LightSource>(*oneShotCommands, lightSourcesBuffer, 0, lights);

  lightsBufferPtr = lightSourcesBuffer.map();
}

void SceneManager::createParticleElements()
{
  {
    std::vector<std::string> textureToLoad = {
      GRAPHICS_COURSE_RESOURCES_ROOT "/particles/circle_01.png",
      GRAPHICS_COURSE_RESOURCES_ROOT "/particles/fire_01.png",
      GRAPHICS_COURSE_RESOURCES_ROOT "/particles/flare_01.png",
      GRAPHICS_COURSE_RESOURCES_ROOT "/particles/light_01.png",
      GRAPHICS_COURSE_RESOURCES_ROOT "/particles/magic_04.png",
      GRAPHICS_COURSE_RESOURCES_ROOT "/particles/star_09.png",
    };

    particleTextures.reserve(textureToLoad.size());

    int width, height, channels;
    for (uint32_t i = 0; i < textureToLoad.size(); ++i)
    {
      const auto &fileName = textureToLoad[i];

      unsigned char* imageData = stbi_load(
        fileName.c_str(), &width, &height, &channels, 4);

      particleTextures.push_back(etna::get_context().createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{static_cast<unsigned>(width), static_cast<unsigned>(height), 1},
        .name = std::format("particle%d", i),
        .format = vk::Format::eR8G8B8A8Srgb,
        .imageUsage = vk::ImageUsageFlagBits::eSampled |
                      vk::ImageUsageFlagBits::eTransferDst,
      }));

      transferHelper.uploadImage(*oneShotCommands, particleTextures.back(), 0, 0,
          std::span<const std::byte>(reinterpret_cast<const std::byte*>(imageData), width * height * 4));

      stbi_image_free(imageData);
    }
  }

  {
    emitters = {
      ParticleEmitter{
        .position = {1.0f, 1.0f, -2.0f, 1.0f},
        .initialVelocity = {2.0f, 2.0f, 0.0f, 1.0f},
        .particleColor = {0.f, 1.f, 1.f, 1.f},
        .textureId = 0,
      },
      ParticleEmitter{
        .position = {0.0f, 1.0f, -2.0f, 1.0f},
        .initialVelocity = {0.0f, 2.0f, 0.0f, 1.0f},
        .particleColor = {1.f, 0.f, 1.f, 1.f},
        .textureId = 1,
        .particleGravityK = 0.5f
      },
      ParticleEmitter{
        .position = {-1.0f, 1.0f, -2.0f, 1.0f},
        .initialVelocity = {-2.0f, 2.0f, 0.0f, 1.0f},
        .particleColor = {1.f, 1.f, 0.f, 1.f},
        .textureId = 2,
        .particleGravityK = -0.5f
      },
    };

    std::vector<ParticleEmitterRuntime> clearRuntime(emitters.size());

    particlesBuffers = {
      .emittersBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
        .size = emitters.size() * sizeof(ParticleEmitter),
        .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        .name = "emittersBuffer",
      }),
      .emittersRuntimeBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
        .size = emitters.size() * sizeof(ParticleEmitterRuntime),
        .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
        .name = "emittersRuntimeBuffer",
      }),
      .emittersOrderBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
        .size = emitters.size() * sizeof(uint32_t),
        .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
        .name = "emittersOrderBuffer",
      }),
      .particlesBuffers = {},
      .particlesOrderBuffers = {},
      .drawCommands = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
        .size = renderElements.size() * sizeof(VkDrawIndirectCommand),
        .bufferUsage = vk::BufferUsageFlagBits::eIndirectBuffer |
                      vk::BufferUsageFlagBits::eTransferDst |
                      vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
        .name = "particlesDrawCommandsBuffer",
      }),
    };

    transferHelper.uploadBuffer<ParticleEmitter>(
      *oneShotCommands, particlesBuffers.emittersBuffer, 0, emitters);
    transferHelper.uploadBuffer<ParticleEmitterRuntime>(
      *oneShotCommands, particlesBuffers.emittersRuntimeBuffer, 0, clearRuntime);

    emittersBufferPtr = particlesBuffers.emittersBuffer.map();
  }

  particlesBuffers.particlesBuffers.reserve(emitters.size());
  particlesBuffers.particlesOrderBuffers.reserve(emitters.size());
  for (uint32_t i = 0; i < emitters.size(); ++i)
  {
    particlesBuffers.particlesBuffers.push_back(etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = 256 * sizeof(Particle),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = std::format("particlesBuffer%d", i),
    }));

    particlesBuffers.particlesOrderBuffers.push_back(etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = 256 * sizeof(uint32_t),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = std::format("particlesOrderBuffer%d", i),
    }));
  }

  {
    std::vector<VkDrawIndirectCommand> drawCmds = {};
    drawCmds.reserve(emitters.size());

    for (std::uint32_t i = 0; i < emitters.size(); ++i)
      drawCmds.push_back(VkDrawIndirectCommand{
        .vertexCount = 6,
        .instanceCount = 0,
        .firstVertex = 0,
        .firstInstance = 0,
      });

    transferHelper.uploadBuffer<VkDrawIndirectCommand>(
      *oneShotCommands, particlesBuffers.drawCommands, 0, drawCmds);
  }
}


etna::VertexByteStreamFormatDescription SceneManager::getVertexFormatDescription()
{
  return etna::VertexByteStreamFormatDescription{
    .stride = sizeof(Vertex),
    .attributes = {
      etna::VertexByteStreamFormatDescription::Attribute{
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = 0,
      },
      etna::VertexByteStreamFormatDescription::Attribute{
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = sizeof(glm::vec4),
      },
    }};
}
