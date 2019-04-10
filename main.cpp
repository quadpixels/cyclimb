#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <GL/glew.h>
#include <GL/freeglut.h>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "shader.hpp"
#include "camera.hpp"
#include "testshapes.hpp"
#include "chunk.hpp"
#include "util.hpp"
#include "chunkindex.hpp"
#include "sprite.hpp"
#include <vector>
#include <algorithm>
#include "rendertarget.hpp"
#include "game.hpp"
#include "textrender.hpp"
#include "scene.hpp"
#include <bitset>

#ifdef WIN32
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

void StartGame();

int WIN_W = 1024, WIN_H = 600;
const int SHADOW_RES = 512;
int g_font_size = 24;

bool g_main_menu_visible = true;
unsigned g_fadein_complete_millis;

glm::mat4 g_projection(1.0f);
GLuint g_programs[7];

GLuint GetShaderProgram(int idx) {
  return g_programs[idx];
}

Camera g_cam;
unsigned g_last_millis = 0;
char g_cam_dx = 0,   g_cam_dy = 0,   g_cam_dz = 0,  // CONTROL axes, not OpenGL axes
     g_arrow_dx = 0, g_arrow_dy = 0, g_arrow_dz = 0;
std::bitset<18> g_cam_flags;

// These are all scaffolds
Triangle *g_triangle[2];
ColorCube *g_colorcube[2];
Chunk* ALLNULLS[26] = { NULL };
ChunkGrid* g_chunkgrid[4];
std::vector<Sprite*> g_projectiles;
Particles*  g_particles;

// globally shared
Particles* GetGlobalParticles() { return g_particles; }

TestShapesScene*   g_testscene    = NULL;
ClimbScene*        g_climbscene   = NULL;

int scene_idx = 1;
GameScene* GetCurrentGameScene() {
  GameScene* scenes[] = { g_testscene, g_climbscene };
  return scenes[scene_idx];
}
Camera* GetCurrentSceneCamera() {
  return GetCurrentGameScene()->camera;
}

// Render targets.
// Follow this tutorial: https://learnopengl.com/Advanced-OpenGL/Anti-Aliasing
MsaaFBO* g_msaa_fbo, *g_slateui_msaa_fbo;
BasicFBO* g_basic_fbo, *g_slateui_basic_fbo;
DepthOnlyFBO* g_depth_fbo;
FullScreenQuad* g_fullscreen_quad;
DirectionalLight* g_dir_light;

MainMenu* g_mainmenu;
TextMessage* g_textmessage;

// UI camera shake/roll
float g_cam_rot_x = 0.0f, g_cam_rot_y = 0.0f;

int g_ctrl_spr_idx = 0;
bool g_aa = true, g_shadows = true;

unsigned g_test_tri_vao;
unsigned g_test_quad_vao;

