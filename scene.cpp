#include "scene.hpp"
#include "textrender.hpp"
#include "game.hpp"
#include "sounds.hpp"
#include "util.hpp"
#define _USE_MATH_DEFINES // For MSVC
#include <math.h>
#include <assert.h>
#include <wchar.h>
#ifdef WIN32
#include "WICTextureLoader.h"
extern ID3D11Device* g_device11;
extern ID3D11DeviceContext* g_context11;
#endif

extern unsigned g_fadein_complete_millis;
extern unsigned g_last_millis;
extern char g_arrow_dx, g_arrow_dy;

extern Particles* GetGlobalParticles();
extern TextMessage* g_textmessage;
extern float g_cam_rot_x, g_cam_rot_y;
extern std::bitset<18> g_cam_flags;

GLuint GetShaderProgram(int idx);
extern int WIN_W, WIN_H;
extern bool g_main_menu_visible;
extern bool g_debug;

static const char kKeys[] = { 'q', 'e', 'w', 'a', 'd', 'z', 's', 'c' };
static const glm::vec3 kDirs[] = {
    glm::vec3(-1, 1, 0), glm::vec3(1, 1, 0), glm::vec3(0, 1, 0), // Q E W
    glm::vec3(-1, 0, 0), glm::vec3(1, 0, 0), // A D
    glm::vec3(-1,-1, 0), glm::vec3(0,-1, 0), glm::vec3(1,-1, 0), // Z S C
};

//==========================

void TestShapesScene::PrepareSpriteListForRender() {
  sprite_render_list.clear();
  sprite_render_list.push_back(test_sprite);
  sprite_render_list.push_back(global_xyz);
}

std::vector<Sprite*>* TestShapesScene::GetSpriteListForRender() {
  return &sprite_render_list;
}

//==========================
#ifdef WIN32
ID3D11Resource* ClimbScene::helpinfo_res, *ClimbScene::keys_res;
ID3D11ShaderResourceView* ClimbScene::helpinfo_srv, *ClimbScene::keys_srv;
#endif
ClimbScene* ClimbScene::instance = nullptr;
std::vector<ChunkGrid*> ClimbScene::model_platforms;
std::vector<ChunkGrid*> ClimbScene::model_backgrounds1;
std::vector<ChunkGrid*> ClimbScene::model_backgrounds2;
ChunkGrid* ClimbScene::model_char;
ChunkGrid* ClimbScene::model_anchor;
ChunkGrid* ClimbScene::model_coin;
ChunkGrid* ClimbScene::model_exit;
const float ClimbScene::SPRING_K = 57.0f;
const float ClimbScene::L0 = 5.0f;
const float ClimbScene::GRAVITY = -338.0f;
const int   ClimbScene::NUM_SEGMENTS = 20;
const float ClimbScene::PROBE_DIST = 80.0f;
const float ClimbScene::PLAYER_DISP_DELTAZ = 10.0f;
const float ClimbScene::X_VEL_DAMP = 0.99f;
const float ClimbScene::Y_VEL_DAMP = 0.97f;
const glm::vec3 ClimbScene::RELEASE_THRUST = glm::vec3(0, 30, 0);
const int   ClimbScene::PROBE_DURATION = 170;
const float ClimbScene::ANCHOR_LEN = 7.0f;
const float ClimbScene::BACKGROUND_SCALE = 2.0f;
const float ClimbScene::CAM_FOLLOW_DAMP = 0.04f;

const int   ClimbScene::COUNTDOWN_MILLIS = 1000;
const int   ClimbScene::LEVEL_FINISH_SEQ_MILLIS = 1000;
const float ClimbScene::X_THRUST = 80.0f;
const int   ClimbScene::LEVEL_FINISH_JUMP_PERIOD = 500;
const float ClimbScene::INVERSE_INERTIA = 0.05f;

extern bool IsGL();
extern bool IsD3D11();
extern bool IsD3D12();
extern glm::vec3 WindowCoordToPickRayDir(Camera* cam, int x, int y);
extern glm::vec3 WindowCoordToGamePlane(Camera* cam, int x, int y);
extern int g_mouse_x, g_mouse_y;

const glm::vec3 ClimbScene::PLAYER_ROPE_ENDPOINT[8] = 
  {
    glm::vec3(-7/1.414f, 7/1.414f, 0.0f), // 左上
    glm::vec3( 7/1.414f, 7/1.414f, 0.0f), // 右上
    glm::vec3( 0,        7,        0   ), // 上
    glm::vec3(-7/1.414f, 7/1.414f, 0.0f), // 左
    glm::vec3( 7/1.414f, 7/1.414f, 0.0f), // 右
    glm::vec3(-7/1.414f, 7/1.414f, 0.0f), // 左下
    glm::vec3( 0,        7,        0   ), // 下
    glm::vec3( 7/1.414f, 7/1.414f, 0.0f), // 右
  };
    

void ClimbScene::InitStatic() {
  model_platforms.push_back(new ChunkGrid("climb/1.vox"));
  model_platforms.push_back(new ChunkGrid("climb/2.vox"));
  model_char = new ChunkGrid("climb/chr.vox");
  model_anchor = new ChunkGrid("climb/anchor.vox");
  model_backgrounds1.push_back(new ChunkGrid("climb/bg1.vox"));
  model_backgrounds1.push_back(new ChunkGrid("climb/bg1_2.vox"));
  model_backgrounds2.push_back(new ChunkGrid("climb/bg2.vox"));
  model_backgrounds2.push_back(new ChunkGrid("climb/bg2_2.vox"));
  model_coin = new ChunkGrid("climb/coin.vox");
  model_exit = new ChunkGrid("climb/goal.vox");

  if (!IsGL()) {
#ifdef WIN32
    if (IsD3D11()) {
      HRESULT hr = DirectX::CreateWICTextureFromFile(g_device11,
        L"climb\\help.jpg", &helpinfo_res, &helpinfo_srv);
      assert(SUCCEEDED(hr));

      hr = DirectX::CreateWICTextureFromFile(g_device11,
        L"climb\\keys.jpg", &keys_res, &keys_srv);
      assert(SUCCEEDED(hr));
    }
    else if (IsD3D12()) {
      printf("(Todo: load texture from file\n");
    }
#endif
  }
}

