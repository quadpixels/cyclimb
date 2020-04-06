#include "scene.hpp"
#include "textrender.hpp"
#include "game.hpp"
#include "util.hpp"
#define _USE_MATH_DEFINES // For MSVC
#include <math.h>
#include <assert.h>
#include <wchar.h>

std::vector<ChunkGrid*> LightTestScene::model_backgrounds;
std::map<char, ChunkGrid*> LightTestScene::model_digits;
ChunkGrid* LightTestScene::model_clock;
extern bool g_main_menu_visible;
extern DirectionalLight* g_dir_light1;

float LightTestScene::ACTOR_INIT_Y = -38;

LightTestScene::LightTestScene() {
  for (int i=0; i<6; i++)
    bg_sprites[i] = new ChunkSprite(model_backgrounds[i]);

  clock_sprite = new ChunkSprite(model_clock);
  clock_sprite->pos.z = -35;
  clock_sprite->pos.y = 50;
  clock_sprite->scale = glm::vec3(2, 2, 2);
  for (int i = 0; i < 20; i++) {
    digit_sprites[i] = new ChunkSprite(model_digits[' ']);
    ChunkSprite* s = digit_sprites[i];
    s->scale = glm::vec3(2, 2, 2);
    int y = i / 10, x = i % 10;
    int xx = -90 + 20 * x, yy = clock_sprite->pos.y - 12 + 32 * (1 - y);
    s->pos.z = clock_sprite->pos.z + 1;
    s->pos.x = xx;
    s->pos.y = yy;
  }
  
  bg_sprites[1]->pos.x = -100; // Left
  bg_sprites[2]->pos.x =  100; // Right
  bg_sprites[3]->pos.y = 100; // Top
  bg_sprites[4]->pos = glm::vec3(-100, 100, 0); // Top-Left
  bg_sprites[5]->pos = glm::vec3(100, 100, 0);

  camera = new Camera();
  camera->pos = glm::vec3(0, 23, 180);
  camera->lookdir = glm::vec3(0, 0, -1);
  camera->up = glm::vec3(0, 1, 0);

  lights[0] = new DirectionalLight(glm::vec3(0, -1, 0), glm::vec3(-100, 200, 0), glm::vec3(1, 0, 0), 13 * 3.14159f / 180.0f);
  lights[1] = new DirectionalLight(glm::vec3(0, -1, 0), glm::vec3(-60, 200, 0), glm::vec3(1, 0, 0), 13 * 3.14159f / 180.0f);
  lights[2] = new DirectionalLight(glm::vec3(0, -1, 0), glm::vec3(-20, 200, 0), glm::vec3(1, 0, 0), 13 * 3.14159f / 180.0f);
  lights[3] = new DirectionalLight(glm::vec3(0, -1, 0), glm::vec3(20, 200, 0), glm::vec3(1, 0, 0), 13 * 3.14159f / 180.0f);
  lights[4] = new DirectionalLight(glm::vec3(0, -1, 0), glm::vec3(60, 200, 0), glm::vec3(1, 0, 0), 13 * 3.14159f / 180.0f);
  lights[5] = new DirectionalLight(glm::vec3(0, -1, 0), glm::vec3(100, 200, 0), glm::vec3(1, 0, 0), 13 * 3.14159f / 180.0f);
  for (int i=6; i<16; i++)
    lights[i] = new DirectionalLight(glm::vec3(0, -1, 0), glm::vec3(100, 200, 0), glm::vec3(1, 0, 0), 13 * 3.14159f / 180.0f);

  for (int i = 0; i < 16; i++) {
    char_sprites[i] = new ChunkSprite(ClimbScene::model_char);
    int x = i / 4, z = i % 4;
    ChunkSprite* s = char_sprites[i];
    s->scale = glm::vec3(1.5, 1.5, 1.5);
    const float DX = 66, DZ = 33;
    s->pos.x = -DX + 2 * DX / 3.0 * x;
    s->pos.z = -DZ + 2 * DZ / 3.0 * z;
    s->pos.y = ACTOR_INIT_Y;
    if (z % 2) s->pos.x -= DZ / 3; else s->pos.x += DZ / 3;
    deltays[i] = 0;
  }

  curr_light_angle = 0;
  last_sec = 0;
  total_secs = 0;
}

