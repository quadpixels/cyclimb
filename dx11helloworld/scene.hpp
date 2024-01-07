#ifndef _SCENE_HPP
#define _SCENE_HPP

#include <d3d11.h>
#include <DirectXMath.h>

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
  struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
  };
  DX11HelloTriangleScene();
  void Render() override;
  void Update(float secs) override;

  ID3D11Buffer* vertex_buffer;
  ID3D11InputLayout* input_layout;
  ID3D11VertexShader* vs;
  ID3D11PixelShader* ps;
};

#endif