void ClimbScene::PreRender() { }

void ClimbScene::PrepareSpriteListForRender() {
  // 在此处理删除掉的coins和platforms
  std::vector<Platform*> platforms1;
  std::vector<Sprite*> coins1;
  for (Platform* p : platforms) {
    if (p->marked_for_removal == true) {
    }
    else {
      platforms1.push_back(p);
    }
  }
  for (Sprite* c : coins) {
    if (c->marked_for_removal == true) {
    }
    else {
      coins1.push_back(c);
    }
  }
  platforms = platforms1;
  coins = coins1;

  sprite_render_list.clear();
  bool should_add_hl = false;
  glm::vec3 campos = camera->pos;
  campos.z = 0;
  hovered_sprite = nullptr;
  for (Platform* p : platforms) {
    Sprite* sp = p->GetSpriteForDisplay();
    if (this->game_state == ClimbGameStateInEditing && 
        sp != nullptr &&
        curr_edit_option == 0) {
      ChunkSprite* csp = dynamic_cast<ChunkSprite*>(sp);

      if (is_dragging && dragged_sprite) {
        glm::vec3 drag_delta = WindowCoordToGamePlane(camera, mouse_x, mouse_y) - drag_pos0;
        dragged_sprite->pos = dragged_sprite_pos0 + drag_delta;
        if (is_shift_down) {
          dragged_sprite->pos.x = int(dragged_sprite->pos.x);
          dragged_sprite->pos.y = int(dragged_sprite->pos.y);
        }
      }

      if (csp) {
        AABB aabb = csp->GetAABBInWorld();

        glm::vec3 pickray_dir = WindowCoordToPickRayDir(camera, mouse_x, mouse_y);
        glm::vec3 pickray_orig = camera->pos;

        bool x1 = (campos.x >= aabb.lb.x && campos.x <= aabb.ub.x &&
          campos.y >= aabb.lb.y && campos.y <= aabb.ub.y);
        bool x2 = aabb.IntersectRay(pickray_orig, pickray_dir);

        if (x2) {
          hovered_sprite = csp;
        }

        if (x1 || x2) {
          highlight_sprite->pos = sp->pos;
          float phase = fabs(sin(light_phase * 4));
          highlight_sprite->scale.x = (aabb.ub.x - aabb.lb.x) + phase + 1;
          highlight_sprite->scale.y = (aabb.ub.y - aabb.lb.y) + phase + 1;
          highlight_sprite->scale.z = (aabb.ub.z - aabb.lb.z) + phase + 1;
          should_add_hl = true;
        }
      }
    }
    sprite_render_list.push_back(sp);
  }
  if (should_add_hl) {
    sprite_render_list.push_back(highlight_sprite);
  }

  for (Sprite* s : coins) {
    sprite_render_list.push_back(s);
  }
  sprite_render_list.push_back(player);
  LayoutBackground();
  
  if ((rope_state == Anchored) || (rope_state == Probing)) {
    const int N = int(rope_segments.size());
    for (int i=0; i<N; i++) {
      const float completion = 1.0f * i / (N-1);
      glm::vec3 p = GetPlayerEffectiveRopeEndpoint() * (1.0f - completion) +
                    anchor_rope_endpoint * completion;
      rope_segments[i]->pos = p;
      sprite_render_list.push_back(rope_segments[i]);
    }
    sprite_render_list.push_back(anchor);
  }
  
  // Particle
  Particles* particles = GetGlobalParticles();
  for (int i=0; i<particles->particles.size(); i++) {
    sprite_render_list.push_back(particles->particles[i].sprite);
  }

  // Edit mode
  if (this->game_state == ClimbScene::ClimbGameState::ClimbGameStateInEditing) {
    Sprite* c = CurrentCursorSprite();
    if (c)
      sprite_render_list.push_back(c);
  }
}

std::vector<Sprite*>* ClimbScene::GetSpriteListForRender() {
  return &sprite_render_list;
}

ClimbScene::ClimbScene() {
  lights[0] = new DirectionalLight(glm::vec3(1, -1, 0), glm::vec3(-100, 200, 0), glm::vec3(1, 0, 0), 15 * 3.14159f / 180.0f);
  lights[1] = new DirectionalLight(glm::vec3(-1, -1, 0), glm::vec3(100, 200, 0), glm::vec3(1, 0, 0), 15 * 3.14159f / 180.0f);
  lights[2] = new DirectionalLight(glm::vec3(0, -1, 0), glm::vec3(0, 200, 0), glm::vec3(1, 0, 0), 15 * 3.14159f / 180.0f);
  is_test_playing = false;
  curr_edit_option = 0;
}