extern int g_scene_idx;
void LightTestScene::InitStatic() {
  if (g_scene_idx != 2) return;
  // Middle
  model_backgrounds.push_back(new ChunkGrid("stage\\stage1.vox"));
  // Left
  model_backgrounds.push_back(new ChunkGrid("stage\\stage1_L.vox"));
  // Right
  model_backgrounds.push_back(new ChunkGrid("stage\\stage1_R.vox"));
  // Top
  model_backgrounds.push_back(new ChunkGrid("stage\\stage1_T.vox"));
  // Top-Left
  model_backgrounds.push_back(new ChunkGrid("stage\\stage1_TL.vox"));
  // Top-Right
  model_backgrounds.push_back(new ChunkGrid("stage\\stage1_TR.vox"));

  std::string base = "C:\\Users\\nitroglycerine\\Downloads\\MagicaVoxel-0.99.1-alpha-win64\\vox\\climb\\";
  model_clock = new ChunkGrid(std::string(base + "digitboard.vox").c_str());

  for (int i = 0; i <= 9; i++) {
    char buf[233];
    sprintf(buf, "digit%d.vox", i);
    model_digits['0' + i] = new ChunkGrid(std::string(base + std::string(buf)).c_str());
  }
  model_digits['-'] = new ChunkGrid(std::string(base + "digit_hyphen.vox").c_str());
  model_digits[':'] = new ChunkGrid(std::string(base + "digit_colon.vox").c_str());
  model_digits[' '] = new ChunkGrid(std::string(base + "digit_null.vox").c_str());
}

void LightTestScene::PrepareSpriteListForRender() {
  g_main_menu_visible = false; // HACK
  sprite_render_list.clear();
  for (int i=0; i<sizeof(bg_sprites)/sizeof(bg_sprites[0]); i++)
    sprite_render_list.push_back(bg_sprites[i]);
  for (int i = 0; i < 16; i++)
    sprite_render_list.push_back(char_sprites[i]);
  sprite_render_list.push_back(clock_sprite);

  for (int i = 0; i < 20; i++) {
    char ch = ' ';
    DigitState& st = digit_states[i];

    // tween is started @ update

    const float TWEEN_LEN = 0.2f;
    if (st.tween_end_sec < total_secs) {
      st.old_char = st.new_char;
      digit_sprites[i]->scale.y = 2;
      ch = st.new_char;
    }
    else if (total_secs > st.tween_end_sec - TWEEN_LEN) {
      float completed = total_secs - (st.tween_end_sec - TWEEN_LEN);
      float yscale = 2.0f;
      if (completed < TWEEN_LEN / 2) {
        ch = st.old_char;
        yscale = 2 * (1.0f - completed / (TWEEN_LEN / 2));
      }
      else {
        ch = st.new_char;
        yscale = 2 * ((completed - TWEEN_LEN / 2) / (TWEEN_LEN / 2));
      }
      digit_sprites[i]->scale.y = yscale;
    }

    if (ch != ' ' && model_digits.find(ch) != model_digits.end()) {
      digit_sprites[i]->chunk = model_digits[ch];
      sprite_render_list.push_back(digit_sprites[i]);
    }
  }
}

std::vector<Sprite*>* LightTestScene::GetSpriteListForRender() {
  return &sprite_render_list;
}

extern struct VolumetricLightCB g_vol_light_cb;
extern ID3D11DeviceContext *g_context11;
extern GraphicsAPI g_api;
extern ID3D11Buffer* g_perscene_cb_light11;
extern Camera* GetCurrentSceneCamera();

