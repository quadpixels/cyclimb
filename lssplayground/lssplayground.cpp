// LSS PLAYGROUND.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <source_location>
#include <unordered_set>

#include <GLFW/glfw3.h>
#include <glfw/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include "../dxrquestions/MyFramework.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_dx12.h"

#ifdef NDEBUG
#include "x64/Release/lss_nvapi.hlsl.h"
#include "x64/Release/lss_procedural.hlsl.h"
#include "x64/Release/showdiff_cs.hlsl.h"
#include "x64/Release/lss_cs.hlsl.h"
#else
#include "x64/Debug/lss_nvapi.hlsl.h"
#include "x64/Debug/lss_procedural.hlsl.h"
#include "x64/Debug/showdiff_cs.hlsl.h"
#include "x64/Debug/lss_cs.hlsl.h"
#endif

MyFramework* g_myframework{};
GLFWwindow* g_window{};
uint32_t WIN_W = 960, WIN_H = 600;
uint32_t VIEW_W = 320, VIEW_H = 320;

enum RenderMethod {
  RENDER_METHOD_LSS_NVAPI,
  RENDER_METHOD_INTERSECTION_SHADER,
  RENDER_METHOD_COMPUTE_SHADER,
  RENDER_METHOD_CPU_SP,
  RENDER_METHOD_CPU_DP,
};

ID3D12RootSignature* g_lss_rootsig{};
ID3D12RootSignature* g_proc_rootsig{};
MyRtPipeline g_lss_pipeline{};
MyRtPipeline g_proc_pipeline{};
ID3D12Resource* g_lss_output_resource{};
ID3D12Resource* g_proc_output_resource{};
ID3D12DescriptorHeap* g_lss_srv_uav_cbv_heap{}, * g_lss_srv_uav_cbv_heap_cpu{};
ID3D12DescriptorHeap* g_proc_srv_uav_cbv_heap{}, * g_proc_srv_uav_cbv_heap_cpu{};
ImTextureID g_lss_output_imgui_texid{};
ImTextureID g_proc_output_imgui_texid{};
ID3D12Resource* g_lss_tlas_resource{}, * g_lss_blas_resource{};
ID3D12Resource* g_proc_tlas_resource{}, * g_proc_blas_resource{};
ID3D12Resource* g_perscene_cb{};
// Interesction Shader path.
ID3D12Resource* g_lss_poses_resource{};
ID3D12Resource* g_lss_radii_resource{};

// Completely compute shader path.
ID3D12PipelineState* g_lss_cs_pipeline{};
ID3D12RootSignature* g_lss_cs_rootsig{};
ID3D12DescriptorHeap* g_lss_cs_srv_uav_cbv_heap{}, * g_lss_cs_srv_uav_cbv_heap_cpu{};
ID3D12Resource* g_lss_cs_output_resource{};
ImTextureID g_lss_cs_imgui_texid{};

// CPU path.
ID3D12Resource* g_cpu_sp_output_resource_cpuvsible{};
ID3D12Resource* g_cpu_sp_output_resource_viz{};
ID3D12Resource* g_cpu_dp_output_resource_cpuvsible{};
ID3D12Resource* g_cpu_dp_output_resource_viz{};
ID3D12DescriptorHeap* g_cpu_output_srv_uav_cbv_heap{};

template<typename Vec3, typename Float>
void InitCPURender(uint32_t w, uint32_t h,
  std::vector<glm::vec3> ps,
  std::vector<float> rs,
  glm::mat4 iv, glm::mat4 ip
);

template<typename Vec3, typename Float>
void UpdateCPURenderResults(ID3D12Resource* res);

template<typename Vec3, typename Float>
bool IsCPUDone();

ImTextureID g_cpu_sp_output_imgui_texid;
ImTextureID g_cpu_dp_output_imgui_texid;

ID3D12PipelineState* g_showdiff_cs_pipeline{};
ID3D12RootSignature* g_showdiff_rootsig{};
ID3D12DescriptorHeap* g_showdiff_srv_uav_cbv_heap{}, *g_showdiff_srv_uav_cbv_heap_cpu{};
ID3D12Resource* g_showdiff_output_resource{};
ImTextureID g_showdiff_imgui_texid{};

RenderMethod g_render_method_1{RenderMethod::RENDER_METHOD_LSS_NVAPI};
RenderMethod g_render_method_2{RenderMethod::RENDER_METHOD_INTERSECTION_SHADER};

bool g_dirty{ true };
bool g_cpu_sp_dirty{ true };
bool g_cpu_dp_dirty{ true };
bool g_dir_dirty{ true };

struct PerSceneCB {
  DirectX::XMMATRIX inverse_view;
  DirectX::XMMATRIX inverse_proj;
  int cam_mode;  // 0 = perspective, 1 = orthogonal
};
PerSceneCB h_perscene_cb;

struct MyLssTestCase {
  std::vector<glm::vec3> lss_poses;
  std::vector<float> lss_radii;
  glm::vec3 ro, rd;
  std::string title;

  std::string ToString() {
    char buf[100];
    snprintf(buf, sizeof(buf), "lss:(%g,%g,%g,%g)-(%g,%g,%g,%g),ro=(%g,%g,%g),rd=(%g,%g,%g)",
      lss_poses[0].x, lss_poses[0].y, lss_poses[0].z, lss_radii[0],
      lss_poses[1].x, lss_poses[1].y, lss_poses[1].z, lss_radii[1],
      ro.x, ro.y, ro.z, rd.x, rd.y, rd.z);
    return std::string(buf);
  }
  
  std::string GetTitle() {
    if (title.empty()) return ToString();
    return title;
  }

  MyLssTestCase() = default;

