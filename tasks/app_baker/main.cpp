#include <filesystem>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include "tiny_gltf.h"

using Path = std::filesystem::path;


struct RenderElement
{
  std::uint32_t vertexOffset;
  std::uint32_t indexOffset;
  std::uint32_t indexCount;
};

struct Mesh
{
  std::uint32_t firstRelem;
  std::uint32_t relemCount;
};

struct ProcessedInstances
{
  std::vector<glm::mat4x4> matrices;
  std::vector<std::uint32_t> meshes;
};

struct Vertex
{
  glm::vec4 positionAndNormal;
  glm::vec4 texCoordAndTangentAndPadding;
};

struct ProcessedMeshes
{
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<RenderElement> relems;
  std::vector<Mesh> meshes;
};

std::uint32_t encode_normal(glm::vec3 normal)
{
  const std::uint8_t x = static_cast<std::uint8_t>(glm::round((normal.x) * 127.0f));
  const std::uint8_t y = static_cast<std::uint8_t>(glm::round((normal.y) * 127.0f));
  const std::uint8_t z = static_cast<std::uint8_t>(glm::round((normal.z) * 127.0f));

  const std::uint32_t sx = static_cast<std::uint32_t>(x);
  const std::uint32_t sy = static_cast<std::uint32_t>(y) << 8;
  const std::uint32_t sz = static_cast<std::uint32_t>(z) << 16;
  const std::uint32_t sw = static_cast<std::uint32_t>(127) << 24;

  return sx | sy | sz | sw;
}