void LightTestScene::PrepareLights() {
  if (g_api == ClimbOpenGL) return;
  
  //UpdatePerSceneCB(&g_dir_light->GetDir_D3D11(), &(g_dir_light->GetPV_D3D11()), &(GetCurrentSceneCamera()->GetPos_D3D11()), nullptr);
  D3D11_MAPPED_SUBRESOURCE mapped;
  assert(SUCCEEDED(g_context11->Map(g_perscene_cb_light11, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));

  const int NC = 6;
  float colors[][3] = {
    {1,0,0}, {0,1,0}, {0,0,1}, {1,1,0}, {1,0,1}, {0,1,1}
  };

  float dz = abs(sin(curr_light_angle)), dy = -abs(cos(curr_light_angle));

  if (GetMacroState(nullptr) == OLD_YEAR) {
    for (int i = 0; i < 6; i++) {
      int x = -100 + i * 40;
      int cidx = 0;
      g_vol_light_cb.spotlightColors[i].m128_f32[0] = colors[cidx][0];
      g_vol_light_cb.spotlightColors[i].m128_f32[1] = colors[cidx][1];
      g_vol_light_cb.spotlightColors[i].m128_f32[2] = colors[cidx][2];
      lights[i]->pos.x = x;
      lights[i]->pos.y = 200;
      lights[i]->pos.z = 0;
      lights[i]->dir = glm::normalize(glm::vec3(0, dy, dz));
      g_vol_light_cb.spotlightPV[i] = lights[i]->GetPV_D3D11();
    }
    g_vol_light_cb.spotlightCount = 6;
    g_vol_light_cb.forceAlwaysOn = false;
  }
  else {
    if (total_secs - last_update_total_secs > 0.1) {
      last_update_total_secs = total_secs;

      const float HX = 100.0f, HY = 80.0f, HZ = 50.0f, LIGHT_MULT = 3;
      for (int i = 0; i < 16; i++) {
        float x = rand() * 1.0f / RAND_MAX;
        if (x < 0.3 || x > 0.7) { // Left wall
          glm::vec3 p(-HX, -HY + 2 * HY*1.0f*rand() / RAND_MAX, -HZ + 2 * HZ*1.0f*rand() / RAND_MAX);
          glm::vec3 q( HX, -HY + 2 * HY*1.0f*rand() / RAND_MAX, -HZ + 2 * HZ*1.0f*rand() / RAND_MAX);
          if (x < 0.3) {
            lights[i]->dir = glm::normalize(q - p);
            lights[i]->pos = p;
          }
          else {
            lights[i]->dir = glm::normalize(p - q);
            lights[i]->pos = q;
          }
          int cidx = rand() % NC;
          g_vol_light_cb.spotlightColors[i].m128_f32[0] = colors[cidx][0] * LIGHT_MULT;
          g_vol_light_cb.spotlightColors[i].m128_f32[1] = colors[cidx][1] * LIGHT_MULT;
          g_vol_light_cb.spotlightColors[i].m128_f32[2] = colors[cidx][2] * LIGHT_MULT;
          g_vol_light_cb.spotlightPV[i] = lights[i]->GetPV_D3D11();
        }
        else {
          glm::vec3 p(-HX + 2 * HX*rand()*1.0f / RAND_MAX, HY, -HZ + 2 * HZ*1.0f*rand() / RAND_MAX);
          glm::vec3 q(-HX + 2 * HX*rand()*1.0f / RAND_MAX, -HY, -HZ + 2 * HZ*1.0f*rand() / RAND_MAX);
          lights[i]->dir = glm::normalize(q - p);
          lights[i]->pos = p;
          int cidx = rand() % NC;
          g_vol_light_cb.spotlightColors[i].m128_f32[0] = colors[cidx][0] * LIGHT_MULT;
          g_vol_light_cb.spotlightColors[i].m128_f32[1] = colors[cidx][1] * LIGHT_MULT;
          g_vol_light_cb.spotlightColors[i].m128_f32[2] = colors[cidx][2] * LIGHT_MULT;
          g_vol_light_cb.spotlightPV[i] = lights[i]->GetPV_D3D11();
        }
      }
      g_vol_light_cb.spotlightCount = 16;
      g_vol_light_cb.forceAlwaysOn = false;
    }
  }


  g_vol_light_cb.cam_pos = GetCurrentSceneCamera()->GetPos_D3D11();

  memcpy(mapped.pData, &g_vol_light_cb, sizeof(g_vol_light_cb));
  g_context11->Unmap(g_perscene_cb_light11, 0);
}

void ClimbScene::PrepareLights() {
  if (g_api == ClimbOpenGL) return;

  {
    float tmp = 3.1415926 * 2 / 3;
    float a = light_phase;
    const float dx = 4, dy = 10;
    glm::vec3 x(cos(a) * dx, -dy, sin(a) * dx);
    x = glm::normalize(x);
    lights[0]->dir = x;
    x = glm::normalize(glm::vec3(cos(a + tmp) * dx, -dy, sin(a + tmp) * dx));
    lights[1]->dir = x;
    x = glm::normalize(glm::vec3(cos(a - tmp) * dx, -dy, sin(a - tmp) * dx));
    lights[2]->dir = x;
  }

  //UpdatePerSceneCB(&g_dir_light->GetDir_D3D11(), &(g_dir_light->GetPV_D3D11()), &(GetCurrentSceneCamera()->GetPos_D3D11()), nullptr);
  D3D11_MAPPED_SUBRESOURCE mapped;
  assert(SUCCEEDED(g_context11->Map(g_perscene_cb_light11, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));

  if (is_all_rockets_collected) {
    g_vol_light_cb.spotlightCount = 0;
    for (Platform* p : platforms) {
      ExitPlatform* ep = dynamic_cast<ExitPlatform*>(p);
      if (ep) { // There should be only 1 exit platform
        glm::vec3 pos = ep->sprite->pos;
        g_vol_light_cb.spotlightColors[0].m128_f32[0] = 1;
        g_vol_light_cb.spotlightColors[0].m128_f32[1] = 1;
        g_vol_light_cb.spotlightColors[0].m128_f32[2] = 0;
        lights[0]->pos = pos;
        g_vol_light_cb.spotlightPV[0] = lights[0]->GetPV_D3D11();

        g_vol_light_cb.spotlightColors[1].m128_f32[0] = 1;
        g_vol_light_cb.spotlightColors[1].m128_f32[1] = 0;
        g_vol_light_cb.spotlightColors[1].m128_f32[2] = 1;
        lights[1]->pos = pos;
        g_vol_light_cb.spotlightPV[1] = lights[1]->GetPV_D3D11();

        g_vol_light_cb.spotlightColors[2].m128_f32[0] = 0;
        g_vol_light_cb.spotlightColors[2].m128_f32[1] = 1;
        g_vol_light_cb.spotlightColors[2].m128_f32[2] = 1;
        g_vol_light_cb.spotlightPV[2] = lights[2]->GetPV_D3D11();
        lights[2]->pos = pos;
        g_vol_light_cb.spotlightCount = 3;

        break;
      }
    }
  }
  else {
    g_vol_light_cb.spotlightCount = 0;
  }

  g_vol_light_cb.forceAlwaysOn = false;
  g_vol_light_cb.cam_pos = GetCurrentSceneCamera()->GetPos_D3D11();

  memcpy(mapped.pData, &g_vol_light_cb, sizeof(g_vol_light_cb));
  g_context11->Unmap(g_perscene_cb_light11, 0);
}

void LightTestScene::Update(float secs) {
  total_secs += secs;
  for (int i = 0; i < NUM_ACTORS; i++) {
    float mult = (GetMacroState(nullptr) == OLD_YEAR) ? 3 : 12;
    deltays[i] = mult * abs(cos(total_secs * 2 * 3.14159));
    char_sprites[i]->pos.y = deltays[i] + ACTOR_INIT_Y;
  }

  
  int curr_sec = 0;
  if (GetMacroState(&curr_sec) == OLD_YEAR) {
    status_string = "2019-12-31 23:59:" + std::to_string(50 + curr_sec);
    if (curr_sec != last_sec) {
      curr_light_angle = 0;
    }
    else {
      curr_light_angle += secs * 0.2 * 3.14159;
      curr_light_angle = std::min(curr_light_angle, 3.14159f / 2);
    }
  }
  else {
    status_string = "2020-01-01 00:00:0" + std::to_string(curr_sec - 10);
    curr_light_angle = 0;
  }

  // Set tween
  for (int i = 0; i < 20; i++) {
    DigitState& st = digit_states[i];
    if (st.new_char != status_string[i]) {
      st.tween_end_sec = total_secs + 0.2f;
      st.new_char = status_string[i];
    }
  }

  last_sec = curr_sec;
}

LightTestScene::MacroState LightTestScene::GetMacroState(int* curr_sec) {
  int s = int(total_secs) % 20;
  if (curr_sec != nullptr)
    *curr_sec = s;
  if (s < 10) return OLD_YEAR; else return NEW_YEAR;
}