  MyLssTestCase(const glm::vec3& pa, float ra, const glm::vec3& pb, float rb, const glm::vec3 ro, const glm::vec3 rd, std::string titel = "") {
    lss_poses.push_back(pa); lss_poses.push_back(pb);
    lss_radii.push_back(ra); lss_radii.push_back(rb);
    this->ro = ro; this->rd = rd;
    this->title = titel;
  }

  std::string ToJSONString() const {
    nlohmann::json ret{};
    nlohmann::json poses = nlohmann::json::array();
    for (uint32_t i = 0; i < lss_poses.size(); i++) {
      nlohmann::json p = nlohmann::json::array();
      p.push_back(lss_poses[i].x);
      p.push_back(lss_poses[i].y);
      p.push_back(lss_poses[i].z);
      poses.push_back(p);
    }
    ret["poses"] = poses;
    nlohmann::json radii = nlohmann::json::array();
    for (uint32_t i = 0; i < lss_radii.size(); i++) {
      radii.push_back(lss_radii[i]);
    }
    ret["radii"] = radii;
    nlohmann::json jro = nlohmann::json::array();
    jro.push_back(ro.x);
    jro.push_back(ro.y);
    jro.push_back(ro.z);
    ret["ro"] = jro;
    nlohmann::json jrd = nlohmann::json::array();
    jrd.push_back(rd.x);
    jrd.push_back(rd.y);
    jrd.push_back(rd.z);
    ret["rd"] = jrd;
    ret["title"] = title;

    return ret.dump();
  }

  static bool FromJSONString(const std::string& s, MyLssTestCase* parsed) {
    nlohmann::json j = nlohmann::json::parse(s);
    if (j.is_discarded()) {
      printf("Oh! could not parse.\n");
    }
    
    nlohmann::json& poses = j["poses"];
    for (uint32_t i = 0; i < poses.size(); i++) {
      nlohmann::json& p = poses[i];
      float x = p[0], y = p[1], z = p[2];
      parsed->lss_poses.push_back(glm::vec3(x, y, z));
    }

    nlohmann::json& radii = j["radii"];
    for (uint32_t i = 0; i < radii.size(); i++) {
      float x = radii[i];
      parsed->lss_radii.push_back(x);
    }

    nlohmann::json& jro = j["ro"];
    parsed->ro.x = jro[0];
    parsed->ro.y = jro[1];
    parsed->ro.z = jro[2];

    nlohmann::json& jrd = j["rd"];
    parsed->rd.x = jrd[0];
    parsed->rd.y = jrd[1];
    parsed->rd.z = jrd[2];

    parsed->title = j["title"];
    return true;
  }
};
bool g_testcases_dirty{ true };
bool g_testcases_show_title{ false };
std::vector<MyLssTestCase> g_testcases = {
  MyLssTestCase(glm::vec3(0, 0, -9.0f), 1.0f, glm::vec3(-3000.0f, 0.0f, -3009.0f), 8.0f, glm::vec3(23.8f, 0.0f, -10.8f), glm::vec3(-0.954947f, 0.0f, -0.296775f), "nv glitch1"),
  MyLssTestCase(glm::vec3(0, -5.0f, 0.0f), 1.0f, glm::vec3(0.0f, 5.0f, -0.0f), 1.0f, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), "small capsule"),
  MyLssTestCase(glm::vec3(0, -2000.0f, 0.0f), 1.0f, glm::vec3(0.0f, 2000.0f, -0.0f), 1.0f, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), "long capsule"),
};
std::vector<std::string> g_testcase_names;
constexpr const char* FILE_NAME = "lssplayground.txt";
void LoadTestCasesFromFile() {
  std::string txt("");
  std::ifstream ifs(FILE_NAME);
  uint32_t num_loaded = 0;
  if (ifs.is_open()) {
    std::string line("");
    while (getline(ifs, line)) {
      MyLssTestCase tc;
      if (MyLssTestCase::FromJSONString(line, &tc)) {
        if (num_loaded == 0) {
          g_testcases.clear();
        }
        g_testcases.push_back(tc);
        num_loaded++;
      }
    }
    ifs.close();
  }
  printf("Loaded %u testcases.\n", num_loaded);
}
void SaveTestCasesToFile() {
  std::ofstream ofs(FILE_NAME, std::ios::trunc);
  for (uint32_t i = 0; i < g_testcases.size(); i++) {
    ofs << g_testcases[i].ToJSONString() << "\n";
  }
  ofs.close();
}

constexpr const float lss_len = 3000;
std::vector<glm::vec3> g_lss_poses = {
    { 0, 0, -9.0 },
    { -lss_len, 0, -9.0 - lss_len }
};
std::vector<float> g_lss_radii = {
  1,8
};
bool g_should_update_as{ false };

#ifndef CE
#define CE(x) { \
  const std::source_location location = std::source_location::current(); \
  if (FAILED(x)) { \
    printf("ERROR: %X at %s:%u\n", x, location.file_name(), location.line()); \
    throw std::exception(); \
  } \
}
#endif

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  return 0;
};

char g_axes[6]{};
glm::mat4 g_view, g_inv_view;
glm::mat4 g_proj, g_inv_proj;
glm::vec3 g_cam_pos(20.0f, 0.0f, 0.0f);
float g_azimuth = 180.0;
float g_elevation = 0.0;
glm::vec3 g_cam_dir(-1, 0, 0);

