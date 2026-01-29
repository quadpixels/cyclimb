#include "MyFramework.h"
#include "textrender1.hpp"

class MyLssScene : public MyScene {
public:
  struct LssSphere {
    glm::vec3 pos;
    float radius;
  };
  MyLssScene(MyFramework* f);
  void Render() override;
  void Update(float secs) override;
  void OnKeyDown(uint32_t k) override;
  void OnKeyUp(uint32_t k) override;

  ID3D12RootSignature* global_rootsig;
  ID3D12Resource* rt_output_resource;
  ID3D12DescriptorHeap* cbvsrvuav_heap;
  MyRtPipeline my_rt_pipeline{};
  ID3D12Resource* lss_pos_resource, * lss_pos_resource1;
  ID3D12Resource* lss_radii_resource;
  ID3D12Resource* lss_indices_list_resource, * lss_indices_successive_resource;
  ID3D12Resource* blas_result, * tlas_result;
  ID3D12Resource* my_debug_resource;
  ID3D12Resource* my_debug_resource_cpu;
  struct PerSceneCB {
    DirectX::XMMATRIX inverse_view;
    DirectX::XMMATRIX inverse_proj;
    int viz_mode;
    int cam_mode;  // 0 = perspective, 1 = orthogonal
  };
  ID3D12Resource* per_scene_cb;

  std::vector<glm::vec3> lss_poses;
  std::vector<float> lss_radii;
  std::vector<uint32_t> lss_indices;
  std::vector<uint32_t> lss_indices_successive;
  glm::vec3 cam_pos{ 0, 0, 2.0 };
  glm::vec3 cam_lookdir{ 0, 0, -1.0 };
  glm::vec3 cam_up{ 0, 1, 0 };
  glm::mat4 cam_view_matrix;
  char axes[3]{};
  char rot_axes[2]{};
  float cam_azimuth{};
  float cam_elevation{};
  int viz_mode{ 1 };
  int cam_mode{ 0 };
  int case_idx{ 0 };

  bool inited{ false };  // False if no nvapi support
};

class MyInstanceFlagScene : public MyScene {
public:
  struct PerSceneCB {
    uint32_t cull_flag;
  };
  PerSceneCB h_perscene_cb;
  MyInstanceFlagScene(MyFramework* f);
  void Render() override;
  void Update(float secs) override;
  void OnKeyDown(uint32_t k) override;
  void OnKeyUp(uint32_t k) override;

  ID3D12RootSignature* global_rootsig;
  ID3D12Resource* rt_output_resource;
  MyRtPipeline my_rt_pipeline{};
  ID3D12Resource* tri_verts_resource;
  ID3D12Resource* blas_result, *tlas_result;
  ID3D12Resource* perscene_cb;
  ID3D12DescriptorHeap* cbvsrvuav_heap, *cbvsrvuav_heap_cpu;
  std::vector<std::pair<std::wstring, glm::vec2>> labels;

  TextPass* text_pass;
};

class MyCullingScene : public MyScene {
public:
  struct PerSceneCB {
    uint32_t ray_flag;
  };
  uint32_t inst_flag{};
  MyCullingScene(MyFramework* f);
  void Render() override;
  void Update(float secs) override;
  void OnKeyDown(uint32_t k) override;
  void OnKeyUp(uint32_t k) override;
  void BuildOrRebuildAS();
  
  TextPass* text_pass;
  ID3D12RootSignature* global_rootsig;
  ID3D12Resource* rt_output_resource;
  MyRtPipeline my_rt_pipeline{};
  ID3D12Resource* blas_result_tri, *blas_result_proc, *blas_result_lss;
  ID3D12Resource* tlas_result;
  ID3D12Resource* perscene_cb;
  PerSceneCB h_perscene_cb{};
  ID3D12DescriptorHeap* cbvsrvuav_heap, * cbvsrvuav_heap_cpu;

  int choice_idx{ -1 };
  bool is_as_dirty{ false };
  bool has_nvapi{ true };
  void do_ChangeChoice(int delta);
  void do_ChangeOption(int delta);
};