void ClimbScene::Init() {
  level_finish_state.parent = this;
  
  HideRope();
  camera = new Camera();
  camera->pos = glm::vec3(0, 0, 200);
  camera->lookdir = glm::vec3(0, 0, -1);
  camera->up = glm::vec3(0, 1, 0);
  
  // Player sprite
  player      = new ChunkSprite(model_char);
  SpawnPlayer();
  
  // Anchor sprite
  anchor      = new ChunkSprite(model_anchor);
  anchor->anchor.y = 10; // 把锚点移到模型顶上
  //anchor->scale = glm::vec3(0.5, 0.5, 0.5);
  
  // Rope segment sprite
  for (int i=0; i<NUM_SEGMENTS; i++) {
    ChunkSprite* s = new ChunkSprite(Particles::default_particle);
    s->pos = glm::vec3(0, 0, 0);
    rope_segments.push_back(s);
  }
  
  
  is_key_pressed = false;
  is_debug = false;
  anchor_levels = 0;
  
  player_x_thrust = 0;
  
  // Load Level
  LoadLevelData();
  num_coins = 0; num_coins_total = 0;
  curr_level = 1;
  SetBackground(0);
  StartLevel(curr_level);
  
  ClimbScene::instance = this;
  
  keyflags.reset();

  // Edit mode cursor
  ChunkGrid* chunkgrids[] = {
    dynamic_cast<ChunkGrid*>(Particles::default_particle),
    model_platforms[0],
    model_platforms[1],
    model_coin,
    model_exit,
  };
  for (int i = 0; i < 5; i++) {
    Sprite* s = new ChunkSprite(chunkgrids[i]);
    s->draw_mode = Sprite::DrawMode::WIREFRAME;
    s->scale = glm::vec3(1, 1, 1);
    cursor_sprites.push_back(s);
  }

  highlight_sprite = new ChunkSprite(Particles::default_particle);
  highlight_sprite->draw_mode = Sprite::DrawMode::WIREFRAME;
  highlight_sprite->anchor = glm::vec3(0, 0, 0);
}

void ClimbScene::Update(float secs) {
  bool should_step = false;
  if (g_main_menu_visible == false) {
    countdown_millis -= secs * 1000.0f;
    if (countdown_millis < 0) countdown_millis = 0;
    should_step = true;
  }
  
  switch (game_state) {
    case ClimbGameStateNotStarted: {
      // 游戏没开始时也要相机追踪
      CameraFollow(secs);
      break;
    }
    case ClimbGameStateInGame:
      {
        curr_level_time += secs;
        
        if (is_debug) {
          player->pos += debug_vel * (100.0f * secs);
        } else {
          if (should_step) {
            player->vel += glm::vec3(0, GRAVITY * secs, 0);
            player->Update(secs);
            player->vel.x += player_x_thrust * secs;
          }
        }
    
        if (player->pos.y < -100) {
          // Particles
          const int N = 100;
          for (int i=0; i<N; i++) {
            GetGlobalParticles()->SpawnDefaultSprite(player->pos, 
               1.5f + (rand() * 2.0f / RAND_MAX),
               2.0f + rand() * 1.0f / RAND_MAX);
          }
          
          SetGameState(ClimbGameStateStartCountdown);
          SpawnPlayer();
        }
        
        // probe
        const unsigned millis = GetElapsedMillis();
        if (should_step) {
          probe_remaining_millis -= 1000.0f * secs;
          if (probe_remaining_millis < 0) {
            probe_remaining_millis = -999;
          }
        }
        float probe_completion = 1.0f - probe_remaining_millis / PROBE_DURATION;
        bool probing = ((probe_completion > 0 || is_key_pressed) && (rope_state == Probing));
            
        if (probing) {
          if (probe_completion >= 1) {
            HideRope();
            probing = false;
          } else {
            const float t = sin(M_PI * probe_completion);
            // Probe!
            glm::vec3 probe_dest = GetPlayerEffectivePos() + probe_delta * t;
            const int num_steps = 20;
            glm::vec3 x = GetPlayerEffectivePos(), delta = (probe_dest - x) * (1.0f / num_steps);
            for (int i=0; i<num_steps; i++, x += delta) {
              for (int i=0; i<int(platforms.size()); i++) {
                Sprite* sp = platforms[i]->GetSpriteForCollision();
                if (is_key_pressed && // 只有按着键，才会钩上
                  sp != nullptr && sp->IntersectPoint(x)) {
                  CyclimbSound snd = CyclimbSound::Tap;
                  if (dynamic_cast<ExitPlatform*>(platforms[i])) {
                    BeginLevelCompleteSequence();
                    snd = CyclimbSound::Whistle;
                  }
                  
                  rope_state = Anchored;
                  probe_remaining_millis = -999;
                  SetAnchorPoint(x, probe_delta);
                  anchor_levels = 1;
                  {
                    const int N = 11;
                    for (int i=0; i<N; i++) {
                      GetGlobalParticles()->SpawnDefaultSprite(x, 
                        0.7f + rand()* 0.1f / RAND_MAX, 
                        2.0f);
                    }
                  }
                  
                  DamagablePlatform* dp = dynamic_cast<DamagablePlatform*>(platforms[i]);
                  if (dp) {
                    dp->DoDamage(x);
                    snd = CyclimbSound::SmallHit;
                  }
                  MyPlaySound(snd);
                  goto DONE;
                }
              }
            }
            if (rope_state != Anchored) {
              SetAnchorPoint(probe_dest, probe_delta);
            }
          }
        DONE: { }
        }
        
        if (should_step) {
          if (rope_state == Anchored) {
            glm::vec3 delta = anchor_rope_endpoint - GetPlayerEffectiveRopeEndpoint();
            if (glm::dot(delta, delta) > L0* L0) {
              const float len = sqrtf(glm::dot(delta, delta)) - L0;
              glm::vec3 pull = glm::normalize(delta);
              glm::vec3 acc = pull * len * SPRING_K;

              // 计算旋转
              {
                // 角速度变化量
                glm::vec3 local_pull = glm::transpose(player->orientation) * pull;
                glm::vec3 delta_omega = glm::cross(local_pull, -curr_player_rope_endpoint);
                player->omega += delta_omega * secs * INVERSE_INERTIA;
              }

              player->vel += acc * secs;
              player->vel.x *= X_VEL_DAMP;
              player->vel.y *= Y_VEL_DAMP;
            }
          }
        
          {
            player->omega *= 0.99f;
          }
        }
        
        // Intersect coin
        if (is_all_rockets_collected == false) {
          glm::vec3 p = GetPlayerEffectivePos();
          std::vector<Sprite*> next_coins;
          for (Sprite* c : coins) {
            if (c->IntersectPoint(p, 5)) { // Intersecting
              num_coins --;
              
              if (num_coins <= 0) {
                printf("num_coins <= 0\n");
                RevealExit();
              }
              
            } else {
              next_coins.push_back(c);
            }
          }
          coins = next_coins;
        }
        
        // 只有在还有剩余时才转
        if (num_coins > 0)
          RotateCoins(secs);
        
        // 无论何时都会转
        CameraFollow(secs);
        if (should_step) {
          GetGlobalParticles()->Update(secs);
        }
        light_phase += secs;
      }
      break;
    case ClimbGameStateStartCountdown: {
      GetGlobalParticles()->Update(secs);
      CameraFollow(secs);
      if (countdown_millis <= 0) {
        SetGameState(ClimbGameStateInGame);
      }
      break;
    }
    case ClimbGameStateLevelFinishSequence: {
        
      GetGlobalParticles()->Update(secs);
      CameraFollow(secs);
      
      float completion = level_finish_state.GetCompletion();
      if (completion > 0.1f) { HideRope(); }
      float t = 1.0f - (1.0f - completion) * (1.0f - completion);
      player->pos = t * level_finish_state.pos1 +
                    (1.0f - t) * level_finish_state.pos0;
      player->vel = glm::vec3(0, 0, 0);
      if (completion >= 1.0f) {
        SetGameState(ClimbGameStateLevelEndWaitKey);
      }
      break;
    }
    case ClimbGameStateLevelEndWaitKey: {
      unsigned millis = GetElapsedMillis();
      int period = int(millis / LEVEL_FINISH_JUMP_PERIOD);
      float phase = (millis - period * LEVEL_FINISH_JUMP_PERIOD) * 1.0f / LEVEL_FINISH_JUMP_PERIOD;
      player->pos = level_finish_state.pos0 + glm::vec3(0, 4*sinf(M_PI*phase), 0);
      
      // Rockets fly
      for (Sprite* s : coins) {
        s->pos += s->vel;
        if (rand() * 1.0f / RAND_MAX < 1.0f / 6) {
          GetGlobalParticles()->SpawnDefaultSprite(s->pos,
            0.6f,
            0.6f);
        }
        s->vel += glm::vec3(0, 2.0f * secs, 0);
      }
      GetGlobalParticles()->Update(secs);
      break;
    }
    case ClimbGameStateInEditing: {
      for (int i = 0; i < 9; i++) {
        if (keyflags.test(i)) {
          camera->pos += kDirs[i];
        }
      }
      for (int i = 0; i < cursor_sprites.size(); i++) {
        Sprite* c = cursor_sprites[i];
        c->pos.x = camera->pos.x;
        c->pos.y = camera->pos.y;
      }
      light_phase += secs;
      break;
    }
  }

  for (Platform* p : platforms) p->Update(secs);
}