std::pair<float, float> RayDirToAzimuthAndElevation(const glm::vec3& dir) {
  float elevation = glm::degrees(asin(dir.y));
  float azimuth{};

  float x_proj = dir.x;
  float z_proj = dir.z;

  float proj_len = sqrt(x_proj * x_proj + z_proj * z_proj);

  if (proj_len < 1e-6f) {
    azimuth = 0.0f;
  }
  else {
    float azimuth_rad = atan2(-z_proj, x_proj);
    azimuth = glm::degrees(azimuth_rad);
    if (azimuth < 0.0f) azimuth += 360.0f;
  }
  return std::make_pair(azimuth, elevation);
}

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
  if (action == GLFW_PRESS)
  {
    switch (key)
    {
    case GLFW_KEY_ESCAPE:
      glfwTerminate();
      exit(0);
      break;
    case GLFW_KEY_W: g_axes[2] = -1; break;
    case GLFW_KEY_S: g_axes[2] = 1; break;
    case GLFW_KEY_A: g_axes[0] = -1; break;
    case GLFW_KEY_D: g_axes[0] = 1; break;
    case GLFW_KEY_E: g_axes[1] = -1; break;
    case GLFW_KEY_Q: g_axes[1] = 1; break;
    case GLFW_KEY_J: g_axes[3] = 1; break;
    case GLFW_KEY_L: g_axes[3] = -1; break;
    case GLFW_KEY_I: g_axes[4] = 1; break;
    case GLFW_KEY_K: g_axes[4] = -1; break;
    }
  }
  else if (action == GLFW_RELEASE) {
    switch (key) {
    case GLFW_KEY_W: case GLFW_KEY_S: g_axes[2] = 0; break;
    case GLFW_KEY_A: case GLFW_KEY_D: g_axes[0] = 0; break;
    case GLFW_KEY_Q: case GLFW_KEY_E: g_axes[1] = 0; break;
    case GLFW_KEY_J: case GLFW_KEY_L: g_axes[3] = 0; break;
    case GLFW_KEY_I: case GLFW_KEY_K: g_axes[4] = 0; break;
    }
  }
}

void CreateLSSPlaygroundWindow() {
  if (!glfwInit()) {
    printf("Oh! Could not initialize GLFW.\n");
    exit(1);
  }
  printf("GLFW inited.\n");
  GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* video_mode = glfwGetVideoMode(primary_monitor);
  printf("Video mode of primary monitor is %dx%d\n", video_mode->width, video_mode->height);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  g_window = glfwCreateWindow(WIN_W, WIN_H, "LSS Playground !", nullptr, nullptr);
  glfwSetKeyCallback(g_window, KeyCallback);
}

