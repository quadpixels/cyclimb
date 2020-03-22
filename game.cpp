#include <windows.h>

#include "game.hpp"
#include "chunkindex.hpp"
#include <algorithm>
#include <stdio.h>
#include "textrender.hpp"
#include <math.h>
#include "sprite.hpp"
#include "scene.hpp"
#include <stddef.h>
#include <DirectXMath.h>

extern bool g_aa, g_shadows;
extern int WIN_W, WIN_H;
extern TextMessage* g_textmessage;
extern bool g_main_menu_visible;
extern unsigned g_fadein_complete_millis;
extern unsigned g_last_millis;
extern ChunkGrid* g_chunkgrid[4];
extern std::vector<Sprite*> g_projectiles;
extern int g_font_size;
extern GLFWwindow* g_window;
extern void UpdateSimpleTexturePerSceneCB(const float x, const float y, const float alpha);
extern ID3D11DeviceContext* g_context11;
extern HWND g_hwnd;
extern ID3D11RenderTargetView *g_backbuffer_rtv11, *g_gbuffer_rtv11;
extern ID3D11DepthStencilView *g_dsv11;
extern D3D11_RECT g_scissorrect11;
extern ID3D11InputLayout* g_inputlayout_voxel11;
extern ID3D11VertexShader* g_vs_default_palette;
extern ID3D11PixelShader* g_ps_default_palette;
extern ID3D11SamplerState* g_sampler11;
extern ID3D11Buffer* g_perobject_cb_default_palette;
extern ID3D11Buffer* g_perscene_cb_default_palette;
extern DirectionalLight* g_dir_light;
extern DirectX::XMMATRIX g_projection_helpinfo_d3d11;

extern void UpdatePerSceneCB(const DirectX::XMVECTOR* dir_light, const DirectX::XMMATRIX* lightPV, const DirectX::XMVECTOR* camPos);
extern void UpdateGlobalPerObjectCB(const DirectX::XMMATRIX* M, const DirectX::XMMATRIX* V, const DirectX::XMMATRIX* P);

void StartGame();
Particles* GetGlobalParticles();

//======================== Particles =======

Particles::Particles() { }

ChunkIndex* Particles::default_particle = NULL;

void Particles::InitStatic(ChunkIndex* x) {
  default_particle = x;
}

void Particles::Spawn(ChunkIndex* src, const glm::vec3& pos, float lifetime, float v0) {
  float randx = rand() * 1.0f / RAND_MAX, randy = rand() * 1.0f / RAND_MAX;
  float cos_phi = cos(2.0 * 3.14159 * randx),
        sin_phi = sin(2.0 * 3.14159 * randx),
        cos_theta = 1.0f - randy,
        sin_theta = sqrt(1.0f - cos_theta * cos_theta);
  if (rand() * 1.0f / RAND_MAX > 0.5f) cos_theta = -cos_theta;
  glm::vec3 dir(
    sin_theta * cos_phi,
    sin_theta * sin_phi,
    cos_theta
  );

  State s;
  s.lifetime = s.lifetime_full = lifetime;
  s.sprite = new ChunkSprite(src);
  s.sprite->vel = dir * v0;
  s.sprite->pos = pos;
  particles.push_back(s);
}

void Particles::SpawnDefaultSprite(const glm::vec3& pos, float lifetime, float v0) {
  Spawn(default_particle, pos, lifetime, v0);
}

void Particles::Update(float secs) {
  for (State& s : particles) {
    float completion = 1.0f - (s.lifetime / s.lifetime_full);
    glm::vec3 vv = s.sprite->vel * (1.0f - completion) * (1.0f - completion);
    s.sprite->pos += vv;
    s.lifetime -= secs;
    if (s.lifetime < 0) {
      s.lifetime = 0;
      delete s.sprite;
    }
  }
  std::vector<State> next_state;
  for (State& s : particles) {
    if (s.lifetime > 0) next_state.push_back(s);
  }
  particles = next_state;
}

unsigned MainMenu::program = 0;

