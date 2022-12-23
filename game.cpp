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
#include <sstream>

extern GraphicsAPI g_api;
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
extern D3D11_VIEWPORT g_viewport11;
extern ID3D11Buffer* g_perscene_cb_light11;
extern struct VolumetricLightCB g_vol_light_cb;

// For render lights
extern ID3D11DeviceContext* g_context11;
extern ID3D11InputLayout* g_inputlayout_for_light11;
extern ID3D11Buffer* g_fsquad_for_light11, * g_perscene_cb_light11;
extern ID3D11BlendState* g_blendstate11;
extern ID3D11PixelShader* g_ps_light;
extern ID3D11VertexShader* g_vs_light;
extern ID3D11ShaderResourceView* g_gbuffer_srv11;
extern ID3D11SamplerState* g_sampler11;

extern void UpdatePerSceneCB(const DirectX::XMVECTOR* dir_light, const DirectX::XMMATRIX* lightPV, const DirectX::XMVECTOR* camPos);
extern void UpdateGlobalPerObjectCB(const DirectX::XMMATRIX* M, const DirectX::XMMATRIX* V, const DirectX::XMMATRIX* P);
extern GameScene* GetCurrentGameScene();

bool g_debug = true;

void StartGame();
void EnterEditMode();
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
  secs_elapsed = 0.0f;
  is_in_help = false;
  fade_alpha0 = fade_alpha1 = 0;
  fade_millis0 = fade_millis1 = 0;
  fsquad = new FullScreenQuad(ClimbScene::helpinfo_srv);

  sprites_helpinfo.push_back(new ChunkSprite(ClimbScene::model_char));
  sprites_helpinfo.push_back(new ChunkSprite(ClimbScene::model_anchor));
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

  Sprite* p3 = new ChunkSprite(ClimbScene::model_exit);
  p3->scale = glm::vec3(0.2, 0.2, 0.2);
  p3->pos = glm::vec3(0);
  sprites_helpinfo.push_back(p3); // for demo'ing level end
  sprites_helpinfo.push_back(new ChunkSprite(ClimbScene::model_coin)); // for demo'ing level end

  cam_helpinfo = new Camera();
  cam_helpinfo->InitForHelpInfo();
  
  const int W0 = 1280, H0 = 720; // Normalize to this
  int viewports[][4] = {
    { 264, 0, 192, 192 }, // 主角
    { 582, 23, 128, 128 }, // anchor
    { 1050, 454, 256, 256 }, // coin
    { 915, -56, 320, 320 }, // platforms[0] - platform example
    { 915, 61, 320, 320 }, // platforms[1] - platform example
    { 915, 170, 320, 320 }, // goal - platform example
    { 491, 451, 320, 320 }, // goal - level clear example
    { -32, 286, 320, 320 }, // coin - level clear example
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

  // TODO: Fix discrepancy between position of rendered goal platform and light shaft
  viewport_vollight = viewports11_helpinfo[6];
  viewport_vollight.TopLeftY += 32 * 1.0f / H0 * WIN_H;
  
  // Button pattern Q, W, E, D, C, S, Z, A
  const int dx[] = { -1, 0, 1, 1, 1, 0, -1, -1 };
  const int dy[] = { -1, -1, -1, 0, 1, 1, 1, 0 };
  for (int i = 0; i < 8; i++) {
    RECT tex{ 32 * i, 0, 32 * (i + 1), 32 };
    const glm::vec2 he{ 40, 40 };
    const glm::vec2 p{ 640 + 42 * dx[i], 186 + 42 * dy[i] };
    keys_sprites.push_back(new ImageSprite2D(ClimbScene::keys_srv, tex, p, he));
  }

  lights[0] = new DirectionalLight(glm::vec3(1, -1, 0), glm::vec3(-100, 0, 0), glm::vec3(1, 0, 0), 15 * 3.14159f / 180.0f);
  lights[1] = new DirectionalLight(glm::vec3(-1, -1, 0), glm::vec3(100, 0, 0), glm::vec3(1, 0, 0), 15 * 3.14159f / 180.0f);
  lights[2] = new DirectionalLight(glm::vec3(0, -1, 0), glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), 15 * 3.14159f / 180.0f);

  EnterMenu(0, false);
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

