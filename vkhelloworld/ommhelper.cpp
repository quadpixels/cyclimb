#include <assert.h>
#include <stdio.h>

#include <filesystem>

#include <glm/glm.hpp>
#include <omm.hpp>

#include "stb_image.h"

extern const char* g_tex_files[2];

struct Vertex {
  alignas(16) glm::vec3 pos;
  alignas(16) glm::vec3 color;
  alignas(16) glm::vec2 uv;
};
const int NUM_VERTS = 6;
extern Vertex g_vertices[NUM_VERTS];

static void Log(omm::MessageSeverity severity, const char* message, void* userArg)
{
  const char* sev = "";
  switch (severity)
  {
  case omm::MessageSeverity::Info:
    sev = "INFO";
    break;
  case omm::MessageSeverity::Warning:
    sev = "WARNING";
    break;
  case omm::MessageSeverity::PerfWarning:
    sev = "PERF_WARNING";
    break;
  case omm::MessageSeverity::Fatal:
    sev = "FATAL";
    break;
  }

  printf("[omm-sdk] [%s] %s\n", sev, message);
}

const omm::Cpu::BakeResultDesc* bakeOmmForMask() {
  printf("[bakeOmmForMask]\n");
  std::filesystem::path mask_path(g_tex_files[1]);

  omm::BakerCreationDesc desc{};
  desc.type = omm::BakerType::CPU;
  desc.messageInterface.messageCallback = &Log;

  omm::Baker baker;
  omm::Result res = omm::CreateBaker(desc, &baker);

  int tex_w, tex_h, tex_ch;
  stbi_uc* stbi_pixels = stbi_load(mask_path.string().c_str(),
    &tex_w, &tex_h, &tex_ch, STBI_rgb_alpha);

  std::vector<float> alpha_values;
  for (int y = 0; y < tex_h; y++) {
    for (int x = 0; x < tex_w; x++) {
      int idx = (x + y * tex_w) * 4;
      alpha_values.push_back(stbi_pixels[idx] > 0 ? 1.0f : 0.0f);
    }
  }

  omm::Cpu::TextureDesc texture_desc{};
  texture_desc.alphaCutoff = 0.5f;
  texture_desc.flags = omm::Cpu::TextureFlags::None;
  texture_desc.format = omm::Cpu::TextureFormat::FP32;
  texture_desc.mipCount = 1;

  omm::Cpu::TextureMipDesc mips{};
  mips.height = tex_h;
  mips.width = tex_w;
  mips.textureData = (void*)alpha_values.data();
  texture_desc.mips = &mips;

  omm::Cpu::Texture texture{};
  res = omm::Cpu::CreateTexture(baker, texture_desc, &texture);
  assert(res == omm::Result::SUCCESS);

  std::vector<int> index_buffer;
  std::vector<glm::vec2> texcoords;
  for (int i = 0; i < NUM_VERTS; i++) {
    index_buffer.push_back(i);
    texcoords.push_back(g_vertices[i].uv);
  }

  omm::Cpu::BakeInputDesc input_desc{};
  input_desc.alphaCutoff = 0.5;
  input_desc.alphaCutoffGreater = omm::OpacityState::Opaque;
  //input_desc.alphaCutoffGT = omm::OpacityState::Opaque;        // Deprecated
  //input_desc.alphaCutoffLE = omm::OpacityState::Transparent;   // Deprecated
  input_desc.alphaCutoffLessEqual = omm::OpacityState::Transparent;
  input_desc.alphaMode = omm::AlphaMode::Test;
  input_desc.bakeFlags = (omm::Cpu::BakeFlags)((int)omm::Cpu::BakeFlags::EnableInternalThreads | (int)omm::Cpu::BakeFlags::Force32BitIndices);
  //input_desc.degenTriState = omm::SpecialIndex::FullyUnknownOpaque;  // Deprecated
  input_desc.dynamicSubdivisionScale = 1.0f;
  input_desc.format = omm::Format::OC1_2_State;
  input_desc.formats = nullptr;
  input_desc.indexBuffer = index_buffer.data();
  input_desc.indexCount = NUM_VERTS;
  input_desc.indexFormat = omm::IndexFormat::UINT_32;
  input_desc.maxArrayDataSize = (uint32_t)(-1);
  input_desc.maxSubdivisionLevel = 6;
  input_desc.maxWorkloadSize = (uint64_t)(-1);
  input_desc.nearDuplicateDeduplicationFactor = 0.15;
  input_desc.rejectionThreshold = 0;
  input_desc.runtimeSamplerDesc.addressingMode = omm::TextureAddressMode::Wrap;
  input_desc.runtimeSamplerDesc.filter = omm::TextureFilterMode::Linear;
  input_desc.runtimeSamplerDesc.borderAlpha = 0;
  input_desc.subdivisionLevels = nullptr;
  input_desc.texCoordFormat = omm::TexCoordFormat::UV32_FLOAT;
  input_desc.texCoords = texcoords.data();
  input_desc.texCoordStrideInBytes = sizeof(float) * 2;
  input_desc.texture = texture;
  input_desc.unknownStatePromotion = omm::UnknownStatePromotion::ForceOpaque;
  input_desc.unresolvedTriState = omm::SpecialIndex::FullyUnknownOpaque;

  omm::Cpu::BakeResult result{};
  printf(">> bake\n");
  res = omm::Cpu::Bake(baker, input_desc, &result);
  assert(res == omm::Result::SUCCESS);

  const omm::Cpu::BakeResultDesc* res_desc;
  omm::Cpu::GetBakeResultDesc(result, &res_desc);

  omm::Cpu::DeserializedDesc des_desc{};
  des_desc.inputDescs = &input_desc;
  des_desc.numInputDescs = 1;
  des_desc.resultDescs = res_desc;
  des_desc.numResultDescs = 1;
  omm::Cpu::SerializedResult ser_res{};
  printf(">> serialize\n");
  omm::Cpu::Serialize(baker, des_desc, &ser_res);
  const omm::Cpu::BlobDesc* blob_desc;
  omm::Cpu::GetSerializedResultDesc(ser_res, &blob_desc);
  //omm::Debug::SaveBinaryToDisk(g_baker, *blob_desc, (mask_fn.string() + ".bin").c_str());

  omm::Debug::SaveImagesDesc save_images_desc{};
  save_images_desc.oneFile = true;
  std::string save_img_path = "mask_omm";
  save_images_desc.path = save_img_path.c_str();
  save_images_desc.detailedCutout = false;
  save_images_desc.filePostfix = "image";
  printf(">> save images\n");
  omm::Debug::SaveAsImages(baker, input_desc, res_desc, save_images_desc);

  printf("Result desc:\n");
  printf("  Array data size: %u\n", res_desc->arrayDataSize);
  printf("  Index count %u, type %u, ", res_desc->indexCount, res_desc->indexFormat);
  assert(res_desc->indexFormat == omm::IndexFormat::UINT_32);
  for (uint32_t i = 0; i < res_desc->indexCount; i++) {
    const uint32_t ix = ((uint32_t*)(res_desc->indexBuffer))[i];
    printf("%u ", ix);
  }
  printf("\n");
  printf("  Index histogram count: %u\n", res_desc->indexHistogramCount);
  for (uint32_t i = 0; i < res_desc->indexHistogramCount; i++) {
    const omm::Cpu::OpacityMicromapUsageCount omuc = res_desc->indexHistogram[i];
    printf("    [%u]: count=%u format=%u level=%u\n", i, omuc.count, omuc.format, omuc.subdivisionLevel);
  }
  printf("  Desc array count: %u\n", res_desc->descArrayCount);
  for (uint32_t i = 0; i < res_desc->descArrayCount; i++) {
    const omm::Cpu::OpacityMicromapDesc& desc = res_desc->descArray[i];
    printf("    [%u]: offset=%u format=%u level=%u\n", i, desc.offset, desc.format, desc.subdivisionLevel);
  }

  stbi_image_free(stbi_pixels);
  return res_desc;
}