void MyInit() {
  {
    float vertices[] = {
      -0.9f,  0.8f, -0.5f,
      -0.8f,  0.9f, -0.5f,
      -0.9f,  0.9f, -0.5f,
    };
    glGenVertexArrays(1, &g_test_tri_vao);
    glBindVertexArray(g_test_tri_vao);
    {
      unsigned vbo;
      glGenBuffers(1, &vbo);

      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

      const size_t stride = 3 * sizeof(GLfloat);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
      glEnableVertexAttribArray(0);
    }
    glBindVertexArray(0);
  }

  // Test screen-space pixels
  {
    const float x0 = 16.0f, x1 = 32.0f;
    float vertices[] = {
      x0, x0, 0.0f, 1.0f,
      x0, x1, 0.0f, 0.0f,
      x1, x0, 1.0f, 1.0f,

      x0, x1, 0.0f, 0.0f,
      x1, x1, 1.0f, 0.0f,
      x1, x0, 1.0f, 1.0f,
    };
    glGenVertexArrays(1, &g_test_quad_vao);
    glBindVertexArray(g_test_quad_vao);
    {
      unsigned vbo;
      glGenBuffers(1, &vbo);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
      const size_t stride = 4*sizeof(float);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)0);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(2*sizeof(float)));
      glEnableVertexAttribArray(1);
    }
    glBindVertexArray(0);
    MyCheckGLError("test_quad_vao");
  }
  g_programs[0] = CreateProgram("shaders/default_palette.vs",   "shaders/default_palette.fs");
  g_programs[1] = CreateProgram("shaders/vert_norm_data_ao.vs", "shaders/vert_norm_data_ao.fs");
  g_programs[2] = CreateProgram("shaders/simple_texture.vs",    "shaders/simple_texture.fs");
  g_programs[3] = CreateProgram("shaders/simple_texture.vs",    "shaders/show_depth.fs");
  g_programs[4] = CreateProgram("shaders/simple_depth.vs",      "shaders/nop.fs");
  g_programs[5] = CreateProgram("shaders/passthrough.vs",       "shaders/red.fs");
  g_programs[6] = CreateProgram("shaders/textrender.vs",        "shaders/textrender.fs");
  g_projection = glm::perspective(60.0f*3.14159f/180.0f, WIN_W*1.0f/WIN_H, 0.1f, 499.0f);
  //g_projection = glm::ortho(-100.f, 100.f, -60.f, 60.f, -10.f, 499.f);
  g_dir_light = new DirectionalLight(glm::vec3(-1, -3, -1), glm::vec3(1,3,-1));

  g_msaa_fbo  = new MsaaFBO(WIN_W, WIN_H, 4);
  g_slateui_msaa_fbo = new MsaaFBO(WIN_W, WIN_H, 4);
  g_basic_fbo = new BasicFBO(WIN_W, WIN_H);
  g_slateui_basic_fbo = new BasicFBO(WIN_W, WIN_H);
  g_depth_fbo = new DepthOnlyFBO(SHADOW_RES, SHADOW_RES);

  Triangle::Init(g_programs[0]);
  ColorCube::Init(g_programs[0]);
  MainMenu::InitStatic(g_programs[6]);
  Chunk::program = g_programs[1];
  FullScreenQuad::Init(g_programs[2], g_programs[3]);
  TextMessage::InitStatic(g_programs[6]);

  g_particles = new Particles();

  g_fullscreen_quad = new FullScreenQuad();

  g_triangle[0] = new Triangle();
  g_triangle[0]->pos = glm::vec3(-0.1,0,-0.1);
  g_triangle[1] = new Triangle();
  g_triangle[1]->pos = glm::vec3(0.1,0,0);
  g_colorcube[0] = new ColorCube();
  g_colorcube[0]->pos = glm::vec3(0, 0, 0);

  g_chunkgrid[3] = new ChunkGrid(1,1,1);
  g_chunkgrid[3]->SetVoxel(0,0,0,12);

  Particles::InitStatic(g_chunkgrid[3]);
  
  InitTextRender();

  g_mainmenu = new MainMenu();
  g_textmessage = new TextMessage();

  ChunkSprite* test_sprite = new ChunkSprite(new ChunkGrid(
    "climb/coords.vox"
  )), *global_xyz = new ChunkSprite(new ChunkGrid(
    "climb/xyz.vox"
  ));

  test_sprite->pos = glm::vec3(0, -4, 0);
  test_sprite->anchor = glm::vec3(0.5f, 0.5f, 0.5f);
  test_sprite->scale = glm::vec3(3,3,3);

  global_xyz->pos = glm::vec3(-40, -42, -24);
  global_xyz->scale = glm::vec3(2,2,2);
  global_xyz->anchor = glm::vec3(0.5f, 0.5f, 0.5f);

  g_testscene = new TestShapesScene();
  g_testscene->test_sprite = test_sprite;
  g_testscene->global_xyz = global_xyz;
  
  ClimbScene::InitStatic();
  g_climbscene = new ClimbScene();
  g_climbscene->Init();
}

void IssueDrawCalls() {
  GameScene* scene = GetCurrentGameScene();
  if (scene) {
    std::vector<Sprite*>* sprites = GetCurrentGameScene()->GetSpriteListForRender();
    for (Sprite* s : *sprites) {
      if (s) s->Render();
    }
    for (Sprite* s : g_projectiles) s->Render();
  }
}

