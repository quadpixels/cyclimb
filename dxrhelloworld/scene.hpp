#pragma once
class Scene {
public:
  virtual void Render() = 0;
  virtual void Update(float secs) = 0;
};

class TriangleScene : public Scene {

};