void ClimbScene::do_RenderHUD(GraphicsAPI api) {
  glm::mat4 uitransform(1);

  wchar_t buf[50];
  float width;
  bool show_coin_count = false;

  // 准备阶段
  if (game_state == ClimbGameStateStartCountdown && g_main_menu_visible == false) {
    std::wstring text = std::wstring(L"准备… 第") + std::to_wstring(curr_level) + std::wstring(L"关");
    //std::wstring text = std::wstring(L"Get ready for level ") + std::to_wstring(curr_level);
    MeasureTextWidth(text, &width);

    RenderText(api, text, WIN_W / 2 - width / 2, WIN_H / 2, 0.8f,
      glm::vec3(1.0f, 1.0f, 0.2f),
      uitransform);
    show_coin_count = true;
  }
  else if (game_state == ClimbGameStateLevelEndWaitKey) {
    //std::wstring text = std::wstring(L"Level ") + std::to_wstring(curr_level) + std::wstring(L"Completed!");
    std::wstring text = std::wstring(L"关卡 ") + std::to_wstring(curr_level) + std::wstring(L" 完成！");
    MeasureTextWidth(text, &width);

    RenderText(api, text, WIN_W / 2 - width / 2, WIN_H / 2 + 32, 0.8f,
      glm::vec3(1.0f, 1.0f, 0.2f),
      uitransform);
    //swprintf(buf, 20, L"过关！");
    swprintf(buf, 50, L"时间: %.1f秒", curr_level_time);
    text = std::wstring(buf);
    MeasureTextWidth(text, &width);

    RenderText(api, text, WIN_W / 2 - width / 2, WIN_H / 2 + 64, 0.8f,
      glm::vec3(1.0f, 1.0f, 0.2f),
      uitransform);

    swprintf(buf, 50, L"请按 [空格] 继续");
    text = std::wstring(buf);
    MeasureTextWidth(text, &width);

    RenderText(api, text, WIN_W / 2 - width / 2, WIN_H / 2 + 96, 0.8f,
      glm::vec3(1.0f, 1.0f, 0.2f),
      uitransform);
    show_coin_count = true;
  }
  else if (game_state == ClimbGameStateInEditing) {
    std::wstring text = std::to_wstring(platforms.size()) + L" 个平台";
    glm::mat4 uitransform(1);
    RenderText(api, text, 32, 32, 1.0f, glm::vec3(1.0f, 1.0f, 0.2f), uitransform);

    swprintf(buf, 50, L"%.1f, %.1f", camera->pos.x, camera->pos.y);
    RenderText(api, buf, 32, 64, 1.0f, glm::vec3(1.0f, 1.0f, 0.2f), uitransform);

    const wchar_t* labels[] = {
      L"Cursor",
      L"Normal",
      L"Destructible",
      L"Coin",
      L"Exit",
    };
    float dx = 32;
    for (int i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
      glm::vec3 c = glm::vec3(1.0f, 1.0f, 0.2f);
      if (i == curr_edit_option) c = glm::vec3(1.0f, 0.3f, 0.3f);
      RenderText(api, labels[i], dx, 96, 1.0f, c, uitransform);
      float w;
      MeasureTextWidth(labels[i], &w);
      swprintf(buf, 50, L"%d", i+1);
      RenderText(api, buf, dx + w / 2 - 4, 116, 1.0f, c, uitransform);
      dx += 8 + w;
    }

    if (is_shift_down) {
      swprintf(buf, 50, L"SHIFT");
      RenderText(api, buf, dx, 116, 1.0f, glm::vec3(1,1,0.2f), uitransform);
    }
  }

  std::wstring text(buf);
  const float scale = 0.8f;
  if (show_coin_count) {
    // 金币数
    swprintf(buf, 20, L"硬币 %d/%d %.1fs", num_coins, num_coins_total, curr_level_time);
    RenderText(api, text, 32, 32, scale, glm::vec3(1.0f, 1.0f, 0.2f), uitransform);
  }

  bool SHOW_KEYSTROKES = false;
  if (SHOW_KEYSTROKES) {
    text = L"按下的键：";
    const std::wstring keys[] = { L"↖", L"↗", L"↑", L"←", L"→", L"↙", L"↓", L"↘" };
    for (int i = 0; i < 9; i++) {
      if (keyflags.test(i)) text += keys[i];
      RenderText(api, text, 32, 64, 1.0f, glm::vec3(0.2f, 1.0f, 1.0f), uitransform);
    }

  }

  if (g_textmessage->IsExpired() == false)
    g_textmessage->Render(api);

  if (g_debug) {
    swprintf(buf, 20, L"state=%d", game_state);
    text = std::wstring(buf);
    RenderText(api, text, 32, WIN_H - 48, scale, glm::vec3(1.0f, 1.0f, 0.2f), uitransform);
  }
}