void RenderScene(const glm::mat4& V, const glm::mat4& P) {
  // Clear
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glClearColor(1.0f, 1.0f, 0.8f, 0.0f);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_MULTISAMPLE_ARB);
  glCullFace(GL_BACK);

  // Set V and P to all shaders
  for (unsigned i=0; i<sizeof(g_programs)/sizeof(g_programs[0]); i++) {
    glUseProgram(g_programs[i]);
    GLuint vLoc = glGetUniformLocation(g_programs[i], "V");
    glUniformMatrix4fv(vLoc, 1, GL_FALSE, &V[0][0]);
    GLuint pLoc = glGetUniformLocation(g_programs[i], "P");
    glUniformMatrix4fv(pLoc, 1, GL_FALSE, &P[0][0]);
  }
  MyCheckGLError("set uniforms");

  // Set Directional light
  glUseProgram(g_programs[1]);
  GLuint dlLoc = glGetUniformLocation(g_programs[1], "dir_light");
  glUniform3f(dlLoc, g_dir_light->dir.x,
                     g_dir_light->dir.y,
                     g_dir_light->dir.z);
  glUseProgram(0);

  IssueDrawCalls();

  MyCheckGLError("Render Scene");
}

void RenderSceneWithShadow(
    const glm::mat4& V, const glm::mat4& P,
    const glm::mat4& lightPV,
    GLuint shadow_map) {

  // Clear
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glClearColor(0.8f, 1.0f, 1.0f, 0.0f);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_MULTISAMPLE_ARB);
  glCullFace(GL_BACK);

  // Bind shadow texture
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, shadow_map);

  // Set V and P to all shaders
  for (unsigned i=0; i<sizeof(g_programs)/sizeof(g_programs[0]); i++) {
    glUseProgram(g_programs[i]);
    GLint vLoc = glGetUniformLocation(g_programs[i], "V");
    glUniformMatrix4fv(vLoc, 1, GL_FALSE, &V[0][0]);
    GLint pLoc = glGetUniformLocation(g_programs[i], "P");
    glUniformMatrix4fv(pLoc, 1, GL_FALSE, &P[0][0]);
    GLint lpvLoc = glGetUniformLocation(g_programs[i], "lightPV");
    if (lpvLoc != -1) {
      glUniformMatrix4fv(lpvLoc, 1, GL_FALSE, &lightPV[0][0]);
    }
    GLint shadowmapLoc = glGetUniformLocation(g_programs[i], "shadow_map");
    if (shadowmapLoc != -1) {
      glUniform1i(shadowmapLoc, 0);
    }
  }

  // Set Directional light
  glUseProgram(g_programs[1]);
  GLuint dlLoc = glGetUniformLocation(g_programs[1], "dir_light");
  glUniform3f(dlLoc, g_dir_light->dir.x,
                     g_dir_light->dir.y,
                     g_dir_light->dir.z);
  glUseProgram(0);

  IssueDrawCalls();

  MyCheckGLError("RenderSceneWithShadow");

  glBindTexture(GL_TEXTURE_2D, 0);
}

