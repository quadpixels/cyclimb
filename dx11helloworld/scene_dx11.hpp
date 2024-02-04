#ifndef _SCENE_HPP
#define _SCENE_HPP

#include <d3d11.h>
#include <DirectXMath.h>
#include "camera.hpp"
#include "chunk.hpp"
#include "util.hpp"

class Scene {
public:
  virtual void Render() = 0;
  virtual void Update(float secs) = 0;
};

class DX11ClearScreenScene : public Scene {
public:
  void Render() override;
  void Update(float secs) override;
};

class DX11HelloTriangleScene : public Scene {
public:
  DX11HelloTriangleScene();
  struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
  };
  struct PerTriangleCB {
    DirectX::XMFLOAT2 pos;
  };
  void Render() override;
  void Update(float secs) override;

  ID3D11Buffer* vertex_buffer;
  ID3D11InputLayout* input_layout;
  ID3D11VertexShader* vs;
  ID3D11PixelShader* ps;
  ID3D11Buffer* per_triangle_cb;
  float elapsed_secs;
};

class DX11ChunksScene : public Scene {
public:
  struct DefaultPalettePerObjectCB {
    DirectX::XMMATRIX M, V, P;
  };
  DX11ChunksScene();
  void Render() override;
  void Update(float secs) override;

  Chunk* chunk;
  ID3D11DepthStencilView *dsv_main, *dsv_shadowmap;
  ID3D11VertexShader* vs;
  ID3D11PixelShader* ps;
  Camera* camera;
  ID3D11InputLayout* input_layout;
  ID3D11Buffer* backdrop_vb;
  DirectionalLight* dir_light;
  ID3D11ShaderResourceView* srv_shadowmap;
  ID3D11SamplerState* sampler;

  ID3D11Texture2D* gbuffer;
  ID3D11ShaderResourceView *srv_gbuffer;
  ID3D11RenderTargetView* rtv_gbuffer;
};

class DX11LightScatterScene : public Scene {
public:
  struct ConstantBufferDataDrawLight {
    float WIN_W, WIN_H;
    float light_x, light_y, light_r;
    DirectX::XMVECTOR light_color;
    float global_alpha;
  };
  struct VertexUV {
    float x, y, z, u, v;
  };

  DX11LightScatterScene();

  ID3D11VertexShader* vs_drawlight, * vs_mask, * vs_combine;
  ID3D11PixelShader* ps_drawlight, * ps_mask, * ps_combine;

  ID3D11Texture2D* lightmap;
  ID3D11ShaderResourceView* srv_lightmap;
  ID3D11RenderTargetView* rtv_lightmask;

  ID3D11Buffer* cb_drawlight;
  ConstantBufferDataDrawLight h_cb_drawlight;

  ID3D11Buffer* vb_fsquad;
  ID3D11InputLayout* input_layout;

  void Render() override;
  void Update(float secs) override;
};

#endif