void ClimbScene::RenderHUD() {
  do_RenderHUD(ClimbOpenGL);
}

void ClimbScene::RenderHUD_D3D11() {
  do_RenderHUD(ClimbD3D11);
}

void ClimbScene::RenderHUD_D3D12() {
  do_RenderHUD(ClimbD3D12);
}

void ClimbScene::OnKeyPressed(char k) {
  bool should_step = !g_main_menu_visible;

  // Editing
  if (game_state == ClimbGameStateInEditing) {
    if (k >= '1' && k <= '5') {
      int blah = k - '1';
      curr_edit_option = blah;
    }
    else if (k == 'i') {
      const float x = camera->pos.x, y = camera->pos.y;
      switch (curr_edit_option) {
      case 1: {
        ChunkSprite* s = new ChunkSprite(model_platforms[0]);
        s->pos = glm::vec3(x, y, 0);
        platforms.push_back(new NormalPlatform(s));
        break;
      }
      case 2: {
        ChunkSprite* s = new ChunkSprite(model_platforms[1]);
        s->pos = glm::vec3(x, y, 0);
        platforms.push_back(new DamagablePlatform(s));
        break;
      }
      case 3:
      {
        ChunkSprite* s = new ChunkSprite(model_coin);
        s->pos = glm::vec3(x, y, 0);
        coins.push_back(s);
        break;
      }
      default: break;
      }
    }
    else if (k == 'p') {
      DumpCurrentLevelToText();
    }
    else if (k == VK_SHIFT) {
      is_shift_down = true;
    }
  }

  if (k == 'g') { 
    is_debug = !is_debug;
    debug_vel = glm::vec3(0, 0, 0);
  } else if (k >= '1' && k <= '5' && game_state == ClimbGameStateInGame) {
    if (StartLevel(k - '0')) {
      SetGameState(ClimbGameStateStartCountdown);
    }
  }
  else if (k == ' ') {
    if (game_state == ClimbGameStateLevelEndWaitKey) {
      curr_level = (curr_level + 1);
      if (curr_level > 3) curr_level = 1;
      StartLevel(curr_level);
      SetGameState(ClimbGameStateStartCountdown);
    }
  }
  
  if (is_debug) {
    if      (k == 'i') debug_vel += glm::vec3( 0, 1, 0);
    else if (k == 'k') debug_vel += glm::vec3( 0,-1, 0);
    else if (k == 'j') debug_vel += glm::vec3(-1, 0, 0);
    else if (k == 'l') debug_vel += glm::vec3( 1, 0, 0);
  }

  // 扔绳子试探
  if (game_state == ClimbGameStateInGame) {
    int idx = -999;
    if (should_step) {
      for (int i = 0; i < 9; i++) {
        if (k == kKeys[i]) {
          idx = i;
          keyflags.set(i);
          if (rope_state != Anchored)
            curr_player_rope_endpoint = PLAYER_ROPE_ENDPOINT[i];
          break;
        }
      }
    }
    
    // 开始扔绳子试探
    if (idx != -999) {
      if (rope_state == Anchored) {
        printf("Anchor %d->%d\n", anchor_levels, anchor_levels +1);
        anchor_levels ++;
        
        if (k == 'a') { // 蕩鞦韆
          player_x_thrust = -X_THRUST;
        } else if (k == 'd') {
          player_x_thrust =  X_THRUST;
        }
        
      } else {
        printf("STARTPROBE, k=%c\n", k);
        MyPlaySound(CyclimbSound::Whoosh);
        is_key_pressed = true;
        rope_state = Probing;
        probe_delta = glm::normalize(kDirs[idx]) * PROBE_DIST;
        probe_remaining_millis = PROBE_DURATION;
        anchor->pos = GetPlayerEffectivePos();
        anchor_rope_endpoint = anchor->pos;
        // ROTATE
        anchor->orientation[1] = glm::normalize(kDirs[idx]);
        anchor->orientation[0] = glm::normalize(glm::cross(kDirs[idx], glm::vec3(0, 0, 1)));
      }
    }
  }
  else if (game_state == ClimbGameStateInEditing) {
    for (int i = 0; i < 9; i++) {
      if (k == kKeys[i]) {
        keyflags.set(i);
      }
    }
  }
}