void render() {
  Camera* cam = GetCurrentSceneCamera();
  // 0: Prepare
  GetCurrentGameScene()->PreRender();
  GetCurrentGameScene()->PrepareSpriteListForRender();

  // 1: DEPTH PASS
  g_depth_fbo->Bind();
  if (g_shadows) {
    RenderScene(g_dir_light->V, g_dir_light->P);
  } else {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearColor(.0f, .0f, .0f, .0f);
  }
  g_depth_fbo->Unbind();

  // 2: Main Pass, with shadows applied
  if (g_aa) {
    g_msaa_fbo->Bind();
    RenderSceneWithShadow(cam->GetViewMatrix(),
        g_projection,
        g_dir_light->P * g_dir_light->V,
        g_depth_fbo->tex);
    g_msaa_fbo->Unbind();

    // 2. Copy to Singlesample FBO
    g_msaa_fbo->BlitTo(g_basic_fbo);
  } else {
    g_basic_fbo->Bind();
    RenderSceneWithShadow(cam->GetViewMatrix(),
            g_projection,
            g_dir_light->P * g_dir_light->V,
            g_depth_fbo->tex);
    g_basic_fbo->Unbind();
  }

  // 3. Draw the rendered texture


  // 4. Draw UI
  g_slateui_msaa_fbo->Bind();

  glViewport(0, 0, WIN_W, WIN_H);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glClearColor(.0f, .0f, .0f, .0f);

  // FOR DEBUGGING TEXT RENDER
  glm::mat4 uitransform(1);
  uitransform *= glm::rotate(uitransform, g_cam_rot_x, glm::vec3(0.0f,1.0f,0.0f));
  uitransform *= glm::rotate(uitransform, g_cam_rot_y, glm::vec3(1.0f,0.0f,0.0f));

  GameScene* scene = GetCurrentGameScene();
  if (scene) scene->RenderHUD();

  {
    if (g_main_menu_visible) {
      g_mainmenu->Render(uitransform);
    }
  }

  g_slateui_msaa_fbo->Unbind();
  g_slateui_msaa_fbo->BlitTo(g_slateui_basic_fbo);

  glViewport(0, 0, WIN_W, WIN_H);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  // Show depth g_fullscreen_quad->RenderDepth(g_depth_fbo->tex);
  g_fullscreen_quad->Render(g_basic_fbo->tex);

  g_fullscreen_quad->RenderWithBlend(g_slateui_basic_fbo->tex);

  glutSwapBuffers();

  GetCurrentGameScene()->PostRender();
}

void update() {
  unsigned elapsed = GetElapsedMillis();
  float secs = (elapsed - g_last_millis) * 0.001f;

  {
    const float X = 0.002f, DECAY = 0.95f;
    g_cam_rot_x -= X * g_arrow_dx; // DEGREES
    g_cam_rot_y += X * g_arrow_dy;
    g_cam_rot_x *= DECAY;
    g_cam_rot_y *= DECAY;
  }

  Camera* cam = GetCurrentSceneCamera();
  
  // 每帧更新的东西
  cam->vel = glm::vec3(0, 0,-60) * float(g_cam_dy) +
        glm::vec3(60,0, 0 ) * float(g_cam_dx) +
        glm::vec3(0, 60,0) * float(g_cam_dz);
  cam->Update(secs);

  GetCurrentGameScene()->Update(secs);

  // 这是当时为了解答以下知乎问题而做的演示场景而准备的
  // https://www.zhihu.com/question/313691919/answer/619138757
  const char* msgs[] = {
    "绕局部X轴 逆时针旋转",
    "绕局部X轴 顺时针旋转",
    "绕局部Y轴 逆时针旋转",
    "绕局部Y轴 顺时针旋转",
    "绕局部Z轴 逆时针旋转",
    "绕局部Z轴 顺时针旋转",
    "绕世界X轴 逆时针旋转",
    "绕世界X轴 顺时针旋转",
    "绕世界Y轴 逆时针旋转",
    "绕世界Y轴 顺时针旋转",
    "绕世界Z轴 逆时针旋转",
    "绕世界Z轴 顺时针旋转",
    "摄像机绕自身局部X轴逆时针旋转 同时保持面向物体",
    "摄像机绕自身局部X轴顺时针旋转 同时保持面向物体",
    "摄像机绕自身局部Y轴逆时针旋转 同时保持面向物体",
    "摄像机绕自身局部Y轴顺时针旋转 同时保持面向物体",
    "摄像机绕自身局部Z轴逆时针旋转 同时保持面向物体",
    "摄像机绕自身局部Z轴顺时针旋转 同时保持面向物体",
  };
  std::string msg;
  static std::string msg_prev;
  for (int i=0; i<18; i++) {
    ChunkSprite* test_sprite = (ChunkSprite*)(g_testscene->test_sprite);
    if (g_cam_flags.test(i)) {
      switch (i) {
        case 0: test_sprite->RotateAroundLocalAxis(glm::vec3( 1, 0, 0), 2.0f); break;
        case 1: test_sprite->RotateAroundLocalAxis(glm::vec3(-1, 0, 0), 2.0f); break;
        case 2: test_sprite->RotateAroundLocalAxis(glm::vec3( 0, 1, 0), 2.0f); break;
        case 3: test_sprite->RotateAroundLocalAxis(glm::vec3( 0,-1, 0), 2.0f); break;
        case 4: test_sprite->RotateAroundLocalAxis(glm::vec3( 0, 0, 1), 2.0f); break;
        case 5: test_sprite->RotateAroundLocalAxis(glm::vec3( 0, 0,-1), 2.0f); break;

        case 6:  test_sprite->RotateAroundGlobalAxis(glm::vec3( 1, 0, 0), 2.0f); break;
        case 7:  test_sprite->RotateAroundGlobalAxis(glm::vec3(-1, 0, 0), 2.0f); break;
        case 8:  test_sprite->RotateAroundGlobalAxis(glm::vec3( 0, 1, 0), 2.0f); break;
        case 9:  test_sprite->RotateAroundGlobalAxis(glm::vec3( 0,-1, 0), 2.0f); break;
        case 10: test_sprite->RotateAroundGlobalAxis(glm::vec3( 0, 0, 1), 2.0f); break;
        case 11: test_sprite->RotateAroundGlobalAxis(glm::vec3( 0, 0,-1), 2.0f); break;

        case 12: cam->RotateAlongPoint(g_testscene->test_sprite->pos, glm::vec3(1,0,0),  2.0f/57.5f); break;
        case 13: cam->RotateAlongPoint(g_testscene->test_sprite->pos, glm::vec3(1,0,0), -2.0f/57.5f); break;
        case 14: cam->RotateAlongPoint(g_testscene->test_sprite->pos, glm::vec3(0,1,0), -2.0f/57.5f); break;
        case 15: cam->RotateAlongPoint(g_testscene->test_sprite->pos, glm::vec3(0,1,0),  2.0f/57.5f); break;
        case 16: cam->RotateAlongPoint(g_testscene->test_sprite->pos, glm::vec3(0,0,1), -2.0f/57.5f); break;
        case 17: cam->RotateAlongPoint(g_testscene->test_sprite->pos, glm::vec3(0,0,1),  2.0f/57.5f); break;

        default: break;
      }
      msg = std::string(msgs[i]);
    }
  }
  if (msg.size() > 0 && msg_prev != msg) {
    printf("%s\n", msg.c_str());
    msg_prev = msg;
  }

  g_last_millis = elapsed;
  glutPostRedisplay();
}

