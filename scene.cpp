#include "scene.hpp"
#include "textrender.hpp"
#include "game.hpp"
#include "util.hpp"
#define _USE_MATH_DEFINES // For MSVC
#include <math.h>
#include <assert.h>
#include <wchar.h>

extern unsigned g_fadein_complete_millis;
extern unsigned g_last_millis;
extern char g_arrow_dx, g_arrow_dy;

extern Particles* GetGlobalParticles();
extern TextMessage* g_textmessage;
extern float g_cam_rot_x, g_cam_rot_y;

GLuint GetShaderProgram(int idx);
extern int WIN_W, WIN_H;

extern bool g_main_menu_visible;

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
}

void ClimbScene::PreRender() { }

void ClimbScene::PrepareSpriteListForRender() {
  sprite_render_list.clear();
  for (Platform* p : platforms) {
    Sprite* sp = p->GetSpriteForDisplay();
    sprite_render_list.push_back(sp);
  }
  for (Sprite* s : coins)       sprite_render_list.push_back(s);
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
}

std::vector<Sprite*>* ClimbScene::GetSpriteListForRender() {
  return &sprite_render_list;
}

ClimbScene::ClimbScene() { }

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
  num_coins = 0; num_coins_total = 0;
  curr_level = 1;
  SetBackground(0);
  StartLevel(curr_level);
  
  ClimbScene::instance = this;
  
  keyflags.reset();
}