void ClimbScene::OnKeyReleased(char k) {
  if (is_debug) {
    if (k == 'i' || k == 'k') debug_vel.y = 0;
    else if (k == 'j' || k == 'l') debug_vel.x = 0;
  }

  char keys[] = { 'q', 'e', 'w', 'a', 'd', 'z', 's', 'c' };

  if (game_state == ClimbGameStateInGame) {
    int idx = -999;
    for (int i=0; i<9; i++) {
      if (keys[i] == k) { idx = i; keyflags.reset(i); break; } 
    }
    
    if (idx != -999) {
      if (rope_state != Hidden) {
        if (rope_state == Anchored) {
          
          if (k == 'a' || k == 'd') player_x_thrust = 0; // 荡秋千
          printf("AnchorLevels %d->%d\n", anchor_levels, anchor_levels-1);
          anchor_levels --;
          if (anchor_levels <= 0) {
            anchor_levels = 0;
            player->vel += RELEASE_THRUST;
            HideRope();
          }
        }
        is_key_pressed = false;
      }
    }
  }
  else if (game_state == ClimbGameStateInEditing) {
    for (int i = 0; i < 9; i++) {
      if (keys[i] == k) { keyflags.reset(i); break; }
    }
    if (k == VK_SHIFT) {
      is_shift_down = false;
    }
  }
}

void ClimbScene::SpawnPlayer() {
  player->pos = GetPlayerInitPos();
  player->vel = GetPlayerInitVel();
  player->orientation = glm::mat3(1);
  player->omega = glm::vec3(0);
  HideRope();
  keyflags.reset();
}

glm::vec3 ClimbScene::GetPlayerInitPos() {
  return glm::vec3(0, -90, PLAYER_DISP_DELTAZ);
}

glm::vec3 ClimbScene::GetPlayerInitVel() {
  return glm::vec3(0, 250, 0);
}

glm::vec3 ClimbScene::GetPlayerEffectivePos() {
  glm::vec3 ret = player->pos;
  ret.z = 0;
  return ret;
}

glm::vec3 ClimbScene::GetPlayerEffectiveRopeEndpoint() {
  glm::vec3 ret = player->GetWorldCoord(
    curr_player_rope_endpoint + player->anchor * player->scale);
  ret.z = 0;
  return ret;
}

void ClimbScene::SetAnchorPoint(glm::vec3 anchor_p, glm::vec3 anchor_dir) {
  anchor_rope_endpoint = anchor_p - glm::normalize(anchor_dir) * ANCHOR_LEN;
  anchor->pos = anchor_p;
  printf("[SetAnchorPoint]\n");
}

void ClimbScene::LoadLevelData() {
  std::vector<std::string> data = ReadLinesFromFile("cyclimb_levels.txt");
  printf("Level data has %lu lines\n", data.size());
  
  LevelData level;
  int curr_level = 0;
  for (std::string line : data) {
    std::vector<std::string> l = SplitStringBySpace(line);
    if (l[0] == "level") {
      curr_level = std::stoi(l[1]);
      if (!level.Empty()) {
        levels.push_back(level);
        level = LevelData();
      }
    }
    else if (l[0] == "bgid") {
      level.bgid = std::stoi(l[1]);
    }
    else if (l.size() == 3) {
      float x = std::stof(l[1]), y = std::stof(l[2]);
      level.AddEntry(x, y, l[0]);
    }
  }
  if (!level.Empty()) {
    levels.push_back(level);
  }
  printf("%zd levels\n", levels.size());
}

void ClimbScene::do_StartLevel(LevelData& ldata) {
  curr_bgid = 0;
  curr_level_time = 0;
  is_all_rockets_collected = false;
  num_coins = num_coins_total = 0;

  // 删除物件
  for (Platform* s : platforms) { s->marked_for_removal = true; }
  for (Sprite* c : coins) { c->marked_for_removal = true; }
  coins.clear();
  platforms.clear();
  GetGlobalParticles()->DeleteAll();

  for (LevelData::PlatformEntry entry : ldata.entries) {
    if (entry.tag == "plat0") {
      ChunkSprite* s = new ChunkSprite(model_platforms[0]);
      s->pos = glm::vec3(entry.x, entry.y, 0);
      platforms.push_back(new NormalPlatform(s));
      printf("plat0 at (%g,%g)\n", entry.x, entry.y);
    }
    else if (entry.tag == "damagable0") {
      ChunkSprite* s = new ChunkSprite(model_platforms[1]);
      s->pos = glm::vec3(entry.x, entry.y, 0);
      platforms.push_back(new DamagablePlatform(s));
      printf("damagable0 at (%g,%g)\n", entry.x, entry.y);
    }
    else if (entry.tag == "coin") {
      ChunkSprite* s = new ChunkSprite(model_coin);
      s->pos = glm::vec3(entry.x, entry.y, 0);
      coins.push_back(s);
      printf("coin at (%g,%g)\n", entry.x, entry.y);
      num_coins++;
      num_coins_total++;
    }
    else if (entry.tag == "exit") {
      ChunkSprite* s = new ChunkSprite(model_exit);
      s->pos = glm::vec3(entry.x, entry.y, 0);
      platforms.push_back(new ExitPlatform(s));
      printf("exit at (%g,%g)\n", entry.x, entry.y);
    }
  }

  initial_coins = coins;

  SetBackground(ldata.bgid);

  is_key_pressed = false;
  anchor_levels = 0;
  HideRope();
  SpawnPlayer();
  SetGameState(ClimbGameStateNotStarted);
  ComputeCamBB();
}