MainMenu::MainMenu() {
  is_in_help = false;
  fade_alpha0 = fade_alpha1 = 0;
  fade_millis0 = fade_millis1 = 0;
  fsquad = new FullScreenQuad(g_helpinfo_srv11);

  sprites_helpinfo.push_back(new ChunkSprite(ClimbScene::model_char));
  sprites_helpinfo.push_back(new ChunkSprite(ClimbScene::model_coin));
  
  const glm::vec3 X(1, 0, 0);

  Sprite* p0 = new ChunkSprite(ClimbScene::model_platforms[0]);
  p0->scale = glm::vec3(0.5, 0.5, 0.5);
  p0->RotateAroundLocalAxis(X, -15);
  sprites_helpinfo.push_back(p0);

  Sprite* p1 = new ChunkSprite(ClimbScene::model_platforms[1]);
  p1->scale = glm::vec3(0.5, 0.5, 0.5);
  p1->RotateAroundLocalAxis(X, -15);
  sprites_helpinfo.push_back(p1);

  Sprite* p2 = new ChunkSprite(ClimbScene::model_exit);
  p2->scale = glm::vec3(0.5, 0.5, 0.5);
  p2->RotateAroundLocalAxis(X, -15);
  sprites_helpinfo.push_back(p2);

  cam_helpinfo = new Camera();
  cam_helpinfo->InitForHelpInfo();
  
  const int W0 = 1280, H0 = 720; // Normalize to this
  int viewports[][4] = {
    { 264, 0, 192, 192 },
    { 1050, 454, 256, 256 },
    { 915, -56, 320, 320 },
    { 915, 61, 320, 320 },
    { 915, 170, 320, 320 },
  };
  for (int i = 0; i < sizeof(viewports) / sizeof(viewports[0]); i++) {
    D3D11_VIEWPORT vp;
    vp.TopLeftX = int(viewports[i][0] * 1.0f / W0 * WIN_W);
    vp.TopLeftY = int(viewports[i][1] * 1.0f / H0 * WIN_H);
    vp.Width = int(viewports[i][2] * 1.0f / W0 * WIN_W);
    vp.Height = int(viewports[i][3] * 1.0f / H0 * WIN_H);
    vp.MinDepth = 0;
    vp.MaxDepth = 1;
    viewports11_helpinfo.push_back(vp);
  }
  EnterMenu(0);
}

/**
 * Prerequisite: bind the overlay Render Target
 */
void MainMenu::Render(const glm::mat4& uitransform) {
  const int ymin = 200, textsize = 24;
  int y = ymin;

  // Titles
  for (int i=0; i<int(menutitle.size()); i++) {
    std::wstring line = menutitle.at(i);
    float w;
    MeasureTextWidth(line, &w);
    glm::vec3 c = glm::vec3(1.0f, 1.0f, 0.5f);
    RenderText(ClimbOpenGL, line, WIN_W/2 - w/2, y, 1.0f, c, uitransform);
    y += textsize;
  }

  y += textsize;

  // Menu items
  for (int i=0; i<int(menuitems.size()); i++) {
    MenuItem* itm = &(menuitems[i]);
    std::wstring line = itm->GetTextForDisplay();

    float w;
    MeasureTextWidth(line, &w);
    glm::vec3 c;
    if (i == curr_selection.back()) c = glm::vec3(1.0f, 0.2f, 0.2f);
    else c = glm::vec3(1.0f, 1.0f, 0.1f);
    RenderText(ClimbOpenGL, line, WIN_W/2 - w/2, y, 1.0f, c, uitransform);
    y += textsize;
  }
}

