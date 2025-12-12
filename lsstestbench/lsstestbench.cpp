#include <stdio.h>

#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <source_location>
#include <vector>

#include <glm/glm.hpp>

#include "../dxrquestions/MyFramework.h"
#ifdef NDEBUG
#include "x64/Release/CompiledShaders/shaders.hlsl.h"
#else
#include "x64/Debug/CompiledShaders/shaders.hlsl.h"
#endif

#include <omm.hpp>
#include <d3dx12.h>
#include "lssintersect.h"

const omm::Cpu::BakeResultDesc* bakeOmmForMask(uint32_t level) { return nullptr; }    // DUMMY.
LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) { return 0; }  // DUMMY.

#ifndef CE
#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}
#endif

MyFramework* g_myframework{};
MyRtPipeline g_my_rtpipeline{};
ID3D12RootSignature* g_global_rootsig{};
ID3D12Resource* g_uav_resource{}, * g_uav_resource_cpu{};
ID3D12DescriptorHeap* g_cbvsrvuav_heap{};
ID3D12Resource* g_perscene_cb_cpu{};
ID3D12Resource* g_blas_result{};
ID3D12Resource* g_tlas_result{};
std::ofstream* g_outfile;
bool g_compare_with_cpu{ false };
extern bool g_use_do_hit_old;

static float RandFloat() {
  return rand() * 1.0 / RAND_MAX;
}

struct PerSceneCB {
  glm::vec3 ray_origin;
  float tmin;
  glm::vec3 ray_dir;
  float tmax;
};

static_assert(sizeof(PerSceneCB) == 32);
static_assert(offsetof(PerSceneCB, tmin) == 12);

// Just test 1 LSS
std::vector<uint32_t> g_lss_indices = {
  0, 1
};

std::vector<std::pair<glm::vec3, glm::vec3>> g_lss_pos_pairs = {
  {
    {  0.01, 0, 2 },
    {  0, 0, -3 }
  }
};
std::vector<std::pair<float, float>> g_lss_radii_pairs = {
  { 1.0f, 1.0f }
};

std::vector<std::vector<glm::vec3>> g_tri_verts = {
  {
    { -1, 0, 0 },
    {  0, 1, 0 },
    {  1, 0, 0 }
  }
};

std::vector<PerSceneCB> g_per_scene_cbs = {
  {
    { 0, 0, 10 },
    0.0f,
    { 0, 0, -1 },
    10000.0f
  },
  {
    { 0, 0, 10 },
    0.0f,
    { 0, 0, -1 },
    10000.0f
  },
};

const float TMIN_DEFAULT = 0.0f;
const float TMAX_DEFAULT = 10000.0f;

static uint32_t GetInputSize() {
  assert(g_lss_pos_pairs.size() == g_lss_radii_pairs.size());
  assert(g_lss_radii_pairs.size() + g_tri_verts.size()  == g_per_scene_cbs.size());
  return g_lss_pos_pairs.size() + g_tri_verts.size();
}

static void ClearInputs() {
  g_lss_pos_pairs.clear();
  g_lss_radii_pairs.clear();
  g_per_scene_cbs.clear();
  g_tri_verts.clear();
}

void ReadInputFile(const char* fn) {
  ClearInputs();

  // Open file for reading
  std::ifstream file(fn);
  if (!file) {
    printf("Error: Could not open file %s\n", fn);
    return;
  }

  std::string line;
  size_t lineNumber = 0;

  // Read file line by line
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    if (line.find("//") != std::string::npos ||
      line.find("#") != std::string::npos) {  // Contains Comments, will be ignored
      continue;
    }
    glm::vec3 lss0pos, lss1pos;
    float r0, r1;
    glm::vec3 ray_dir, ray_origin;
    float tmin, tmax;
    if (iss >> lss0pos.x >> lss0pos.y >> lss0pos.z >> r0 
            >> lss1pos.x >> lss1pos.y >> lss1pos.z >> r1
            >> ray_origin.x >> ray_origin.y >> ray_origin.z
            >> ray_dir.x >> ray_dir.y >> ray_dir.z) {
      if (iss >> tmin >> tmax) {
        ;  // Use supplied tmin and tmax
      }
      else {
        tmin = 0;
        tmax = 1e4;
      }
      g_lss_pos_pairs.emplace_back(lss0pos, lss1pos);
      g_lss_radii_pairs.emplace_back(r0, r1);
      PerSceneCB c{};
      c.ray_dir = ray_dir;
      c.ray_origin = ray_origin;
      c.tmin = tmin;
      c.tmax = tmax;
      g_per_scene_cbs.push_back(c);
    }
    else {
      continue;
    }
  }

  printf("%u inputs read.\n", GetInputSize());

  file.close();
}