bool ClimbScene::StartLevel(int levelid) {
  printf("StartLevel %d\n", levelid);
  int idx = levelid - 1;
  if (idx < 0 || idx >= levels.size()) return false;

  curr_level = idx+1; // 1-based
  
  // Platform sprite
  LevelData& ldata = levels.at(idx);
  do_StartLevel(ldata);
  
  return true;
}

void ClimbScene::StartEditingLevel() {
  do_StartLevel(editing_level);
}

void ClimbScene::DumpCurrentLevelToText() {
  for (Platform* p : platforms) {
    if (dynamic_cast<NormalPlatform*>(p)) {
      printf("plat0 %g %g\n", p->sprite->pos.x, p->sprite->pos.y);
    }
    else if (dynamic_cast<DamagablePlatform*>(p)) {
      printf("damagable0 %g %g\n", p->sprite->pos.x, p->sprite->pos.y);
    }
    else if (dynamic_cast<ExitPlatform*>(p)) {
      printf("exit %g %g\n", p->sprite->pos.x, p->sprite->pos.y);
    }
  }
  for (Sprite* coin : coins) {
    printf("coin %g %g\n", coin->pos.x, coin->pos.y);
  }
}

void ClimbScene::SaveCurrentLevelToEditingLevel() {
  editing_level.entries.clear();
  for (Platform* p : platforms) {
    if (dynamic_cast<NormalPlatform*>(p)) {
      editing_level.AddEntry(p->sprite->pos.x, p->sprite->pos.y, "plat0");
    }
    else if (dynamic_cast<DamagablePlatform*>(p)) {
      editing_level.AddEntry(p->sprite->pos.x, p->sprite->pos.y, "damagable0");
    }
    else if (dynamic_cast<ExitPlatform*>(p)) {
      editing_level.AddEntry(p->sprite->pos.x, p->sprite->pos.y, "exit");
    }
  }
  for (Sprite* coin : coins) {
    editing_level.AddEntry(coin->pos.x, coin->pos.y, "coin");
  }
}

void ClimbScene::ComputeCamBB() {
  if (platforms.size() < 1) {
    const float X = 30;
    cam_aabb.lb = glm::vec3(-X, -X, -X);
    cam_aabb.ub = glm::vec3( X,  X,  X);
  } else {
    const float X = 1e20;
    cam_aabb.ub = glm::vec3(-X, -X, -X);
    cam_aabb.lb = glm::vec3( X,  X,  X);
    for (Platform* p : platforms) cam_aabb.ExpandToPoint(p->GetOriginalPos());
  }
}

void ClimbScene::SetBackground(int bgid) {
  for (ChunkSprite* s : backgrounds0) { delete s; }
  for (ChunkSprite* s : backgrounds1) { delete s; }
  backgrounds0.clear();
  backgrounds1.clear();
  
  // Background sprite
  // [1] [1] [1]
  // [0] [0] [0]
  for (int i=0; i<5; i++) {
    ChunkSprite* s = new ChunkSprite(model_backgrounds1[bgid]);
    s->scale = glm::vec3(1,1,1) * BACKGROUND_SCALE;
    backgrounds0.push_back(s);
  }
  
  for (int i=0; i<20; i++) {
    ChunkSprite* s = new ChunkSprite(model_backgrounds2[bgid]);
    s->scale = glm::vec3(1,1,1) * BACKGROUND_SCALE;
    backgrounds1.push_back(s);
  }
}

void ClimbScene::RotateCoins(float secs) {
  for (Sprite* s : coins) {
    ((ChunkSprite*)s)->RotateAroundGlobalAxis(
      glm::normalize(glm::vec3(1,1,0)), 100.0f * secs);
  }
}

void ClimbScene::CameraFollow(float secs) {
  glm::vec3 p1 = GetPlayerEffectivePos(), p0 = camera->pos;
  const float z = camera->pos.z;
  camera->pos = p1 * CAM_FOLLOW_DAMP + p0 * (1.0f - CAM_FOLLOW_DAMP);
  // 超出边界就修理一下
  if (cam_aabb.ContainsPoint(camera->pos) == false) { 
    p1 = cam_aabb.GetClosestPoint(p1); p0 = camera->pos;
    camera->pos = p1 * CAM_FOLLOW_DAMP + p0 * (1.0f - CAM_FOLLOW_DAMP);
  }
  // 不管Z
  camera->pos.z = z;
}

void ClimbScene::LayoutBackground() {
  int x_tick = int(camera->pos.x / (100 * BACKGROUND_SCALE) + 0.5);
  int y_tick = int(camera->pos.y / (100 * BACKGROUND_SCALE) + 0.5);
  
//  printf("layout CPOS=%g,%g xytick=%d,%d\n", camera->pos.x, camera->pos.y, x_tick, y_tick);
      
  // 两种背景，bg0是地平线上的，bg1是天空的
  int idx_bg0 = 0, idx_bg1 = 0;
  
  const int dx = 2, dy = 1;
  
  for (int y = y_tick - dy; y <= y_tick + dy; y++) {
    for (int x = x_tick - dx; x <= x_tick + dx; x++) {
      if (y == 0) {
        if (idx_bg0 < int(backgrounds0.size())) {
          ChunkSprite* b = backgrounds0[idx_bg0];
          b->pos.x = x * 100.0f * BACKGROUND_SCALE;
          b->pos.y = y * 100.0f * BACKGROUND_SCALE;
          b->pos.z = 0;
          sprite_render_list.push_back(b);
          idx_bg0 ++;
        }
      } else if (y > 0) {
        if (idx_bg1 < int(backgrounds1.size())) {
          ChunkSprite* b = backgrounds1[idx_bg1];
          b->pos.x = x * 100.0f * BACKGROUND_SCALE;
          b->pos.y = y * 100.0f * BACKGROUND_SCALE;
          b->pos.z = 0;
          sprite_render_list.push_back(b);
          idx_bg1 ++;
        }
      }
    }
  }
}