void keydown(unsigned char key, int x, int y) {
  if (GetCurrentGameScene() != g_climbscene) {
    switch (key) {
      case 't': g_cam_flags.set(0); break;
      case 'g': g_cam_flags.set(1); break;
      case 'f': g_cam_flags.set(2); break;
      case 'h': g_cam_flags.set(3); break;
      case 'r': g_cam_flags.set(4); break;
      case 'y': g_cam_flags.set(5); break;
      case 'i': g_cam_flags.set(12); break;
      case 'k': g_cam_flags.set(13); break;
      case 'j': g_cam_flags.set(14); break;
      case 'l': g_cam_flags.set(15); break;
      case 'u': g_cam_flags.set(16); break;
      case 'o': g_cam_flags.set(17); break;
        
      case 'w': g_cam_dy =  1; break;
      case 's': g_cam_dy = -1; break;
      case 'a': g_cam_dx = -1; break;
      case 'd': g_cam_dx =  1; break;
      case 'q': g_cam_dz = -1; break;
      case 'e': g_cam_dz =  1; break;
    }
  }
  switch (key) {
    case 27: {
      if (g_main_menu_visible) {
        g_mainmenu->ExitMenu();
        g_main_menu_visible = false;
      } else {
        g_mainmenu->EnterMenu(0);
        g_main_menu_visible = true;
      }
      break;
    }
    case 13: {
      switch (g_main_menu_visible) {
        case true:
          g_mainmenu->OnEnter();
          break;
        default: break;
      }
      break;
    }

    case '[': g_testscene->global_xyz->scale *= glm::vec3(0.99f, 0.99f, 0.99f); break;
    case ']': g_testscene->global_xyz->scale *= glm::vec3(1/0.99f, 1/0.99f, 1/0.99f); break;

    case '`': scene_idx = (scene_idx + 1) % 2; break;
    default: GetCurrentGameScene()->OnKeyPressed(char(key));
  }
}

