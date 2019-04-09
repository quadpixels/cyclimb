#include "rendertarget.hpp"
#include "util.hpp"

void FBO::Bind() {
  glViewport(0, 0, width, height);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
}

void FBO::Unbind() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

MsaaFBO::MsaaFBO(const int _w, const int _h, const int samples) {
  width = _w; height = _h;

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);

  GLuint ms_tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
  glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples,
      GL_RGBA, width, height, GL_TRUE);

  GLuint db;
  glGenRenderbuffers(1, &db);
  glBindRenderbuffer(GL_RENDERBUFFER, db);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
      GL_DEPTH_COMPONENT, width, height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, db);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, tex, 0);

  GLenum rts[1] = { GL_COLOR_ATTACHMENT0 };
  glDrawBuffers(1, rts);

  MyCheckGLError("create ms fbo");

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    printf("Multisampled FBO not complete.\n");
    assert(0);
  }

  glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void MsaaFBO::BlitTo(BasicFBO* x) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, x->fbo);
  glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
      GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

BasicFBO::BasicFBO(const int _w, const int _h) {
  width = _w; height = _h;

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);

  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
      width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  glGenTextures(1, &depth_tex);
  glBindTexture(GL_TEXTURE_2D, depth_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
      width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

  // The following 2 lines are needed
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

//  GLuint db;
//  glGenRenderbuffers(1, &db);
//  glBindRenderbuffer(GL_RENDERBUFFER, db);
//  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
//  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, db);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_TEXTURE_2D, depth_tex, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

  GLenum rts[1] = { GL_COLOR_ATTACHMENT0 };
  glDrawBuffers(1, rts);

  MyCheckGLError("create singlesample fbo");

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    printf("Single-sampled FBO not complete.\n");
    assert(0);
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

DepthOnlyFBO::DepthOnlyFBO(const int _w, const int _h) {
  width = _w; height = _h;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
               width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex, 0);
  //glDrawBuffer(GL_NONE);
  //glReadBuffer(GL_NONE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  MyCheckGLError("create depth fbo");
}