void ClimbScene::SetGameState(ClimbGameState gs) {
  game_state = gs;
  switch (gs) {
    case ClimbGameStateInGame: break;
    case ClimbGameStateStartCountdown: {
      countdown_millis = COUNTDOWN_MILLIS;
      break;
    }
    case ClimbGameStateLevelEndWaitKey: {
      level_finish_state.pos0 = player->pos;
      break;
    }
    case ClimbGameStateInEditing: {
      editing_level = levels.at(curr_level - 1);
      countdown_millis = 0;
      break;
    }
  }
}

bool ClimbScene::CanHideMenu() {
  switch (game_state) {
  case ClimbGameStateNotStarted:
    return false;
  default:
    return true;
  }
}

int ClimbScene::ExitPlatform::FLY_IN_DURATION = 1000;

void ClimbScene::ExitPlatform::Update(float secs) {
  switch (state) {
    case Hidden: { break; }
    case FlyingIn: { 
      float completion = 1.0f - (fly_in_end_millis - GetElapsedMillis()) * 1.0f / FLY_IN_DURATION;
      if (completion > 1.0) {
        completion = 1.0;
      }
      if (fly_in_end_millis < GetElapsedMillis()) { 
        state = Visible;
        completion = 1.0;
      }
      sprite->pos.z = 1000.0f * ((1.0f - completion) * (1.0f - completion));
      ClimbScene::instance->LayoutRocketsOnExit(sprite->pos);
      break;
    }
    case Visible: { break; }
  }
}

void ClimbScene::LayoutRocketsOnExit(const glm::vec3& x) {
  printf("LayoutRocketsOnExit(%g,%g,%g)\n", x.x, x.y, x.z);
  const float Z_NUDGE = 5;
  coins = initial_coins;
  
  // 把 coins 放在 player 周围
  glm::vec3 p0 = x + glm::vec3(0, 12, 0), p1 = p0;
  p0.x -= 20;
  p1.x += 20;
  const int NC = int(coins.size());
  for (int i=0; i<NC; i++) {
    float t = 1.0f / (NC) * (i+0.5);
    coins[i]->pos = p1 * t + p0 * (1.0f - t);
    //coins[i]->pos.z -= Z_NUDGE;
    coins[i]->orientation = glm::mat3(1.0f);
    coins[i]->vel = glm::vec3(0, 0, 0);
  }
}

void ClimbScene::ExitPlatform::FlyIn() {
  state = FlyingIn;
  fly_in_end_millis = GetElapsedMillis() + FLY_IN_DURATION;
}

void ClimbScene::RevealExit() {
  is_all_rockets_collected = true;
  for (Platform* p : platforms) {
    ExitPlatform* ep = dynamic_cast<ExitPlatform*>(p);
    if (ep) {
      ep->FlyIn();
    }
  }
}

void ClimbScene::BeginLevelCompleteSequence() {
  game_state = ClimbGameStateLevelFinishSequence;
  countdown_millis = LEVEL_FINISH_SEQ_MILLIS;
  level_finish_state.pos0 = player->pos;
  
  player->orientation = glm::mat3(1);

  // Find pos1
  level_finish_state.pos1 = glm::vec3(0, 0, 0);
  for (Platform* p : platforms) {
    ExitPlatform* ep = dynamic_cast<ExitPlatform*>(p);
    if (ep) {
      level_finish_state.pos1 = ep->GetOriginalPos() + glm::vec3(0, 12, 0);
      break;
    }
  }
}

void ClimbScene::DamagablePlatform::DoDamage(const glm::vec3& world_x) {
  ChunkSprite* cs = (ChunkSprite*)(sprite);
  if (damaged == false) {
    damaged = true;
  }
  glm::vec3 lx = cs->GetVoxelCoord(world_x);
  const int R = 3;
  cs->chunk->SetVoxelSphere(lx, R, 0);
  lx.z = 8;
  cs->chunk->SetVoxelSphere(lx, R, 0);
}

void ClimbScene::HideRope() {
  rope_state = Hidden;
  probe_remaining_millis = -999;
}

Sprite* ClimbScene::CurrentCursorSprite() {
  if (curr_edit_option >= 0 && curr_edit_option <= 4) {
    return cursor_sprites[curr_edit_option];
  } else return nullptr;
}

void ClimbScene::OnMouseMove(int mx, int my) {
  mouse_x = mx;
  mouse_y = my;
}

void ClimbScene::OnMouseDown() {
  is_dragging = true;
  drag_mouse_x0 = mouse_x;
  drag_mouse_y0 = mouse_y;
  drag_pos0 = WindowCoordToGamePlane(camera, drag_mouse_x0, drag_mouse_y0);
  dragged_sprite = hovered_sprite;
  if (dragged_sprite) {
    dragged_sprite_pos0 = dragged_sprite->pos;
  }
}

void ClimbScene::OnMouseUp() {
  is_dragging = false;
  dragged_sprite = nullptr;
}

// 2019-12-18
// 2020-01-01 ?????
#ifdef WIN32
extern ID3D11DeviceContext* g_context11;
extern ID3D11InputLayout* g_inputlayout_for_light11;
extern ID3D11Buffer* g_fsquad_for_light11, *g_perscene_cb_light11;
extern ID3D11BlendState* g_blendstate11;
extern ID3D11PixelShader* g_ps_light;
extern ID3D11VertexShader* g_vs_light;
extern ID3D11ShaderResourceView* g_gbuffer_srv11;
extern ID3D11SamplerState *g_sampler11;
void GameScene::RenderLights() {
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
#endif