int main(int argc, char** argv) {
  g_lss_pos_pairs.clear();
  g_lss_radii_pairs.clear();
  g_tri_verts.clear();

  for (uint32_t i = 0; i < argc; i++) {
    float x1, x2, x3, x4;
    if (i + 1 < argc) {
      x1 = std::atof(argv[i + 1]);
    }
    if (i + 3 < argc) {
      x2 = std::atof(argv[i + 2]);
      x3 = std::atof(argv[i + 3]);
    }
    if (i + 4 < argc) {
      x4 = std::atof(argv[i + 4]);
    }
    if (!strcmp(argv[i], "-lss0") && i + 4 < argc) {
      if (g_lss_pos_pairs.empty()) {
        g_lss_pos_pairs.push_back(std::make_pair(glm::vec3(0), glm::vec3(0)));
        g_lss_radii_pairs.push_back(std::make_pair(0, 0));
      }
      g_lss_pos_pairs[0].first.x = x1;
      g_lss_pos_pairs[0].first.y = x2;
      g_lss_pos_pairs[0].first.z = x3;
      g_lss_radii_pairs[0].first = x4;
      printf("LSS0 pos set to (%g,%g,%g) r=%g\n", x1, x2, x3, x4);
    }
    if (!strcmp(argv[i], "-lss1") && i + 4 < argc) {
      if (g_lss_pos_pairs.empty()) {
        g_lss_pos_pairs.push_back(std::make_pair(glm::vec3(0), glm::vec3(0)));
        g_lss_radii_pairs.push_back(std::make_pair(0, 0));
      }
      g_lss_pos_pairs[0].second.x = x1;
      g_lss_pos_pairs[0].second.y = x2;
      g_lss_pos_pairs[0].second.z = x3;
      g_lss_radii_pairs[0].second = x4;
      printf("LSS1 pos set to (%g,%g,%g) r=%g\n", x1, x2, x3, x4);
    }
    if (!strcmp(argv[i], "-tri0") && i + 3 < argc) {
      if (g_tri_verts.empty()) {
        g_tri_verts.push_back(std::vector<glm::vec3>{glm::vec3(0), glm::vec3(0), glm::vec3(0)});
      }
      g_tri_verts[0][0].x = x1;
      g_tri_verts[0][0].y = x2;
      g_tri_verts[0][0].z = x3;
    }
    if (!strcmp(argv[i], "-tri1") && i + 3 < argc) {
      if (g_tri_verts.empty()) {
        g_tri_verts.push_back(std::vector<glm::vec3>{glm::vec3(0), glm::vec3(0), glm::vec3(0)});
      }
      g_tri_verts[0][1].x = x1;
      g_tri_verts[0][1].y = x2;
      g_tri_verts[0][1].z = x3;
    }
    if (!strcmp(argv[i], "-tri2") && i + 3 < argc) {
      if (g_tri_verts.empty()) {
        g_tri_verts.push_back(std::vector<glm::vec3>{glm::vec3(0), glm::vec3(0), glm::vec3(0)});
      }
      g_tri_verts[0][2].x = x1;
      g_tri_verts[0][2].y = x2;
      g_tri_verts[0][2].z = x3;
    }
    if (!strcmp(argv[i], "-ro") && i + 3 < argc) {
      g_per_scene_cbs[0].ray_origin.x = x1;
      g_per_scene_cbs[0].ray_origin.y = x2;
      g_per_scene_cbs[0].ray_origin.z = x3;
      printf("ray origin set to (%g,%g,%g)\n", x1, x2, x3);
    }
    if (!strcmp(argv[i], "-rd") && i + 3 < argc) {
      g_per_scene_cbs[0].ray_dir.x = x1;
      g_per_scene_cbs[0].ray_dir.y = x2;
      g_per_scene_cbs[0].ray_dir.z = x3;
      printf("ray dir set to (%g,%g,%g)\n", x1, x2, x3);
    }
    if (!strcmp(argv[i], "-tmin") && i + 1 < argc) {
      g_per_scene_cbs[0].tmin = x1;
      printf("tmin set to %g\n", x1);
    }
    if (!strcmp(argv[i], "-tmax") && i + 1 < argc) {
      g_per_scene_cbs[0].tmax = x1;
      printf("tmax set to %g\n", x1);
    }
    if (!strcmp(argv[i], "-i") && i + 1 < argc) {
      ReadInputFile(argv[i + 1]);
    }
    if (!strcmp(argv[i], "-random") && i + 1 < argc) {
      ClearInputs();
      for (uint32_t i = 0; i < (uint32_t)(x1); i++) {
        glm::vec3 p0, p1, rd, ro;
        float r0, r1;
        const float L = 10.0;
        p0.x = RandFloat() * L; p0.y = RandFloat() * L; p0.z = RandFloat() * L;
        p1.x = RandFloat() * L; p1.y = RandFloat() * L; p1.z = RandFloat() * L;
        rd.x = RandFloat() * L; rd.y = RandFloat() * L; rd.z = RandFloat() * L;
        ro.x = RandFloat() * L; ro.y = RandFloat() * L; ro.z = RandFloat() * L;
        r0 = RandFloat() * L * 0.5f; r1 = RandFloat() * L * 0.5f;
        g_lss_pos_pairs.emplace_back(p0, p1);
        g_lss_radii_pairs.emplace_back(r0, r1);
        PerSceneCB c{};
        c.ray_dir = rd; c.ray_origin = ro; c.tmin = 0; c.tmax = 10000.0;
        g_per_scene_cbs.push_back(c);
      }
    }
    if (!strcmp(argv[i], "-o") && i + 1 < argc) {
      std::string ofn(argv[i + 1]);
      if (std::filesystem::exists(ofn)) {
        printf("Oh! output file exists. Not writing to it\n");
        exit(0);
      }
      else {
        printf("Will output to %s\n", ofn.c_str());
        g_outfile = new std::ofstream(ofn, std::ios::trunc);
      }
    }
    if (!strcmp(argv[i], "--help") || argc == 1) {
      printf("LSS TestBench!\n");
      printf("Requires: NVAPI-enabled card with LSS support\n");
      printf("Usage: %s params\n", argv[0]);
      printf("\n");
      printf("Do intersection on the command line:\n");
      printf(" -lss0 x y z r -lss1 x y z r -ro x y z -rd x y z [-tmin x] [-tmax x] : Perform 1 intersection test\n");
      printf(" -random x                                                           : Perform x random tests, LSS and ray params randomized between 0 and 10\n");
      printf("\n");
      printf("Read tests from file:\n");
      printf(" -i filename   : Read input file\n");
      printf(" -o filename   : Write to output file, the file should not exist yet\n");
      exit(0);
    }
    if (!strcmp(argv[i], "--comparewithcpu") || !strcmp(argv[i], "--cpu")) {
      g_compare_with_cpu = true;
      printf("Will compare with cpu.\n");
    }
    if (!strcmp(argv[i], "--cpuold")) {
      g_use_do_hit_old = true;
      printf("Using do_hit_old.\n");
    }
  }

  g_myframework = new MyFramework();
  g_myframework->InitDeviceAndCommandQ();
  g_myframework->InitNVAPI();
  g_myframework->CreateBufferForUAVAccess(128, &g_uav_resource);
  g_myframework->CreateBufferForCPUAccess(128, &g_uav_resource_cpu);
  // [0] = debug uav, [1] = NVAPI uav, [2] = AS SRV, [3] = PerScene CBV
  g_myframework->CreateCBVSRVUAVHeap(&g_cbvsrvuav_heap, nullptr, 4);
  uint32_t nvapi_uav = 100;
  g_myframework->CreateRtGlobalRootSig(&g_global_rootsig, 1, 1, 1, true, nvapi_uav);
  g_myframework->CreateUAVUintBuffer(g_uav_resource, 32, sizeof(uint32_t), g_cbvsrvuav_heap, 0);
  g_myframework->CreateNullUAV(g_cbvsrvuav_heap, 1);
  g_myframework->CreateBufferForCPUSideData(nullptr, 256, &g_perscene_cb_cpu);
  g_myframework->CreateCBVBuffer(g_perscene_cb_cpu, g_cbvsrvuav_heap, 3, 256);
  
  enum TestType {
    TEST_TYPE_LSS,
    TEST_TYPE_TRIANGLE
  };
  
  std::vector<std::pair<TestType, uint32_t>> test_idxes;
  for (uint32_t i = 0; i < g_tri_verts.size(); i++) {
    test_idxes.emplace_back(TestType::TEST_TYPE_TRIANGLE, i);
  }
  for (uint32_t i = 0; i < g_lss_pos_pairs.size(); i++) {
    test_idxes.emplace_back(TestType::TEST_TYPE_LSS, i);
  }
  uint32_t num_tests = test_idxes.size();

  for (uint32_t i = 0; i < num_tests; i++) {
    const auto [test_type, pertest_idx] = test_idxes.at(i);

    // Resources that might be used
    ID3D12Resource* lss_poses_resource{};
    ID3D12Resource* lss_radii_resource{};
    ID3D12Resource* lss_indices_list_resource{};
    std::pair<glm::vec3, glm::vec3> pos_pair;
    std::pair<float, float> radii_pair;
    ID3D12Resource* tri_verts_resource{};
    std::vector<glm::vec3> tri_verts;

    switch (test_type) {
      case TestType::TEST_TYPE_LSS: {
        pos_pair = g_lss_pos_pairs.at(pertest_idx);
        radii_pair = g_lss_radii_pairs.at(pertest_idx);

        // BVH
        g_myframework->CreateBufferForCPUSideData(&pos_pair, sizeof(pos_pair), &lss_poses_resource);
        g_myframework->CreateBufferForCPUSideData(&radii_pair, sizeof(radii_pair), &lss_radii_resource);
        g_myframework->CreateBufferForCPUSideData(g_lss_indices.data(), g_lss_indices.size() * sizeof(g_lss_indices[0]), &lss_indices_list_resource);
        g_myframework->BuildDummyLSS(
          &g_blas_result, &g_tlas_result,
          lss_poses_resource, nullptr,
          lss_radii_resource,
          lss_indices_list_resource, nullptr,
          2,  // vert count
          NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED,
          NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_LIST,
          2,
          false
        );
        g_myframework->CreateSRVAccelerationStructure(g_tlas_result, g_cbvsrvuav_heap, 2);
        break;
      }
      case TestType::TEST_TYPE_TRIANGLE: {
        tri_verts = g_tri_verts.at(pertest_idx);
        g_myframework->CreateBufferForCPUSideData(tri_verts.data(), sizeof(glm::vec3) * 3, &tri_verts_resource);
        g_myframework->BuildDummyTriNVAPI(&g_blas_result, &g_tlas_result, tri_verts_resource, 3);
        g_myframework->CreateSRVAccelerationStructure(g_tlas_result, g_cbvsrvuav_heap, 2);
        break;
      }
    }


    MyFramework::MyRtShaderListInfo info{};
    info.raygen_shader = L"MyRayGenShader";
    info.miss_shader = L"MyMissShader";
    info.closest_hit_shader = L"MyClosestHitShader";
    info.hitgroup_name = L"MyHitGroup";
    info.dxil_lib_bytecode = (void*)g_lssShader;
    info.dxil_lib_length = sizeof(g_lssShader);
    info.hitgroup_type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    g_myframework->CreateMyRtPipeline(&g_my_rtpipeline, g_global_rootsig, info);
    D3D12_DISPATCH_RAYS_DESC* drd = &(g_my_rtpipeline.dispatch_rays_desc);
    drd->Width = 1;
    drd->Height = 1;
    drd->Depth = 1;

    PerSceneCB h_perscene_cb{};
    {
      char* mapped;
      h_perscene_cb = g_per_scene_cbs.at(i);

      g_perscene_cb_cpu->Map(0, nullptr, (void**)&mapped);
      memcpy(mapped, &h_perscene_cb, sizeof(h_perscene_cb));
      g_perscene_cb_cpu->Unmap(0, nullptr);
    }

    // RENDER
    ID3D12CommandAllocator* command_allocator = g_myframework->GetCommandAllocator();
    ID3D12GraphicsCommandList4* command_list = g_myframework->GetGraphicsCommandList();
    CE(command_allocator->Reset());
    CE(command_list->Reset(command_allocator, nullptr));
    ResourceBarrierTransition(command_list, g_uav_resource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->SetComputeRootSignature(g_global_rootsig);
    command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)&g_cbvsrvuav_heap);
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle_uav;
    handle_uav = CD3DX12_GPU_DESCRIPTOR_HANDLE(g_cbvsrvuav_heap->GetGPUDescriptorHandleForHeapStart(), 0, g_myframework->GetCBVSRVUAVDescriptorSize());
    command_list->SetComputeRootDescriptorTable(0, handle_uav);
    command_list->SetPipelineState1(g_my_rtpipeline.rt_state_object);
    command_list->DispatchRays(drd);
    ResourceBarrierTransition(command_list, g_uav_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ResourceBarrierTransition(command_list, g_uav_resource_cpu, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    command_list->CopyResource(g_uav_resource_cpu, g_uav_resource);
    command_list->Close();
    ID3D12CommandQueue* command_queue = g_myframework->GetCommandQueue();
    command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
    g_myframework->WaitForPreviousFrame();

    uint32_t* mapped;
    g_uav_resource_cpu->Map(0, nullptr, (void**)&mapped);
    g_uav_resource_cpu->Unmap(0, nullptr);
    float t_hit = *((float*)mapped);

    switch (test_type) {
    case TestType::TEST_TYPE_LSS: {
      printf("#%u: lss0=(%g,%g,%g,%g), lss1=(%g,%g,%g,%g), ray=(%g,%g,%g)-(%g,%g,%g), tmin=%g, tmax=%g, ",
        i,
        pos_pair.first.x, pos_pair.first.y, pos_pair.first.z, radii_pair.first,
        pos_pair.second.x, pos_pair.second.y, pos_pair.second.z, radii_pair.second,
        h_perscene_cb.ray_origin.x, h_perscene_cb.ray_origin.y, h_perscene_cb.ray_origin.z,
        h_perscene_cb.ray_dir.x, h_perscene_cb.ray_dir.y, h_perscene_cb.ray_dir.z,
        h_perscene_cb.tmin, h_perscene_cb.tmax
      );

      if (t_hit < 0) {
        printf("Miss");
        if (g_outfile) {
          (*g_outfile) << "Miss";
        }
      }
      else {
        char buf[100];
        snprintf(buf, sizeof(buf), "Hit, t=%g (0x%08x)", t_hit, *mapped);
        printf("%s", buf);
        if (g_outfile) {
          (*g_outfile) << buf;
        }
      }

      // CPU
      if (g_compare_with_cpu) {
        printf(" vs CPU: ");
        if (g_outfile) {
          (*g_outfile) << " vs CPU: ";
        }
        Ray ray(h_perscene_cb.ray_origin, h_perscene_cb.ray_dir);
        LinearSweptSphere lss(pos_pair.first, radii_pair.first, pos_pair.second, radii_pair.second);
        HitRecord rec{};
        bool intersected = lss.Hit(ray, h_perscene_cb.tmin, h_perscene_cb.tmax, rec);
        if (intersected) {
          char buf[100];
          snprintf(buf, sizeof(buf), "Hit, t=%g (0x%08x)", rec.t, *((uint32_t*)&(rec.t)));
          printf("%s", buf);
          if (g_outfile) {
            (*g_outfile) << buf;
          }
        }
      }

      printf("\n");
      if (g_outfile) {
        (*g_outfile) << "\n";
      }

      lss_poses_resource->Release();
      lss_radii_resource->Release();
      lss_indices_list_resource->Release();
      break;
    }
    case TestType::TEST_TYPE_TRIANGLE: {
      printf("#%u: tri0=(%g,%g,%g), tr1=(%g,%g,%g), tri2=(%g,%g,%g), ray=(%g,%g,%g)-(%g,%g,%g), tmin=%g, tmax=%g, ",
        i,
        tri_verts[0].x, tri_verts[0].y, tri_verts[0].z,
        tri_verts[1].x, tri_verts[1].y, tri_verts[1].z,
        tri_verts[2].x, tri_verts[2].y, tri_verts[2].z,
        h_perscene_cb.ray_origin.x, h_perscene_cb.ray_origin.y, h_perscene_cb.ray_origin.z,
        h_perscene_cb.ray_dir.x, h_perscene_cb.ray_dir.y, h_perscene_cb.ray_dir.z,
        h_perscene_cb.tmin, h_perscene_cb.tmax
      );

      if (t_hit < 0) {
        printf("Miss");
        if (g_outfile) {
          (*g_outfile) << "Miss";
        }
      }
      else {
        char buf[100];
        snprintf(buf, sizeof(buf), "Hit, t=%g (0x%08x)", t_hit, *mapped);
        printf("%s", buf);
        if (g_outfile) {
          (*g_outfile) << buf;
        }
      }

      tri_verts_resource->Release();
      break;
    }
    default: {
      assert(0 && "Unimplemented");
    }
    }

    g_blas_result->Release();
    g_tlas_result->Release();
  }

  // DONE
  g_myframework->Deinit();
  g_uav_resource->Release();
  if (g_outfile) {
    g_outfile->close();
    delete g_outfile;
  }
  return 0;
}