void RenderImGui(ID3D12GraphicsCommandList4* command_list) {
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGui::SetNextWindowSize(ImVec2(480, 240), ImGuiCond_Once);
  ImGui::SetNextWindowPos(ImVec2(0, 360), ImGuiCond_Once);

  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoResize
    | ImGuiWindowFlags_NoMove
    | ImGuiWindowFlags_NoCollapse;

  ImGui::Begin("LSS Playground.", nullptr, window_flags | ImGuiWindowFlags_NoTitleBar);

  static char test_case_name[128] = "";
  ImGui::InputText("testcasename", test_case_name, sizeof(test_case_name));

  ImGui::Text("LSS in right-hand coords");
  ImGui::SameLine();
  if (ImGui::Button("Update##1")) {
    g_should_update_as = true;
  }
  ImGui::SetNextItemWidth(240.0f);
  ImGui::InputFloat3("pa", &g_lss_poses[0][0], "%g");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80.0f);
  ImGui::InputFloat("ra", &g_lss_radii[0]);

  ImGui::SetNextItemWidth(240.0f);
  ImGui::InputFloat3("pb", &g_lss_poses[1][0], "%g");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80.0f);
  ImGui::InputFloat("rb", &g_lss_radii[1]);

  ImGui::Text("Camera (use WASDQEIJKL to move & look)");
  ImGui::SameLine();
  if (ImGui::Button("Update##2")) {
    std::tie(g_azimuth, g_elevation) = RayDirToAzimuthAndElevation(glm::normalize(g_cam_dir));
  }
  ImGui::SetNextItemWidth(240.0f);
  ImGui::InputFloat3("pos", &g_cam_pos[0], "%g");
  ImGui::SetNextItemWidth(240.0f);
  ImGui::InputFloat3("dir", &g_cam_dir[0], "%g");
  ImGui::End();

  ImGui::SetNextWindowSize(ImVec2(480, 240), ImGuiCond_Once);
  ImGui::SetNextWindowPos(ImVec2(480, 360), ImGuiCond_Once);
  ImGui::Begin("Test case list.", nullptr, window_flags | ImGuiWindowFlags_NoTitleBar);
  std::vector<const char*> items;
  for (uint32_t i = 0; i < g_testcase_names.size(); i++) {
    items.push_back(g_testcase_names[i].c_str());
  }
  
  static int showtitle = 0, last_showtitle = -999;
  ImGui::Text("Test cases"); ImGui::SameLine();
  ImGui::RadioButton("Title", &showtitle, 0); ImGui::SameLine();
  ImGui::RadioButton("Detail", &showtitle, 1);
  if (showtitle != last_showtitle) {
    g_testcases_dirty = true;  // Update name list
    g_testcases_show_title = (showtitle == 0);
    last_showtitle = showtitle;
  }
  ImGui::SameLine();
  if (ImGui::Button("Load from file")) {
    LoadTestCasesFromFile();
    g_testcases_dirty = true;
    g_cpu_sp_dirty = g_cpu_dp_dirty = g_dirty = g_dir_dirty = g_should_update_as = true;
  }
  
  ImGui::SameLine();
  if (ImGui::Button("Save to file")) {
    SaveTestCasesToFile();
  }

  static int item_current = 0, item_last = -99;
  ImGui::SetNextItemWidth(460);
  ImGui::ListBox("##Test cases", &item_current, items.data(), items.size(), 5);
  if (item_last != item_current) {
    if (item_current >= 0 && item_current < g_testcases.size()) {
      const MyLssTestCase& tc = g_testcases.at(item_current);
      g_lss_poses = tc.lss_poses;
      g_lss_radii = tc.lss_radii;
      g_cam_pos = tc.ro;
      g_cam_dir = tc.rd;
      std::tie(g_azimuth, g_elevation) = RayDirToAzimuthAndElevation(glm::normalize(g_cam_dir));
      g_cpu_sp_dirty = g_cpu_dp_dirty = g_dirty = g_dir_dirty = g_should_update_as = true;
      memset(test_case_name, 0x00, sizeof(test_case_name));
      memcpy(test_case_name, tc.title.c_str(), tc.title.size());
    }
    item_last = item_current;
  }

  if (ImGui::Button("Append")) {
    MyLssTestCase tc{};
    tc.lss_poses = g_lss_poses;
    tc.lss_radii = g_lss_radii;
    tc.ro = g_cam_pos;
    tc.rd = g_cam_dir;
    tc.title = std::string(test_case_name);
    g_testcases.push_back(tc);
    g_testcases_dirty = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Update")) {
    MyLssTestCase& tc = g_testcases.at(item_current);
    tc.lss_poses = g_lss_poses;
    tc.lss_radii = g_lss_radii;
    tc.ro = g_cam_pos;
    tc.rd = g_cam_dir;
    tc.title = std::string(test_case_name);
    g_testcases_dirty = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete")) {
    if (item_current >= 0 && item_current < g_testcases.size()) {
      g_testcases.erase(g_testcases.begin() + item_current);
      if (item_current >= g_testcases.size()) {
        item_current = g_testcases.size() - 1;
      }
      if (item_current < g_testcases.size()) {
        MyLssTestCase& tc = g_testcases.at(item_current);
        g_lss_poses = tc.lss_poses;
        g_lss_radii = tc.lss_radii;
        g_cam_pos = tc.ro;
        g_cam_dir = tc.rd;
        std::tie(g_azimuth, g_elevation) = RayDirToAzimuthAndElevation(glm::normalize(g_cam_dir));
        g_cpu_sp_dirty = g_cpu_dp_dirty = g_dirty = g_dir_dirty = g_should_update_as = true;
        memset(test_case_name, 0x00, sizeof(test_case_name));
        memcpy(test_case_name, tc.title.c_str(), tc.title.size());
        item_last = item_current;
      }
    }
  }

  ImGui::End();

  window_flags = window_flags | ImGuiWindowFlags_NoScrollbar
    | ImGuiWindowFlags_NoScrollWithMouse;

  ImGui::SetNextWindowSize(ImVec2(320, 360), ImGuiCond_Once);
  ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Once);

  const char* items_render_methods[] = {
    "LSS NVAPI",
    "Intersection Shader",
    "Compute Shader",
    "CPU (single precision)",
    "CPU (double precision)"
  };

  ImTextureID texture_ids[] = {
    g_lss_output_imgui_texid,
    g_proc_output_imgui_texid,
    g_lss_cs_imgui_texid,
    g_cpu_sp_output_imgui_texid,
    g_cpu_dp_output_imgui_texid,
  };

  ImGui::Begin("##input1", nullptr, window_flags | ImGuiWindowFlags_NoTitleBar);
  if (ImGui::BeginCombo("Output 1", items_render_methods[g_render_method_1], 0)) {
    for (uint32_t i = 0; i < _countof(items_render_methods); i++) {
      const bool is_selected = (i == static_cast<int>(g_render_method_1));
      if (ImGui::Selectable(items_render_methods[i], is_selected)) {
        g_render_method_1 = static_cast<RenderMethod>(i);
      }
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::Image(texture_ids[g_render_method_1], ImVec2(300, 300));
  ImGui::End();

  ImGui::SetNextWindowSize(ImVec2(320, 360), ImGuiCond_Once);
  ImGui::SetNextWindowPos(ImVec2(320, 0), ImGuiCond_Once);
  ImGui::Begin("##input2", nullptr, window_flags | ImGuiWindowFlags_NoTitleBar);
  if (ImGui::BeginCombo("Output 2", items_render_methods[g_render_method_2], 0)) {
    for (uint32_t i = 0; i < _countof(items_render_methods); i++) {
      const bool is_selected = (i == static_cast<int>(g_render_method_2));
      if (ImGui::Selectable(items_render_methods[i], is_selected)) {
        g_render_method_2 = static_cast<RenderMethod>(i);
      }
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::Image(texture_ids[g_render_method_2], ImVec2(300, 300));
  ImGui::End();

  ImGui::SetNextWindowSize(ImVec2(320, 360), ImGuiCond_Once);
  ImGui::SetNextWindowPos(ImVec2(640, 0), ImGuiCond_Once);
  ImGui::Begin("Difference", nullptr, window_flags);
  ImGui::Image(g_showdiff_imgui_texid, ImVec2(300, 300));
  ImGui::End();
  
  ImGui::Render();
  command_list->SetDescriptorHeaps(1, &(g_myframework->imgui_heap));
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);
}

void Update() {
  static double last_sec{};
  double sec = glfwGetTime();
  float delta_sec = sec - last_sec;
  last_sec = sec;

  float dist = delta_sec * 10.0;
  {
    glm::mat3 inv_view = glm::transpose(g_view);
    g_cam_pos += glm::vec3(inv_view[0]) * static_cast<float>(g_axes[0]) * dist;
    g_cam_pos += glm::vec3(inv_view[1]) * static_cast<float>(g_axes[1]) * dist;
    g_cam_pos += glm::vec3(inv_view[2]) * static_cast<float>(g_axes[2]) * dist;
    g_azimuth += 90.0f * delta_sec * g_axes[3];
    g_elevation += 90.0f * delta_sec * g_axes[4];
  }

  g_dirty = std::any_of(std::begin(g_axes), std::end(g_axes), [](int x) { return x != 0; });
  g_cpu_sp_dirty |= g_dirty;
  g_cpu_dp_dirty |= g_dirty;

  if (g_azimuth > 360.0f) g_azimuth -= 360.0f;
  if (g_azimuth < 0.0f) g_azimuth += 360.0f;
  if (g_elevation > 89.9f) g_elevation = 89.9f;
  if (g_elevation < -89.9f) g_elevation = -89.9f;

  glm::vec3 x_axis(1, 0, 0);
  x_axis = glm::mat3(glm::rotate(glm::mat4(1), glm::radians(g_elevation), glm::vec3(0, 0, 1)))* x_axis;
  x_axis = glm::mat3(glm::rotate(glm::mat4(1), glm::radians(g_azimuth), glm::vec3(0, 1, 0))) * x_axis;
  if (g_axes[3] != 0 || g_axes[4] != 0) {  // feedback to UI
    g_dir_dirty = true;
    g_cam_dir = x_axis;
  }

  g_view = glm::lookAt(g_cam_pos, g_cam_pos + x_axis, glm::vec3(0, 1, 0));

  // UPD
  g_inv_view = glm::inverse(g_view);
  g_inv_proj = glm::inverse(g_proj);
  GlmMat4ToDirectXMatrixColMajor(&h_perscene_cb.inverse_view, g_inv_view);
  GlmMat4ToDirectXMatrixColMajor(&h_perscene_cb.inverse_proj, g_inv_proj);
  h_perscene_cb.cam_mode = 0;

  void* mapped{};
  g_perscene_cb->Map(0, nullptr, &mapped);
  memcpy(mapped, &h_perscene_cb, sizeof(PerSceneCB));
  g_perscene_cb->Unmap(0, nullptr);

  // Which two outputs are diff'ed?
  ID3D12Resource* outputs[] = {
    g_lss_output_resource,
    g_proc_output_resource,
    g_lss_cs_output_resource,
    g_cpu_sp_output_resource_viz,
    g_cpu_dp_output_resource_viz
  };
  g_myframework->CreateSRVTexture2D(outputs[static_cast<int>(g_render_method_1)], g_showdiff_srv_uav_cbv_heap, 1);
  g_myframework->CreateSRVTexture2D(outputs[static_cast<int>(g_render_method_2)], g_showdiff_srv_uav_cbv_heap, 2);

  // Test case list
  if (g_testcases_dirty) {
    g_testcase_names.clear();
    for (uint32_t i = 0; i < g_testcases.size(); i++) {
      g_testcase_names.push_back(
        g_testcases_show_title ?
        g_testcases[i].GetTitle() :
        g_testcases[i].ToString()
      );
    }
  }
}

void Render() {
  D3D12_CPU_DESCRIPTOR_HANDLE handle_rtv = g_myframework->GetCurrRenderTargetCPUDescriptor();
  ID3D12CommandAllocator* command_allocator = g_myframework->GetCommandAllocator();
  ID3D12GraphicsCommandList4* command_list = g_myframework->GetGraphicsCommandList();
  CE(command_allocator->Reset());
  CE(command_list->Reset(command_allocator, nullptr));
  
  std::unordered_set<RenderMethod> used_methods;
  used_methods.insert(g_render_method_1);
  used_methods.insert(g_render_method_2);

  // Render target 1
  if (used_methods.count(RenderMethod::RENDER_METHOD_LSS_NVAPI) > 0) {
    command_list->SetComputeRootSignature(g_lss_rootsig);
    command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)(&g_lss_srv_uav_cbv_heap));
    D3D12_GPU_DESCRIPTOR_HANDLE handle_table = g_lss_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart();
    command_list->SetComputeRootDescriptorTable(0, handle_table);
    command_list->SetPipelineState1(g_lss_pipeline.rt_state_object);
    D3D12_DISPATCH_RAYS_DESC* drd = &(g_lss_pipeline.dispatch_rays_desc);
    drd->Width = VIEW_W;
    drd->Height = VIEW_H;
    drd->Depth = 1;
    ResourceBarrierTransition(command_list, g_lss_output_resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->DispatchRays(drd);
    ResourceBarrierTransition(command_list, g_lss_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
  }

  // Render target 2
  if (used_methods.count(RenderMethod::RENDER_METHOD_INTERSECTION_SHADER) > 0) {
    command_list->SetComputeRootSignature(g_proc_rootsig);
    command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)(&g_proc_srv_uav_cbv_heap));
    D3D12_GPU_DESCRIPTOR_HANDLE handle_table = g_proc_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart();
    command_list->SetComputeRootDescriptorTable(0, handle_table);
    command_list->SetPipelineState1(g_proc_pipeline.rt_state_object);
    ResourceBarrierTransition(command_list, g_proc_output_resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_DISPATCH_RAYS_DESC* drd = &(g_proc_pipeline.dispatch_rays_desc);
    drd->Width = VIEW_W;
    drd->Height = VIEW_H;
    drd->Depth = 1;
    command_list->DispatchRays(drd);
    ResourceBarrierTransition(command_list, g_proc_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
  }

  // Render target 3
  if (used_methods.count(RenderMethod::RENDER_METHOD_COMPUTE_SHADER) > 0) {
    command_list->SetComputeRootSignature(g_lss_cs_rootsig);
    command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)(&g_lss_cs_srv_uav_cbv_heap));
    D3D12_GPU_DESCRIPTOR_HANDLE handle_table = g_lss_cs_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart();
    command_list->SetComputeRootDescriptorTable(0, handle_table);
    command_list->SetPipelineState(g_lss_cs_pipeline);
    ResourceBarrierTransition(command_list, g_lss_cs_output_resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->Dispatch(VIEW_W / 8, VIEW_H / 8, 1);
    ResourceBarrierTransition(command_list, g_lss_cs_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
  }

  auto update_cpu_results = [&](ID3D12Resource* res_cpu, ID3D12Resource* res_gpu, bool is_dp) {
    if (is_dp) {
      UpdateCPURenderResults<glm::dvec3, double>(res_cpu);
    }
    else {
      UpdateCPURenderResults<glm::vec3, float>(res_cpu);
    }
    ResourceBarrierTransition(command_list, res_cpu, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ResourceBarrierTransition(command_list, res_gpu, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = res_gpu;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = res_cpu;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = {
        .Offset = 0, .Footprint = {
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .Width = (uint32_t)VIEW_W,
            .Height = (uint32_t)VIEW_H,
            .Depth = 1,
            .RowPitch = (uint32_t)(4 * VIEW_W),
        }
    };
    command_list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    ResourceBarrierTransition(command_list, res_cpu, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_GENERIC_READ);
    ResourceBarrierTransition(command_list, res_gpu, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
  };

  // CPU SP
  if (used_methods.count(RenderMethod::RENDER_METHOD_CPU_SP) > 0) {
    static double last_update_secs{ 0 };
    double secs = glfwGetTime();
    if (secs - last_update_secs > 0.1) {
      last_update_secs = secs;

      // Kick off refreshment
      // CPU
      if (g_cpu_sp_dirty && IsCPUDone<glm::vec3, float>()) {
        if (IsCPUDone<glm::vec3, float>()) {
          InitCPURender<glm::vec3, float>(VIEW_W, VIEW_H, g_lss_poses, g_lss_radii, g_inv_view, g_inv_proj);
          g_cpu_sp_dirty = false;
        }
      }

      update_cpu_results(g_cpu_sp_output_resource_cpuvsible, g_cpu_sp_output_resource_viz, false);
    }
  }

  // CPU DP
  if (used_methods.count(RenderMethod::RENDER_METHOD_CPU_DP) > 0) {
    static double last_update_secs1{ 0 };
    double secs = glfwGetTime();
    if (secs - last_update_secs1 > 0.1) {
      last_update_secs1 = secs;

      // Kick off refresh
      if (g_cpu_dp_dirty && IsCPUDone<glm::dvec3, double>()) {
        if (IsCPUDone<glm::dvec3, double>()) {
          InitCPURender<glm::dvec3, double>(VIEW_W, VIEW_H, g_lss_poses, g_lss_radii, g_inv_view, g_inv_proj);
          g_cpu_dp_dirty = false;
        }
      }

      update_cpu_results(g_cpu_dp_output_resource_cpuvsible, g_cpu_dp_output_resource_viz, true);
    }
  }

  // DIFF
  {
    command_list->SetComputeRootSignature(g_showdiff_rootsig);
    command_list->SetDescriptorHeaps(1, (ID3D12DescriptorHeap* const*)(&g_showdiff_srv_uav_cbv_heap));
    D3D12_GPU_DESCRIPTOR_HANDLE handle_table = g_showdiff_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart();
    command_list->SetComputeRootDescriptorTable(0, handle_table);
    command_list->SetPipelineState(g_showdiff_cs_pipeline);
    ResourceBarrierTransition(command_list, g_showdiff_output_resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->Dispatch(VIEW_W / 8, VIEW_H / 8, 1);
    ResourceBarrierTransition(command_list, g_showdiff_output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
  }

  // Overall render target
  float bg_color[] = { 0.15f, 0.15f, 0.15f, 1.0f };
  ID3D12Resource* rendertarget = g_myframework->GetCurrentRenderTarget();
  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_list->ClearRenderTargetView(handle_rtv, bg_color, 0, nullptr);
  command_list->OMSetRenderTargets(1, &handle_rtv, true, nullptr);

  RenderImGui(command_list);

  ResourceBarrierTransition(command_list, rendertarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

  CE(command_list->Close());
  ID3D12CommandQueue* command_queue = g_myframework->GetCommandQueue();
  command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&command_list);
  CE(g_myframework->Present());
  g_myframework->WaitForPreviousFrame();
}

void InitPipeline() {
  g_myframework->CreateNvapiEnabledGlobalRootSig(&g_lss_rootsig, 1, 1, 1, true, 999);
  MyFramework::MyRtShaderListInfo sli{};
  sli.dxil_lib_bytecode = static_cast<void*>(const_cast<uint8_t*>(g_LssNvapiShader));
  sli.dxil_lib_length = sizeof(g_LssNvapiShader);
  sli.raygen_shader = L"RayGen";
  sli.miss_shader = L"Miss";
  sli.closest_hit_shader = L"ClosestHit";
  sli.hitgroup_name = L"HitGroup";
  g_myframework->CreateMyRtPipeline(&g_lss_pipeline, g_lss_rootsig, sli);

  g_myframework->CreateNvapiEnabledGlobalRootSig(&g_proc_rootsig, 1, 3, 1, false, 0);
  sli.dxil_lib_bytecode = static_cast<void*>(const_cast<uint8_t*>(g_LssProceduralShader));
  sli.dxil_lib_length = sizeof(g_LssProceduralShader);
  sli.raygen_shader = L"RayGen";
  sli.miss_shader = L"Miss";
  sli.closest_hit_shader = L"ClosestHit";
  sli.hitgroup_name = L"HitGroup";
  sli.intersection_shader = L"Intersection";
  sli.hitgroup_type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
  g_myframework->CreateMyRtPipeline(&g_proc_pipeline, g_proc_rootsig, sli);

  g_myframework->CreateGlobalRootSig(&g_showdiff_rootsig, 1, 2, 0);
  g_myframework->CreateComputePipeline(&g_showdiff_cs_pipeline, g_showdiff_rootsig,
    static_cast<const void*>(g_ShowDiffComputeShader), sizeof(g_ShowDiffComputeShader));

  g_myframework->CreateGlobalRootSig(&g_lss_cs_rootsig, 1, 2, 1);
  g_myframework->CreateComputePipeline(&g_lss_cs_pipeline, g_lss_cs_rootsig,
    static_cast<const void*>(g_LssComputeShader), sizeof(g_LssComputeShader));
}

void InitResources() {
  // NVAPI LSS path
  uint32_t srv_uav_cbv_descriptor_size = g_myframework->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  g_myframework->CreateRtOutputResource(&g_lss_output_resource, VIEW_W, VIEW_H);
  // |For RT in RenderTarget 1                                | RenderTgt 1 in ImGui |
  //  [Rendertarget 1 UAV] [NVAPI UAV] [AS SRV] [PerScene CBV] [Rendertaget 1 SRV]    
  g_myframework->CreateCBVSRVUAVHeap(&g_lss_srv_uav_cbv_heap, &g_lss_srv_uav_cbv_heap_cpu, 5);
  g_myframework->CreateSRVTexture2D(g_lss_output_resource, g_lss_srv_uav_cbv_heap, 4);
  g_lss_output_imgui_texid = g_lss_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart().ptr + 4ULL * srv_uav_cbv_descriptor_size;
  g_myframework->CreateUAVTexture2D(g_lss_output_resource, g_lss_srv_uav_cbv_heap, 0);
  g_lss_srv_uav_cbv_heap->SetName(L"lss_srv_uav_cbv_heap");

  // Procedural path
  g_myframework->CreateRtOutputResource(&g_proc_output_resource, VIEW_W, VIEW_H);
  //
  //  [Rendertarget 2 UAV] [AS SRV] [LSS pos] [LSS radii]  [PerScene CBV] | [Rendertarget 2 SRV]
  g_myframework->CreateCBVSRVUAVHeap(&g_proc_srv_uav_cbv_heap, &g_proc_srv_uav_cbv_heap_cpu, 6);
  g_myframework->CreateSRVTexture2D(g_proc_output_resource, g_proc_srv_uav_cbv_heap, 5);
  g_proc_output_imgui_texid = g_proc_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart().ptr + 5ULL * srv_uav_cbv_descriptor_size;
  g_myframework->CreateUAVTexture2D(g_proc_output_resource, g_proc_srv_uav_cbv_heap, 0);
  g_proc_srv_uav_cbv_heap->SetName(L"proc_srv_uav_cbv_heap");

  // CS path
  // [Rendertarget 3 UAV] [LSS pos SRV] [LSS radii SRV] [PerScene CBV] | [Rendertarget 3 SRV]
  g_myframework->CreateRtOutputResource(&g_lss_cs_output_resource, VIEW_W, VIEW_H);
  g_myframework->CreateCBVSRVUAVHeap(&g_lss_cs_srv_uav_cbv_heap, &g_lss_cs_srv_uav_cbv_heap_cpu, 5);
  g_myframework->CreateUAVTexture2D(g_lss_cs_output_resource, g_lss_cs_srv_uav_cbv_heap, 0);
  g_myframework->CreateSRVTexture2D(g_lss_cs_output_resource, g_lss_cs_srv_uav_cbv_heap, 4);
  g_lss_cs_imgui_texid = g_lss_cs_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart().ptr + 4ULL * srv_uav_cbv_descriptor_size;

  // CPU path
  g_myframework->CreateCBVSRVUAVHeap(&g_cpu_output_srv_uav_cbv_heap, nullptr, 2);
  g_myframework->CreateBufferForCPUSideData(nullptr, VIEW_W * VIEW_H * 4, &g_cpu_sp_output_resource_cpuvsible);
  g_myframework->CreateRtOutputResource(&g_cpu_sp_output_resource_viz, VIEW_W, VIEW_H);
  g_myframework->CreateSRVTexture2D(g_cpu_sp_output_resource_viz, g_cpu_output_srv_uav_cbv_heap, 0);
  g_cpu_sp_output_imgui_texid = g_cpu_output_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart().ptr;
  g_myframework->CreateBufferForCPUSideData(nullptr, VIEW_W * VIEW_H * 4, &g_cpu_dp_output_resource_cpuvsible);
  g_myframework->CreateRtOutputResource(&g_cpu_dp_output_resource_viz, VIEW_W, VIEW_H);
  g_myframework->CreateSRVTexture2D(g_cpu_dp_output_resource_viz, g_cpu_output_srv_uav_cbv_heap, 1);
  g_cpu_dp_output_imgui_texid = g_cpu_output_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart().ptr + srv_uav_cbv_descriptor_size;

  // Same CB used for all paths
  size_t sz = AlignUp(sizeof(PerSceneCB), 256);
  g_myframework->CreateBufferForCPUSideData(nullptr, sz, &g_perscene_cb);
  g_myframework->CreateCBVBuffer(g_perscene_cb, g_lss_srv_uav_cbv_heap, 3, sz);
  g_myframework->CreateCBVBuffer(g_perscene_cb, g_proc_srv_uav_cbv_heap, 4, sz);
  g_myframework->CreateCBVBuffer(g_perscene_cb, g_lss_cs_srv_uav_cbv_heap, 3, sz);

  // Diff view
  g_myframework->CreateRtOutputResource(&g_showdiff_output_resource, VIEW_W, VIEW_H);
  // [RenderTarget UAV] [Output1 SRV] [Output2 SRV] | [Rendertarget SRV]
  g_myframework->CreateCBVSRVUAVHeap(&g_showdiff_srv_uav_cbv_heap, &g_showdiff_srv_uav_cbv_heap_cpu, 4);
  g_myframework->CreateUAVTexture2D(g_showdiff_output_resource, g_showdiff_srv_uav_cbv_heap, 0);
  g_myframework->CreateSRVTexture2D(g_showdiff_output_resource, g_showdiff_srv_uav_cbv_heap, 3);
  g_showdiff_imgui_texid = g_showdiff_srv_uav_cbv_heap->GetGPUDescriptorHandleForHeapStart().ptr + 3ULL * srv_uav_cbv_descriptor_size;
}

void InitSceneLSS() {
  if (g_lss_poses_resource) {
    g_lss_poses_resource->Release();
    g_lss_radii_resource->Release();
    g_lss_blas_resource->Release();
    g_lss_tlas_resource->Release();
  }
  g_myframework->CreateBufferForCPUSideData(g_lss_poses.data(), sizeof(g_lss_poses[0]) * g_lss_poses.size(), &g_lss_poses_resource);
  g_myframework->CreateBufferForCPUSideData(g_lss_radii.data(), sizeof(g_lss_radii[0]) * g_lss_radii.size(), &g_lss_radii_resource);
  g_myframework->BuildDummyLSS(&g_lss_blas_resource, &g_lss_tlas_resource,
    g_lss_poses_resource, nullptr,
    g_lss_radii_resource, nullptr,
    nullptr,
    2,
    NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED,
    NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_LIST,
    2,
    false);

  g_myframework->CreateSRVAccelerationStructure(g_lss_tlas_resource, g_lss_srv_uav_cbv_heap, 2);

  // SRVs used by intersection shader pass and CS pass
  g_myframework->CreateSRVBuffer(g_lss_poses_resource, g_proc_srv_uav_cbv_heap, 2, g_lss_poses.size(), sizeof(g_lss_poses[0]));
  g_myframework->CreateSRVBuffer(g_lss_radii_resource, g_proc_srv_uav_cbv_heap, 3, g_lss_radii.size(), sizeof(g_lss_radii[0]));
  g_myframework->CreateSRVBuffer(g_lss_poses_resource, g_lss_cs_srv_uav_cbv_heap, 1, g_lss_radii.size(), sizeof(g_lss_radii[0]));
  g_myframework->CreateSRVBuffer(g_lss_radii_resource, g_lss_cs_srv_uav_cbv_heap, 2, g_lss_radii.size(), sizeof(g_lss_radii[0]));
}

void InitSceneProcedural() {
  glm::vec3 lb(1e20, 1e20, 1e20), ub(-1e20, -1e20, -1e20);
  for (uint32_t i = 0; i < g_lss_poses.size(); i++) {
    glm::vec3 c = g_lss_poses[i];
    float r = g_lss_radii[i];
    glm::vec3 clch[] = {
      c - glm::vec3(r, r, r),
      c + glm::vec3(r, r, r)
    };
    for (uint32_t j = 0; j < 2; j++) {
      glm::vec3 x = clch[j];
      lb.x = std::min(lb.x, x.x);
      lb.y = std::min(lb.y, x.y);
      lb.z = std::min(lb.z, x.z);
      ub.x = std::max(ub.x, x.x);
      ub.y = std::max(ub.y, x.y);
      ub.z = std::max(ub.z, x.z);
    }
  }
  const float PAD = 0.0;
  lb -= glm::vec3(PAD, PAD, PAD);
  ub += glm::vec3(PAD, PAD, PAD);
  printf("[InitSceneProcedural] lb=(%g,%g,%g), ub=(%g,%g,%g)\n", lb.x, lb.y, lb.z, ub.x, ub.y, ub.z);
  std::vector<D3D12_RAYTRACING_AABB> aabbs = {
    { lb.x, lb.y, lb.z, ub.x, ub.y, ub.z }
  };

  if (g_proc_blas_resource) {
    g_proc_blas_resource->Release();
    g_proc_tlas_resource->Release();
  }

  ID3D12Resource* aabbs_buffer{};
  g_myframework->CreateBufferForCPUSideData(aabbs.data(),
    sizeof(aabbs[0]) * aabbs.size(), &aabbs_buffer);
  g_myframework->BuildDummyProcedural(
    &g_proc_blas_resource, &g_proc_tlas_resource,
    aabbs_buffer, 1
  );
  g_myframework->CreateSRVAccelerationStructure(g_proc_tlas_resource, g_proc_srv_uav_cbv_heap, 1);
  aabbs_buffer->Release();
}

void InitPerSceneCB() {
  g_proj = glm::perspectiveRH_ZO(
    glm::radians(60.0f),
    1.0f * VIEW_W / VIEW_H,
    0.01f,
    49999.0f
  );
}

int main()
{
  CreateLSSPlaygroundWindow();

  g_myframework = new MyFramework();
  g_myframework->InitDeviceAndCommandQ();
  g_myframework->InitNVAPI();
  g_myframework->SetHwnd(glfwGetWin32Window(g_window));
  g_myframework->InitSwapchain(WIN_W, WIN_H);
  g_myframework->InitImGUIForGLFW(g_window);

  InitPipeline();
  InitResources();
  InitSceneLSS();
  InitSceneProcedural();
  InitPerSceneCB();
  LoadTestCasesFromFile();

  while (!glfwWindowShouldClose(g_window))
  {
    Update();
    Render();
    if (g_should_update_as) {
      g_should_update_as = false;
      InitSceneLSS();
      InitSceneProcedural();
    }
    g_dirty = false;
    g_dir_dirty = false;
    glfwPollEvents();
  }
  delete g_myframework;
  return 0;
}