std::optional<tinygltf::Model> load_model(Path model_path)
{
  tinygltf::TinyGLTF loader;
  loader.SetImagesAsIs(true);

  bool success = false;
  tinygltf::Model model;
  std::string error;
  std::string warning;

  std::string ext = model_path.extension().string();
  if (ext == ".gltf")
    success = loader.LoadASCIIFromFile(&model, &error, &warning, model_path.string());
  else if (ext == ".glb")
    success = loader.LoadBinaryFromFile(&model, &error, &warning, model_path.string());
  else
  {
    spdlog::error("glTF: Unknown glTF file extension. Expected .gltf or .glb.");
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

int save_model(tinygltf::Model& model, Path& path)
{
  tinygltf::TinyGLTF saver;
  saver.SetImagesAsIs(true);

  if (!saver.WriteGltfSceneToFile(&model, path.string(), false, false, true, false))
  {
    spdlog::error("glTF: Failed to save model!");
    return 1;
  }

  return 0;
}



ProcessedMeshes process_meshes(const tinygltf::Model& model)
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



int bake_model(Path model_path)
{
  Path bakedModelPath;
  Path bakedBinPath;
  {
    Path modelDir = model_path.parent_path();
    Path modelName = model_path.stem();
    bakedModelPath = modelDir / modelName += "_baked.gltf";
    bakedBinPath = modelDir / modelName += "_baked.bin";
  }


  tinygltf::Model model;
  {
    spdlog::info("Loading model...");
    auto maybeModel = load_model(model_path);
    if (!maybeModel.has_value())
      return 1;
    model = std::move(*maybeModel);
    spdlog::info("Model loaded.");
  }
  model.extensionsRequired.push_back("KHR_mesh_quantization");
  model.extensionsUsed.push_back("KHR_mesh_quantization");



  spdlog::info("Processing meshes...");
  auto [vertices, indices, relems, meshes] = process_meshes(model);

  std::size_t indiciesOffset = 0;
  std::size_t indiciesSize = indices.size() * sizeof(uint32_t);
  std::size_t verticesOffset = (indiciesSize + 15) / 16 * 16;
  std::size_t verticesSize = vertices.size() * sizeof(Vertex);
  spdlog::info("Meshes processed.");



  spdlog::info("Building buffers...");
  {
    tinygltf::Buffer bakedBuffer;
    bakedBuffer.name = bakedBinPath.stem().string();
    bakedBuffer.uri = bakedBinPath.filename().string();
    bakedBuffer.data.resize(verticesOffset + verticesSize);

    std::memcpy(bakedBuffer.data.data() + indiciesOffset, indices.data(), indiciesSize);
    std::memcpy(bakedBuffer.data.data() + verticesOffset, vertices.data(), verticesSize);

    model.buffers.clear();
    model.buffers.push_back(bakedBuffer);
  }
  spdlog::info("Buffers built.");



  spdlog::info("Building buffer views...");
  {
    tinygltf::BufferView bakedBufferViews[2]{};

    bakedBufferViews[0].name = "baked_indicies";
    bakedBufferViews[0].buffer = 0;
    bakedBufferViews[0].byteOffset = indiciesOffset;
    bakedBufferViews[0].byteLength = indiciesSize;
    bakedBufferViews[0].byteStride = 0;
    bakedBufferViews[0].target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

    bakedBufferViews[1].name = "baked_vertices";
    bakedBufferViews[1].buffer = 0;
    bakedBufferViews[1].byteOffset = verticesOffset;
    bakedBufferViews[1].byteLength = verticesSize;
    bakedBufferViews[1].byteStride = sizeof(Vertex);
    bakedBufferViews[1].target = TINYGLTF_TARGET_ARRAY_BUFFER;

    model.bufferViews.clear();
    model.bufferViews.push_back(bakedBufferViews[0]);
    model.bufferViews.push_back(bakedBufferViews[1]);
  }
  spdlog::info("Buffer views built.");



  spdlog::info("Building accessors...");
  std::vector<tinygltf::Accessor> newAccessors;
  for (std::uint32_t i = 0; i < model.meshes.size(); ++i)
  {
      auto& curMesh = model.meshes[i];

      for (std::uint32_t j = 0; j < curMesh.primitives.size(); ++j)
      {
        auto& curPrim = curMesh.primitives[j];

        const auto normalIt = curPrim.attributes.find("NORMAL");
        const auto texcoordIt = curPrim.attributes.find("TEXCOORD_0");
        const auto tangentIt = curPrim.attributes.find("TANGENT");

        const bool hasNormals = normalIt != curPrim.attributes.end();
        const bool hasTexcoord = texcoordIt != curPrim.attributes.end();
        const bool hasTangents = tangentIt != curPrim.attributes.end();

        std::uint32_t& indexOffset = relems[meshes[i].firstRelem + j].indexOffset;
        std::uint32_t& indexCount = relems[meshes[i].firstRelem + j].indexCount;
        std::uint32_t& vertexOffset = relems[meshes[i].firstRelem + j].vertexOffset;

        std::uint32_t maxIndex = 0;
        glm::vec3 maxValues = glm::vec3(vertices[vertexOffset].positionAndNormal);
        glm::vec3 minValues = glm::vec3(vertices[vertexOffset].positionAndNormal);
        for (std::uint32_t k = 0; k < indexCount; ++k)
        {
          std::uint32_t& curIndex = indices[indexOffset + k];

          maxIndex = std::max(maxIndex, curIndex);
          maxValues = glm::max(maxValues, glm::vec3(vertices[vertexOffset + curIndex].positionAndNormal));
          minValues = glm::min(minValues, glm::vec3(vertices[vertexOffset + curIndex].positionAndNormal));
        }

        tinygltf::Accessor bakedAccessors[5]{};

        bakedAccessors[0].name = "baked_indicies_accessor";
        bakedAccessors[0].bufferView = 0;
        bakedAccessors[0].byteOffset = indexOffset * sizeof(uint32_t);
        bakedAccessors[0].count = indexCount;
        bakedAccessors[0].normalized = false;
        bakedAccessors[0].type = TINYGLTF_TYPE_SCALAR;
        bakedAccessors[0].componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;

        bakedAccessors[1].name = "baked_position_accessor";
        bakedAccessors[1].bufferView = 1;
        bakedAccessors[1].byteOffset = vertexOffset * sizeof(Vertex);
        bakedAccessors[1].count = maxIndex + 1;
        bakedAccessors[1].normalized = false;
        bakedAccessors[1].type = TINYGLTF_TYPE_VEC3;
        bakedAccessors[1].componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        bakedAccessors[1].maxValues = {maxValues.x, maxValues.y, maxValues.z};
        bakedAccessors[1].minValues = {minValues.x, minValues.y, minValues.z};

        bakedAccessors[2].name = "baked_normal_accessor";
        bakedAccessors[2].bufferView = 1;
        bakedAccessors[2].byteOffset = vertexOffset * sizeof(Vertex) + 3 * sizeof(float);
        bakedAccessors[2].count = maxIndex + 1;
        bakedAccessors[2].normalized = true;
        bakedAccessors[2].type = TINYGLTF_TYPE_VEC3;
        bakedAccessors[2].componentType = TINYGLTF_COMPONENT_TYPE_BYTE;

        bakedAccessors[3].name = "baked_texCoord_accessor";
        bakedAccessors[3].bufferView = 1;
        bakedAccessors[3].byteOffset = vertexOffset * sizeof(Vertex) + 4 * sizeof(float);
        bakedAccessors[3].count = maxIndex + 1;
        bakedAccessors[3].normalized = false;
        bakedAccessors[3].type = TINYGLTF_TYPE_VEC2;
        bakedAccessors[3].componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;

        bakedAccessors[4].name = "baked_tangent_accessor";
        bakedAccessors[4].bufferView = 1;
        bakedAccessors[4].byteOffset = vertexOffset * sizeof(Vertex) + 6 * sizeof(float);
        bakedAccessors[4].count = maxIndex + 1;
        bakedAccessors[4].normalized = true;
        bakedAccessors[4].type = TINYGLTF_TYPE_VEC4;
        bakedAccessors[4].componentType = TINYGLTF_COMPONENT_TYPE_BYTE;


        curPrim.indices = static_cast<int>(newAccessors.size());
        newAccessors.push_back(bakedAccessors[0]);

        curPrim.attributes.clear();

        curPrim.attributes.insert({"POSITION", static_cast<int>(newAccessors.size())});
        newAccessors.push_back(bakedAccessors[1]);

        if (hasNormals)
        {
          curPrim.attributes.insert({"NORMAL", static_cast<int>(newAccessors.size())});
          newAccessors.push_back(bakedAccessors[2]);
        }
        if (hasTexcoord)
        {
          curPrim.attributes.insert({"TEXCOORD_0", static_cast<int>(newAccessors.size())});
          newAccessors.push_back(bakedAccessors[3]);
        }
        if (hasTangents)
        {
          curPrim.attributes.insert({"TANGENT", static_cast<int>(newAccessors.size())});
          newAccessors.push_back(bakedAccessors[4]);
        }
      }
  }
  model.accessors = std::move(newAccessors);
  spdlog::info("Accessors built.");



  spdlog::info("Saving model...");
  save_model(model, bakedModelPath);
  spdlog::info("Model saved.");



  spdlog::info("Model baked.");

  return 0;
}



int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    spdlog::error(
      "Wrong number of arguments. Expected 1, got {}. Usage: {} <file_path>\n",
      argc - 1, Path(argv[0]).filename().string().c_str());
    return 1;
  }

  spdlog::info("argc good, baking...");
  return bake_model(argv[1]);
}