void MainMenu::Render_D3D11(const glm::mat4& uitransform) {
  // Copy & paste from Render()
  const int ymin = 200, textsize = 24;
  int y = ymin;

  // Titles
  for (int i = 0; i<int(menutitle.size()); i++) {
    std::wstring line = menutitle.at(i);
    float w;
    MeasureTextWidth(line, &w);
    glm::vec3 c = glm::vec3(1.0f, 1.0f, 0.5f);
    RenderText(ClimbD3D11, line, WIN_W / 2 - w / 2, y, 1.0f, c, uitransform);
    y += textsize;
  }

  y += textsize;

  // Menu items
  for (int i = 0; i<int(menuitems.size()); i++) {
    MenuItem* itm = &(menuitems[i]);
    std::wstring line = itm->GetTextForDisplay();

    float w;
    MeasureTextWidth(line, &w);
    glm::vec3 c;
    if (i == curr_selection.back()) c = glm::vec3(1.0f, 0.2f, 0.2f);
    else c = glm::vec3(1.0f, 1.0f, 0.1f);
    RenderText(ClimbD3D11, line, WIN_W / 2 - w / 2, y, 1.0f, c, uitransform);
    y += textsize;
  }

  // Background
  UpdateSimpleTexturePerSceneCB(0, 0, fade_alpha);
  fsquad->Render_D3D11();
  
  if (is_in_help) {
    // Directly render to backbuffer
    // Normal Pass
    ID3D11RenderTargetView* rtvs[] = { g_backbuffer_rtv11, g_gbuffer_rtv11 };
    g_context11->OMSetRenderTargets(2, rtvs, g_dsv11);
    g_context11->RSSetScissorRects(1, &g_scissorrect11);
    g_context11->VSSetShader(g_vs_default_palette, nullptr, 0);
    g_context11->PSSetShader(g_ps_default_palette, nullptr, 0);
    g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context11->IASetInputLayout(g_inputlayout_voxel11);
    g_context11->PSSetSamplers(0, 1, &g_sampler11);

    // The following is vevvy similar 2 Normal Pass
    DirectX::XMMATRIX V = cam_helpinfo->GetViewMatrix_D3D11(), P = g_projection_helpinfo_d3d11;
    ID3D11Buffer* cbs[] = { g_perobject_cb_default_palette, g_perscene_cb_default_palette };
    UpdatePerSceneCB(&(g_dir_light->GetDir_D3D11()), &(g_dir_light->GetPV_D3D11()), &(cam_helpinfo->GetPos_D3D11()));
    g_context11->VSSetConstantBuffers(0, 2, cbs);
    g_context11->PSSetConstantBuffers(1, 1, &g_perscene_cb_default_palette);

    // Prepare V and P 
    bool is_testing_dir_light = false;
    UpdateGlobalPerObjectCB(nullptr, &V, &P);

    // Perpare dir_light
    UpdatePerSceneCB(&g_dir_light->GetDir_D3D11(), &(g_dir_light->GetPV_D3D11()), &(cam_helpinfo->GetPos_D3D11()));

    g_context11->ClearDepthStencilView(g_dsv11, D3D11_CLEAR_DEPTH, 1.0f, 0);
    // Enable DS state b/c drawing text disables DS state
    g_context11->OMSetDepthStencilState(nullptr, 0);

    // Each sprite has its own viewport
    for (int i = 0; i<int(sprites_helpinfo.size()); i++) {
      g_context11->RSSetViewports(1, &(viewports11_helpinfo[i]));
      sprites_helpinfo[i]->Render_D3D11();
    }
  }
}

void MainMenu::OnUpDownPressed(int delta) {
  const int L = int(menuitems.size());
  if (menuitems.empty() == false && curr_selection.empty() == false) {
    int* pCh = &(curr_selection[curr_menu.size()-1]);
    int new_ch = int(delta + (*pCh) + L) % L;
    *pCh = new_ch;
  }
}

void MainMenu::EnterMenu(int idx) {
  menutitle.clear();
  menuitems.clear();
  const wchar_t* title0 = L"C Y Climb",
               * title1 = L"20190402";

  if (idx == 0) {
    menutitle.push_back(title0);
    menutitle.push_back(title1);
    menutitle.push_back(L"Main Menu");

    menuitems.push_back(MenuItem(L"Start Game"));
    menuitems.push_back(MenuItem(L"Options"));
    menuitems.push_back(MenuItem(L"Help"));
    menuitems.push_back(MenuItem(L"Exit"));
  } else if (idx == 1) {
    menutitle.push_back(title0);
    menutitle.push_back(title1);
    menutitle.push_back(L"Main Menu");
    FadeInHelpScreen();
    menuitems.push_back(MenuItem(L" "));
  } else if (idx == 2) {
    menutitle.push_back(title0);
    menutitle.push_back(title1);

    menuitems.push_back(GetMenuItem("antialias"));
    menuitems.push_back(GetMenuItem("shadows"));
    menuitems.push_back(MenuItem(L"TEST"));
  }

  curr_menu.push_back(idx);
  if (curr_selection.size() < curr_menu.size())
    curr_selection.push_back(0);
}

void MainMenu::OnEnter() {
  switch (curr_menu.back()) {
    case 0: {
      switch (curr_selection[curr_menu.size()-1]) {
      case 0:
        StartGame(); // Start Game
        break;
      case 1: EnterMenu(2); break; // Options
      case 2: EnterMenu(1); break; // Help
      case 3: {
        // close glfw window
        glfwSetWindowShouldClose(g_window, true); 
        glfwDestroyWindow(g_window);
        // close D3D window
        DestroyWindow(g_hwnd);
        break; // Exit
      }
      default: break;
      }
      break;
    }
    case 1: {
      FadeOutHelpScreen();
      ExitMenu();
      break;
    }
    case 2: {
      switch (curr_selection[curr_menu.size()-1]) {
      case 2:
        printf("TEST MENU\n");
        g_main_menu_visible = false;
        break;
      }
      break;
    }
    default: break;
  }
}

