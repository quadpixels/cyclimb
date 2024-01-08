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
};

#endif