#ifndef _SCENE_HPP
#define _SCENE_HPP

class Scene {
public:
  virtual void Render() = 0;
  virtual void Update(float secs) = 0;
};

class DX11ClearScreenScene : public Scene {
  void Render() override;
  void Update(float secs) override;
};

#endif