void MainMenu::PrepareLightsForGoalDemo() {
  if (g_api == ClimbOpenGL) return;
  
  float tmp = 3.1415926 * 2 / 3;
  float a = secs_elapsed * 2.0f;
  const float dx = 7, dy = 10;
  glm::vec3 x(cos(a) * dx, -dy, sin(a) * dx);
  x = glm::normalize(x);
  lights[0]->dir = x;
  x = glm::normalize(glm::vec3(cos(a + tmp) * dx, -dy, sin(a + tmp) * dx));
  lights[1]->dir = x;
  x = glm::normalize(glm::vec3(cos(a - tmp) * dx, -dy, sin(a - tmp) * dx));
  lights[2]->dir = x;
  
  D3D11_MAPPED_SUBRESOURCE mapped;
  assert(SUCCEEDED(g_context11->Map(g_perscene_cb_light11, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));

  g_vol_light_cb.spotlightCount = 0;
    
  glm::vec3 pos(0, 0, 0);
  g_vol_light_cb.spotlightColors[0].m128_f32[0] = 2;
  g_vol_light_cb.spotlightColors[0].m128_f32[1] = 2;
  g_vol_light_cb.spotlightColors[0].m128_f32[2] = 0;
  lights[0]->pos = pos;
  g_vol_light_cb.spotlightPV[0] = lights[0]->GetPV_D3D11();

  g_vol_light_cb.spotlightColors[1].m128_f32[0] = 2;
  g_vol_light_cb.spotlightColors[1].m128_f32[1] = 0;
  g_vol_light_cb.spotlightColors[1].m128_f32[2] = 2;
  lights[1]->pos = pos;
  g_vol_light_cb.spotlightPV[1] = lights[1]->GetPV_D3D11();

  g_vol_light_cb.spotlightColors[2].m128_f32[0] = 0;
  g_vol_light_cb.spotlightColors[2].m128_f32[1] = 2;
  g_vol_light_cb.spotlightColors[2].m128_f32[2] = 2;
  g_vol_light_cb.spotlightPV[2] = lights[2]->GetPV_D3D11();
  lights[2]->pos = pos;
  g_vol_light_cb.spotlightCount = 3;
  g_vol_light_cb.aspect = WIN_W * 1.0f / WIN_H;
  g_vol_light_cb.fovy = 60 * 3.14159f / 180.0f;
  g_vol_light_cb.forceAlwaysOn = 1;
  g_vol_light_cb.cam_pos = cam_helpinfo->GetPos_D3D11();
  memcpy(mapped.pData, &g_vol_light_cb, sizeof(g_vol_light_cb));

  g_context11->Unmap(g_perscene_cb_light11, 0);
}

void MainMenu::RenderLightsForGoalDemo() {
  g_context11->IASetInputLayout(g_inputlayout_for_light11);
  g_context11->VSSetShader(g_vs_light, nullptr, 0);
  g_context11->PSSetShader(g_ps_light, nullptr, 0);
  g_context11->PSSetSamplers(0, 1, &g_sampler11);
  g_context11->PSSetShaderResources(0, 1, &g_gbuffer_srv11);
  g_context11->PSSetConstantBuffers(0, 1, &g_perscene_cb_light11);
  float blend_factor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
  g_context11->OMSetBlendState(g_blendstate11, blend_factor, 0xFFFFFFFF);
  unsigned stride = sizeof(float) * 4;
  unsigned zero = 0;
  g_context11->IASetVertexBuffers(0, 1, &g_fsquad_for_light11, &stride, &zero);
  g_context11->Draw(6, 0);
}

void MainMenu::DrawHelpScreen() {
  // Directly render to backbuffer
  // Normal Pass
  ID3D11RenderTargetView* rtvs[] = { g_backbuffer_rtv11, nullptr }; // Do not write gbuffer
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

    // Temporary routine for drawing light shaft
    // Caution: g-buffer provides world position and therefore cannot be cleared
    sprites_helpinfo[i]->Render_D3D11();
  }

  // Draw Light Shaft
  {
    g_context11->RSSetViewports(1, &viewport_vollight);
    const glm::vec3 pos_backup = cam_helpinfo->pos;
    cam_helpinfo->pos = glm::vec3(0, 0, 199);

    //UpdateGlobalPerObjectCB(nullptr, &(cam_helpinfo->GetViewMatrix_D3D11()), &P);
    //UpdatePerSceneCB(&g_dir_light->GetDir_D3D11(), &(g_dir_light->GetPV_D3D11()), &(cam_helpinfo->GetPos_D3D11()));

    //float zeros[] = { 0, 0, 0, 0 };
    //g_context11->ClearRenderTargetView(g_gbuffer_rtv11, zeros);
    //g_context11->ClearDepthStencilView(g_dsv11, D3D11_CLEAR_DEPTH, 1.0f, 0);

    g_context11->OMSetRenderTargets(1, &g_backbuffer_rtv11, g_dsv11);
    PrepareLightsForGoalDemo();
    RenderLightsForGoalDemo();
    g_context11->OMSetRenderTargets(2, rtvs, g_dsv11);
    cam_helpinfo->pos = pos_backup;
  }

  g_context11->RSSetViewports(1, &g_viewport11);

  for (ImageSprite2D* sp : keys_sprites) sp->Render_D3D11();
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
    DrawHelpScreen();
  }

  // DBG
  {
    std::wstringstream ws;
    ws << std::to_wstring(curr_menu.size()) << " | " << std::to_wstring(curr_selection.size());
    RenderText(ClimbD3D11, ws.str(), 20, 700, 1, glm::vec3(0, 1, 1), uitransform);
  }
}