void ClimbScene::Update(float secs) {
  if (g_main_menu_visible == false) {
    countdown_millis -= secs * 1000.0f;
    if (countdown_millis < 0) countdown_millis = 0;
  }
  
  switch (game_state) {
    case ClimbGameStateInGame:
      {
        curr_level_time += secs;
        
        if (is_debug) {
          player->pos += debug_vel * (100.0f * secs);
        } else {
          player->vel += glm::vec3(0, GRAVITY * secs, 0); 
          player->Update(secs);
          player->vel.x += player_x_thrust * secs;
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
        float probe_completion = 1.0f - (int(probe_end_millis) - int(millis)) * 1.0f / PROBE_DURATION;
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
                  if (dynamic_cast<ExitPlatform*>(platforms[i])) {
                    BeginLevelCompleteSequence();
                  }
                  rope_state = Anchored;
                  probe_end_millis = 0;
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
                  if (dp) dp->DoDamage(x);
                  
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
        
        if (rope_state == Anchored) {
          glm::vec3 delta = anchor_rope_endpoint - GetPlayerEffectiveRopeEndpoint();
          if (glm::dot(delta, delta) > L0 * L0) {
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
        
        CameraFollow(secs);
        GetGlobalParticles()->Update(secs);
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
  }
  for (Platform* p : platforms) p->Update(secs);
}

void ClimbScene::do_RenderHUD(GraphicsAPI api) {

  glm::mat4 uitransform(1);

  wchar_t buf[50];
  float width;

  // 准备阶段
  if (game_state == ClimbGameStateStartCountdown && g_main_menu_visible == false) {
    std::wstring text = std::wstring(L"准备… 第") + std::to_wstring(curr_level) + std::wstring(L"关");
    //std::wstring text = std::wstring(L"Get ready for level ") + std::to_wstring(curr_level);
    MeasureTextWidth(text, &width);

    if (api == ClimbOpenGL) {
      RenderText(GetShaderProgram(6), text, WIN_W / 2 - width / 2, WIN_H / 2, 0.8f,
        glm::vec3(1.0f, 1.0f, 0.2f),
        uitransform);
    }
    else {
      RenderText_D3D11(text, WIN_W / 2 - width / 2, WIN_H / 2, 0.8f,
        glm::vec3(1.0f, 1.0f, 0.2f),
        uitransform);
    }
  }
  else if (game_state == ClimbGameStateLevelEndWaitKey) {
    //std::wstring text = std::wstring(L"Level ") + std::to_wstring(curr_level) + std::wstring(L"Completed!");
    std::wstring text = std::wstring(L"关卡 ") + std::to_wstring(curr_level) + std::wstring(L" 完成！");
    MeasureTextWidth(text, &width);

    RenderText(GetShaderProgram(6), text, WIN_W / 2 - width / 2, WIN_H / 2 + 32, 0.8f,
      glm::vec3(1.0f, 1.0f, 0.2f),
      uitransform);
    //swprintf(buf, 20, L"过关！");
    swprintf(buf, 50, L"时间: %.1f秒", curr_level_time);
    text = std::wstring(buf);
    MeasureTextWidth(text, &width);

    RenderText(GetShaderProgram(6), text, WIN_W / 2 - width / 2, WIN_H / 2 + 64, 0.8f,
      glm::vec3(1.0f, 1.0f, 0.2f),
      uitransform);

    swprintf(buf, 50, L"请按 [空格] 继续");
    text = std::wstring(buf);
    MeasureTextWidth(text, &width);

    RenderText(GetShaderProgram(6), text, WIN_W / 2 - width / 2, WIN_H / 2 + 96, 0.8f,
      glm::vec3(1.0f, 1.0f, 0.2f),
      uitransform);
  }

  // 金币数
  swprintf(buf, 20, L"硬币 %d/%d %.1fs", num_coins, num_coins_total, curr_level_time);
  std::wstring text(buf);
  const float scale = 0.8f;
  RenderText(GetShaderProgram(6), text, 32, 32, scale, glm::vec3(1.0f, 1.0f, 0.2f), uitransform);

  bool SHOW_KEYSTROKES = false;
  if (SHOW_KEYSTROKES) {
    text = L"按下的键：";
    const std::wstring keys[] = { L"↖", L"↗", L"↑", L"←", L"→", L"↙", L"↓", L"↘" };
    for (int i = 0; i < 9; i++) {
      if (keyflags.test(i)) text += keys[i];
      RenderText(GetShaderProgram(6), text, 32, 64, 1.0f, glm::vec3(0.2f, 1.0f, 1.0f), uitransform);
    }

  }

  if (g_textmessage->IsExpired() == false)
    g_textmessage->Render();
}

void ClimbScene::RenderHUD() {
}

void ClimbScene::OnKeyPressed(char k) {  
  if (k == 'g') { 
    is_debug = !is_debug;
    debug_vel = glm::vec3(0, 0, 0);
  } else if (k >= '1' && k <= '5') {
    StartLevel(k-'0');
  } //else if (k == '0') RevealExit();
  else if (k == ' ') {
    if (game_state == ClimbGameStateLevelEndWaitKey) {
      curr_level = (curr_level + 1);
      if (curr_level > 3) curr_level = 1;
      StartLevel(curr_level);
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
    char keys[] = { 'q', 'e', 'w', 'a', 'd', 'z', 's', 'c' };
    glm::vec3 dirs[] = { 
      glm::vec3(-1, 1, 0), glm::vec3(1, 1, 0), glm::vec3(0, 1, 0), // Q E W
      glm::vec3(-1, 0, 0), glm::vec3(1, 0, 0), // A D
      glm::vec3(-1,-1, 0), glm::vec3(0,-1, 0), glm::vec3(1,-1, 0), // Z S C
    };
    int idx = -999;
    for (int i=0; i<9; i++) {
      if (k == keys[i]) {
        idx = i; 
        keyflags.set(i);
        if (rope_state != Anchored)
          curr_player_rope_endpoint = PLAYER_ROPE_ENDPOINT[i];
        break;
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
        is_key_pressed = true;
        rope_state = Probing;
        probe_delta = glm::normalize(dirs[idx]) * PROBE_DIST;
        probe_end_millis = GetElapsedMillis() + PROBE_DURATION;
        anchor->pos = GetPlayerEffectivePos();
        anchor_rope_endpoint = anchor->pos;
        // ROTATE
        anchor->orientation[1] = glm::normalize(dirs[idx]);
        anchor->orientation[0] = glm::normalize(glm::cross(dirs[idx], glm::vec3(0, 0, 1)));
      }
    }
  }
}

void ClimbScene::OnKeyReleased(char k) {
  
  if (is_debug) {
    if (k == 'i' || k == 'k') debug_vel.y = 0;
    else if (k == 'j' || k == 'l') debug_vel.x = 0;
  }
  
  if (game_state == ClimbGameStateInGame) {
    char keys[] = { 'q', 'e', 'w', 'a', 'd', 'z', 's', 'c' }; 
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

void ClimbScene::StartLevel(int levelid) {
  curr_level = levelid;
  curr_bgid = 0;
  curr_level_time = 0;
  is_all_rockets_collected = false;
  
  for (Platform* s : platforms) { delete s; }
  platforms.clear();
  for (Sprite* c : coins) { delete c; }
  coins.clear();
  
  // Platform sprite
  std::vector<std::string> data = ReadLinesFromFile("cyclimb_levels.txt");
  printf("Level data has %lu lines\n", data.size());
  
  int curr_level = 0;
  num_coins = num_coins_total = 0;
  
  for (std::string line : data) {
    std::vector<std::string> l = SplitStringBySpace(line);
    if (l[0] == "level") { curr_level = std::stoi(l[1]); }
    if (curr_level == levelid) {
      if (l[0] == "plat0") {
        float x = std::stof(l[1]), y = std::stof(l[2]);
        ChunkSprite* s = new ChunkSprite(model_platforms[0]);
        s->pos = glm::vec3(x, y, 0);
        platforms.push_back(new NormalPlatform(s));
        printf("plat0 at (%g,%g)\n", x, y);
      } else if (l[0] == "damagable0") {
        float x = std::stof(l[1]), y = std::stof(l[2]);
        ChunkSprite* s = new ChunkSprite(model_platforms[1]);
        s->pos = glm::vec3(x, y, 0);
        platforms.push_back(new DamagablePlatform(s));
        printf("damagable0 at (%g,%g)\n", x, y);
      } else if (l[0] == "coin") {
        float x = std::stof(l[1]), y = std::stof(l[2]);
        ChunkSprite* s = new ChunkSprite(model_coin);
        s->pos = glm::vec3(x, y, 0);
        coins.push_back(s);
        printf("coin at (%g,%g)\n", x, y);
        num_coins ++;
        num_coins_total ++;
      } else if (l[0] == "exit") {
        float x = std::stof(l[1]), y = std::stof(l[2]);
        ChunkSprite* s = new ChunkSprite(model_exit);
        s->pos = glm::vec3(x, y, 0);
        platforms.push_back(new ExitPlatform(s));
        printf("exit at (%g,%g)\n", x, y);
      }
      else if (l[0] == "bgid") {
        curr_bgid = std::stoi(l[1]);
        SetBackground(curr_bgid);
      }
    }
  }
  
  initial_coins = coins;
  
  is_key_pressed = false;
  anchor_levels = 0;
  HideRope();
  SpawnPlayer();
  SetGameState(ClimbGameStateStartCountdown);
  ComputeCamBB();
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
  const float Z_NUDGE = 5;
  coins = initial_coins; // optimize
  
  // 把 coins 放在 player 周围
  glm::vec3 p0 = x + glm::vec3(0, 12, 0), p1 = p0;
  p0.x -= 20;
  p1.x += 20;
  const int NC = int(coins.size());
  for (int i=0; i<int(coins.size()); i++) {
    float t = 1.0f / (NC) * (i+0.5);
    coins[i]->pos = p1 * t + p0 * (1.0f - t);
    coins[i]->pos.z -= Z_NUDGE;
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
  probe_end_millis = 0;
}