void keyup(unsigned char key, int x, int y) {
  if (GetCurrentGameScene() != g_climbscene) {
    switch (key) {
      case 'w': case 's': g_cam_dy = 0; break;
      case 'a': case 'd': g_cam_dx = 0; break;
      case 'q': case 'e': g_cam_dz = 0; break;
      case 't': g_cam_flags.reset(0); break;
      case 'g': g_cam_flags.reset(1); break;
      case 'f': g_cam_flags.reset(2); break;
      case 'h': g_cam_flags.reset(3); break;
      case 'r': g_cam_flags.reset(4); break;
      case 'y': g_cam_flags.reset(5); break;
      case 'i': g_cam_flags.reset(12); break;
      case 'k': g_cam_flags.reset(13); break;
      case 'j': g_cam_flags.reset(14); break;
      case 'l': g_cam_flags.reset(15); break;
      case 'u': g_cam_flags.reset(16); break;
      case 'o': g_cam_flags.reset(17); break;
    }
  }
  else GetCurrentGameScene()->OnKeyReleased(char(key));
}

void keydown2(int key, int x, int y) {
  switch (key) {
    case GLUT_KEY_UP:    g_arrow_dy =  1; break;
    case GLUT_KEY_DOWN:  g_arrow_dy = -1; break;
    case GLUT_KEY_RIGHT: g_arrow_dx =  1; break;
    case GLUT_KEY_LEFT:  g_arrow_dx = -1; break;
    default: break;
  }
  if (g_main_menu_visible) {
    switch (key) {
      case GLUT_KEY_UP:    g_mainmenu->OnUpDownPressed(-1); break;
      case GLUT_KEY_DOWN:  g_mainmenu->OnUpDownPressed( 1); break;
      case GLUT_KEY_LEFT:  g_mainmenu->OnLeftRightPressed(-1); break;
      case GLUT_KEY_RIGHT: g_mainmenu->OnLeftRightPressed( 1); break;
      default: break;
    }
  }
}

void keyup2(int key, int x, int y) {
  switch (key) {
    case GLUT_KEY_UP: case GLUT_KEY_DOWN:    g_arrow_dy = 0; break;
    case GLUT_KEY_LEFT: case GLUT_KEY_RIGHT: g_arrow_dx = 0; break;
    default: break;
  }
}

int main(int argc, char** argv) {
  // OpenGL 3.3 context
  glewExperimental = GL_TRUE;
  glutInit(&argc, argv);
  glutInitContextVersion(3, 3);
  glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH | GLUT_MULTISAMPLE);
  glutInitWindowSize(WIN_W, WIN_H);

  glutCreateWindow("ChaoyueClimb");
  glutDisplayFunc(&render);
  glutKeyboardFunc(&keydown);
  glutKeyboardUpFunc(&keyup);
  glutSpecialFunc(&keydown2);
  glutSpecialUpFunc(&keyup2);
  glutIdleFunc(&update);
  glutSetKeyRepeat(false);

  if(glewInit() == GLEW_OK) {

    // Ignore errors in GLEW
    while (glGetError() != GL_NO_ERROR) { }

    if (glewIsSupported("GL_VERSION_3_3")) {
      printf("Ready for OpenGL 3.3\n");
      const char* sz = (const char*)glGetString(GL_VERSION);
      printf("OpenGL Version String: %s\n", sz);
      const char* sz1 = (const char*)glGetString(GL_VENDOR);
      printf("OpenGL Vendor String: %s\n", sz1);
      const char* sz2 = (const char*)glGetString(GL_RENDERER);
      printf("OpenGL Renderer String: %s\n", sz2);
      const char* sz3 = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
      printf("OpenGL Shading Language Version: %s\n", sz3);
    } else {
      printf("OpenGL 3.3 not supported\n");
      exit(1);
    }

    int muc;
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &muc);
    printf("GL_MAX_VERTEX_UNIFORM_COMPONENTS=%u\n", muc);
    MyInit();

    glutMainLoop();
  }

  return 0;
}

void StartGame() {
  g_main_menu_visible = false;
}