void MainMenu::OnUpDownPressed(int delta) {
  PrintStatus();
  const int L = int(menuitems.size());
  if (menuitems.empty() == false && curr_selection.empty() == false) {
    int* pCh = &(curr_selection[curr_menu.size()-1]);
    int new_ch = int(delta + (*pCh) + L) % L;
    *pCh = new_ch;
  }
}

void MainMenu::EnterMenu(int idx, bool is_from_exit) {
  printf("EnterMenu(%d, %d)\n", idx, is_from_exit);
  if (idx == 0) {
    printf("");
  }
  menutitle.clear();
  menuitems.clear();
  const wchar_t* title0 = L"C Y Climb",
               * title1 = L"20190402";

  if (idx == 0) {
    menutitle.push_back(title0);
    menutitle.push_back(title1);
    menutitle.push_back(L"Main Menu");

    menuitems.push_back(MenuItem(L"Start Game"));
    menuitems.push_back(MenuItem(L"Edit Mode"));
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
  }
  else if (idx == 3) {
    menutitle.push_back(L"[Edit Mode Menu]");
    menuitems.push_back(MenuItem(L"Test Play"));
    menuitems.push_back(MenuItem(L"Exit Edit Mode"));
  }

  if (!is_from_exit || curr_menu.empty()) {
    curr_menu.push_back(idx);
    curr_selection.push_back(0);
  }
  PrintStatus();
}

void MainMenu::OnEnter() {
  switch (curr_menu.back()) {
    case 0: {
      switch (curr_selection[curr_menu.size()-1]) {
      case 0:
        StartGame(); // Start Game
        break;
      case 1: {
        EnterMenu(3, false);
        EnterEditMode();
        break;
      }
      case 2: EnterMenu(2, false); break; // Options
      case 3: EnterMenu(1, false); break; // Help
      case 4: {
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
    case 3: {
      switch (curr_selection[curr_menu.size() - 1]) {

      }
      break;
    }
    case 2:
    default: break;
  }
}

void MainMenu::ExitMenu() {
  curr_menu.pop_back();
  curr_selection.pop_back();
  if (curr_menu.empty() == false) {
    EnterMenu(curr_menu.back(), true);
  } else {
    g_main_menu_visible = false;
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
  secs_elapsed += delta_secs;

  fade_millis = GetElapsedMillis();
  if (fade_millis1 - fade_millis0 <= 0) {
    fade_alpha = fade_alpha0;
    return;
  }

  float c = (fade_millis - fade_millis0) * 1.0f / (fade_millis1 - fade_millis0);
  if (c < 0) c = 0; else if (c > 1) c = 1;
  fade_alpha = fade_alpha0 * (1.0f - c) + fade_alpha1 * c;

  float y = fabs(1 * sinf(secs_elapsed * 3.14159f * 2 / 1.5f));
  sprites_helpinfo[0]->pos.y = y;
  float r = delta_secs * 90.0f;
  sprites_helpinfo[1]->RotateAroundLocalAxis(glm::vec3(0, 1, 0), r); // Anchor
  sprites_helpinfo[2]->RotateAroundLocalAxis(glm::vec3(0, 1, 0), r); // Rocket
  r = 0.2 * sinf(secs_elapsed * 3.14159f * 2);
  sprites_helpinfo[3]->RotateAroundLocalAxis(glm::vec3(0, 1, 0), r);
  sprites_helpinfo[4]->RotateAroundLocalAxis(glm::vec3(0, 1, 0), r);
  sprites_helpinfo[5]->RotateAroundLocalAxis(glm::vec3(0, 1, 0), r);
}

void MainMenu::OnEscPressed() {
  if (IsInHelp()) {
    FadeOutHelpScreen();
    ExitMenu();
  }
  else {
    if (curr_menu.empty() == false) {
      switch (curr_menu.back()) {
      case 3:  // 编辑模式
        g_main_menu_visible = !g_main_menu_visible;
        return;
      default: break;
      }
    }
    
    if (GetCurrentGameScene()->CanHideMenu() == false && curr_menu.size() == 1) return;
    ExitMenu();
  }
}

void MainMenu::PrintStatus() {
  printf("curr_selection:");
  for (const int s : curr_selection) {
    printf(" %d", s);
  }
  printf("\n");
  printf("curr_menu:");
  for (const int m : curr_menu) {
    printf(" %d", m);
  }
  printf("\n");
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