void MainMenu::ExitMenu() {
  curr_menu.pop_back();
  if (curr_menu.empty() == false) {
    EnterMenu(curr_menu.back());
  } else {
    if (curr_menu.size() < 1) {
      g_main_menu_visible = false;
    }
  }
}

MainMenu::MenuItem MainMenu::GetMenuItem(const char* which) {
  MenuItem ret(L"");
  if (!strcmp(which, "antialias")) { // disabling/enabling AA
    ret.choices.push_back(L"Disabled");
    ret.choices.push_back(L"Enabled");

    ret.ptr.pBool = &g_aa;

    ret.text = L"Antialiasing";
    ret.type = MenuItemType::Toggle;

    MenuItem::MenuChoiceValue val0, val1;
    val0.asBool = false;
    val1.asBool = true;
    ret.values.push_back(val0);
    ret.values.push_back(val1);

    ret.valuetype = MenuItem::ValueTypeBool;
    if (g_aa) ret.choice_idx = 1;
    else ret.choice_idx = 0;
  } else if (!strcmp(which, "shadows")) {
    ret.choices.push_back(L"Disabled");
    ret.choices.push_back(L"Enabled");

    ret.ptr.pBool = &g_shadows;

    ret.text = L"Shadows";
    ret.type = MenuItemType::Toggle;

    MenuItem::MenuChoiceValue val0, val1;
    val0.asBool = false;
    val1.asBool = true;
    ret.values.push_back(val0);
    ret.values.push_back(val1);

    ret.valuetype = MenuItem::ValueTypeBool;
    if (g_shadows) ret.choice_idx = 1;
    else ret.choice_idx = 0;
  }
  return ret;
}

void MainMenu::OnLeftRightPressed(int delta) {
  MainMenu::MenuItem* itm = &(menuitems[curr_selection[curr_menu.size()-1]]);
  if (itm->type == MenuItemType::Toggle) {
    const int L = int(itm->choices.size());
    int next_ch = (itm->choice_idx + delta + L) % L;
    switch (itm->valuetype) {
      case MenuItem::ValueTypeBool:
        *(itm->ptr.pBool) = itm->values[next_ch].asBool;
        break;
      case MenuItem::ValueTypeInt:
        *(itm->ptr.pInt)  = itm->values[next_ch].asInt;
        break;
    }
    itm->choice_idx = next_ch;
  }
}

void MainMenu::FadeInHelpScreen() {
  fade_alpha0 = 0; fade_alpha1 = 1;
  fade_millis0 = GetElapsedMillis(); fade_millis1 = fade_millis0 + FADE_DURATION;
  is_in_help = true;
}

void MainMenu::FadeOutHelpScreen() {
  fade_alpha0 = 1; fade_alpha1 = 0;
  fade_millis0 = GetElapsedMillis(); fade_millis1 = fade_millis0 + FADE_DURATION;
  is_in_help = false;
}

int MainMenu::FADE_DURATION = 1000; // Milliseconds

void MainMenu::Update(float delta_secs) {
  fade_millis = GetElapsedMillis();
  if (fade_millis1 - fade_millis0 <= 0) {
    fade_alpha = fade_alpha0;
    return;
  }
  float c = (fade_millis - fade_millis0) * 1.0f / (fade_millis1 - fade_millis0);
  if (c < 0) c = 0; else if (c > 1) c = 1;
  fade_alpha = fade_alpha0 * (1.0f - c) + fade_alpha1 * c;
}

void MainMenu::OnEscPressed() {
  if (IsInHelp()) {
    FadeOutHelpScreen();
    ExitMenu();
  }
  else {
    ExitMenu();
    g_main_menu_visible = false;
  }
}

void TextMessage::Render() {
  const int y = WIN_H / 2, textsize = g_font_size;
  const float y0 = WIN_H/2 - g_font_size * 0.5f;
  for (int i=0; i<int(messages.size()); i++) {
    float w;
    std::wstring msg = messages[i];
    MeasureTextWidth(msg, &w);
    glm::vec3 c = glm::vec3(1.0f, 1.0f, 0.5f);
    glm::mat4 ident(1);
    RenderText(ClimbOpenGL, msg, WIN_W/2 - w/2, y0 + g_font_size * i, 1.0f, c, ident);
  }
}

unsigned TextMessage::program = 0;