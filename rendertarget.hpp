#ifndef _RENDERTARGET_HPP_
#define _RENDERTARGET_HPP_

#define GLM_FORCE_RADIANS
#include <gl/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class MsaaFBO;
class BasicFBO;
class DepthOnlyFBO;

class FBO {
public:
  GLuint fbo, tex, width, height;
  void Bind();
  void Unbind();
};

class MsaaFBO : public FBO {
public:
  MsaaFBO(const int _w, const int _h, const int samples);
  void BlitTo(BasicFBO* x);
};

class BasicFBO : public FBO {
public:
  BasicFBO(const int _w, const int _h);
  GLuint depth_tex;
};

class DepthOnlyFBO : public FBO {
public:
  DepthOnlyFBO(const int _w, const int _h);
};

#endif
