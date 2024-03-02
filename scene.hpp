#include "chunk.hpp"
#include "game.hpp"
#include "camera.hpp"
#include "chunkindex.hpp"
#include "testshapes.hpp"
#include "util.hpp"

#include <vector>
#include <bitset>

extern Camera g_cam;
extern void UpdateSimpleTexturePerSceneCB(const float x, const float y, const float alpha);

class GameScene {
public:
  virtual void                  PrepareSpriteListForRender() = 0;
  virtual void                  PreRender()  = 0;
  virtual std::vector<Sprite*>* GetSpriteListForRender() = 0;
#ifdef WIN32
  virtual void                  PrepareLights() = 0;
  virtual void                  RenderLights();
  virtual void                  RenderHUD_D3D11() = 0;
  virtual void                  RenderHUD_D3D12() = 0;
#endif
  virtual void                  PostRender() = 0;
  virtual void                  RenderHUD() = 0;
  virtual void                  Update(float) = 0;
  virtual void                  OnKeyPressed(char key) { };
  virtual void                  OnKeyReleased(char key) { };
  
  Camera* camera;
  
  GameScene() : camera(&g_cam) { }
  virtual bool CanHideMenu() = 0;
};

class TestShapesScene : public GameScene {
public:
  Sprite* test_sprite, *test_background;
  FullScreenQuad* fsquad;
  ChunkSprite* global_xyz;
  void PreRender() { }
  void PostRender() { }
  std::vector<Sprite*> sprite_render_list;
  void PrepareSpriteListForRender();
  std::vector<Sprite*>* GetSpriteListForRender();
  void Update(float secs) { };
  void RenderHUD() { };
#ifdef WIN32
  void RenderHUD_D3D11() { };
  void RenderHUD_D3D12() { };
  void PrepareLights() { };
#endif
  bool CanHideMenu() {
    return true;
  }
};

class ClimbScene : public GameScene {
public:
  void RenderLights() override;
  // Static level info
  class LevelData {
  public:
    struct PlatformEntry {
      float x, y;
      std::string tag;
      PlatformEntry() : x(0), y(0), tag("") {}
    };

    int bgid;
    std::vector<PlatformEntry> entries;

    LevelData() : bgid(0) {}

    void AddEntry(float x, float y, const std::string& tag) {
      PlatformEntry e;
      e.x = x;
      e.y = y;
      e.tag = tag;
      entries.push_back(e);
    }
    bool Empty() {
      return entries.empty();
    }
    void Print() {
      printf("bgid=%d, %zu entries\n", bgid, entries.size());
    }
  };
  void LoadLevelData();
  std::vector<LevelData> levels;

  class GameObject {
  public:
    bool marked_for_removal = false;
    void Remove() {
      marked_for_removal = true;
    }
  };

  class Platform : public GameObject {
  public:
    Sprite* sprite;
    virtual Sprite* GetSpriteForDisplay() = 0;
    virtual Sprite* GetSpriteForCollision() = 0;
    virtual glm::vec3 GetOriginalPos() = 0;
    virtual void    Update(float) { }
    ~Platform() { delete sprite; }
  };
  
  class NormalPlatform : public Platform {
  public:
    NormalPlatform(Sprite* _s) {
      sprite = _s;
    }
    Sprite* GetSpriteForDisplay() { return sprite; }
    Sprite* GetSpriteForCollision() { return sprite; }
    glm::vec3 GetOriginalPos() { return sprite->pos; }
  };
  
  class DamagablePlatform : public Platform {
  public:
    bool damaged;
    DamagablePlatform(Sprite* _s) {
      sprite = _s;
      
      // 复制体素信息
      ChunkSprite* cs = (ChunkSprite*)(sprite);
      ChunkGrid* g = (ChunkGrid*)(cs->chunk);
      cs->chunk = new ChunkGrid(*g);
      
      damaged = false;
    }
    Sprite* GetSpriteForDisplay() { return sprite; }
    Sprite* GetSpriteForCollision() { return sprite; }
    glm::vec3 GetOriginalPos() { return sprite->pos; }
    void DoDamage(const glm::vec3& world_x);
  };
  
  class ExitPlatform : public Platform {
  public:
    enum State {
      Hidden,
      FlyingIn,
      Visible
    };
    State state;
    static int FLY_IN_DURATION;
    int fly_in_end_millis;
    ExitPlatform(Sprite* _s) {
      sprite = _s;
      state = Hidden;
      fly_in_end_millis = 0;
    }
    Sprite* GetSpriteForDisplay() {
      if (state != Hidden) return sprite;
      else return nullptr;
    }
    void Update(float secs);
    Sprite* GetSpriteForCollision() { 
      if (state != Visible) return nullptr;
      else return sprite;
    }
    glm::vec3 GetOriginalPos() { return sprite->pos; }
    void FlyIn();
  };

  //
  static ClimbScene* instance;
  
  // 模型
  static std::vector<ChunkGrid*> model_platforms;
  static std::vector<ChunkGrid*> model_backgrounds1, model_backgrounds2;
  static ChunkGrid* model_exit;
  static ChunkGrid* model_char;
  static ChunkGrid* unit_sq;
  static ChunkGrid* model_anchor;
  static ChunkGrid* model_coin;
  
  Sprite* player, *player_disp;
  glm::vec3 curr_player_rope_endpoint = PLAYER_ROPE_ENDPOINT[2];
  float player_x_thrust;
  int   anchor_levels; // 按键层数
  bool is_debug;
  glm::vec3 debug_vel;
  ChunkSprite* anchor;
  glm::vec3 anchor_rope_endpoint;
  
  std::vector<Platform*> platforms;
  AABB cam_aabb;
  std::vector<Sprite*> rope_segments;
  std::vector<ChunkSprite*> backgrounds0, backgrounds1;
  
  std::vector<Sprite*> coins, initial_coins;
  int num_coins, num_coins_total;
  int curr_level;
  int curr_bgid;
  float curr_level_time;
  
  static void InitStatic();
  
  // 资源
#ifdef WIN32
  static ID3D11Resource* helpinfo_res, *keys_res;
  static ID3D11ShaderResourceView* helpinfo_srv, *keys_srv;
#endif
  static const float SPRING_K; // 弹力系数
  static const float L0      ; // 绳子的初始长度
  static const float GRAVITY;  // 重力加速度
  static const float X_THRUST; // 荡秋千带来的横向加速度
  static const int   NUM_SEGMENTS; // 画的时候有几段
  static const float PROBE_DIST; // 探出的距离
  static const int   PROBE_DURATION; // 探出时所用时间
  static const float ANCHOR_LEN; // 锚的长度  [-----绳子---------]-[--锚--> ]
  static const float PLAYER_DISP_DELTAZ; // 显示玩家时的Z平移量
  static const float X_VEL_DAMP;
  static const float Y_VEL_DAMP;
  static const glm::vec3 RELEASE_THRUST; // 松开绳子时给的向上的冲量
  
  static const float CAM_FOLLOW_DAMP; // 视角跟随的阻尼系数
  static const float BACKGROUND_SCALE; // 背景放大倍数
  
  static const int   COUNTDOWN_MILLIS;        // 开始游戏之前的倒计时
  static const int   LEVEL_FINISH_SEQ_MILLIS; // 关卡完成序列的倒计时
  static const int   LEVEL_FINISH_JUMP_PERIOD;
  
  static const glm::vec3 PLAYER_ROPE_ENDPOINT[8]; // 手抓绳子的局部坐标，八个方向
  static const float INVERSE_INERTIA; // 转动惯量的倒数
  
  std::bitset<10> keyflags;
  
  bool is_key_pressed;
  
  enum RopeState {
    Hidden, Probing, Anchored, Retracting
  };
  RopeState rope_state;
  glm::vec3 probe_delta;
  
  float probe_remaining_millis;
  
  enum ClimbGameState {
    ClimbGameStateNotStarted,
    ClimbGameStateInGame,
    ClimbGameStateStartCountdown,
    ClimbGameStateLevelFinishSequence,
    ClimbGameStateLevelEndWaitKey,

    ClimbGameStateInEditing,
  };
  
  // 这个结构控制游戏状态从 ClimbGameStateLevelFinishSequene 到
  //                   ClimbGameStateLevelEndWaitKey 之间的过程。
  struct LevelFinishState {
    // for level finish
    ClimbScene* parent; // singleton
    glm::vec3 pos0, pos1;
    float GetCompletion() {
      return 1.0f - (1.0f * parent->countdown_millis / LEVEL_FINISH_SEQ_MILLIS);
    }
  };
  ClimbGameState game_state;
  float countdown_millis;
  LevelFinishState level_finish_state;
  int is_all_rockets_collected;
  
  ClimbScene();
  void Init();
  void PreRender();
  void PostRender() { }
  std::vector<Sprite*> sprite_render_list;
  void PrepareSpriteListForRender();
  std::vector<Sprite*>* GetSpriteListForRender();
  void Update(float secs);
  void RenderHUD();
  void RenderHUD_D3D11();
  void RenderHUD_D3D12();
  void do_RenderHUD(GraphicsAPI api);
  virtual void OnKeyPressed(char);
  virtual void OnKeyReleased(char);
  void SetAnchorPoint(glm::vec3 anchor_p, glm::vec3 anchor_dir);
  
  DirectionalLight* lights[16];
  float light_phase = 0.0;
  void PrepareLights();
  
  // 每帧更新时做的事情
  void RotateCoins(float secs);
  void CameraFollow(float secs);
  void LayoutBackground();
  void HideRope();
  
  glm::vec3 GetPlayerInitPos();
  glm::vec3 GetPlayerInitVel();
  glm::vec3 GetPlayerEffectivePos();          // 中心
  glm::vec3 GetPlayerEffectiveRopeEndpoint(); // 头顶
  void SpawnPlayer();
  
  bool StartLevel(int levelid);
  void ComputeCamBB(); // Cam Bounding Box
  void SetBackground(int bgid);
  void SetGameState(ClimbGameState gs);
  void RevealExit();
  void LayoutRocketsOnExit(const glm::vec3& exit_pos);
  void BeginLevelCompleteSequence();

  bool is_test_playing;  // 是否是从试玩模式进入
  Sprite* cursor_sprite;
  bool CanHideMenu();
};

class LightTestScene : public GameScene {
public:
  enum MacroState { OLD_YEAR, NEW_YEAR };
  MacroState macro_state;

  static float ACTOR_INIT_Y;
  static void InitStatic();
  static std::vector<ChunkGrid*> model_backgrounds;
  static std::map<char, ChunkGrid*> model_digits;
  ChunkSprite* bg_sprites[6];
  static ChunkGrid* model_clock;
  ChunkSprite* clock_sprite;

  ChunkSprite* digit_sprites[20];
  struct DigitState {
    char old_char, new_char;
    float tween_end_sec;
  };
  DigitState digit_states[20];
  std::string status_string;

  constexpr static int NUM_ACTORS = 16;
  ChunkSprite* char_sprites[NUM_ACTORS];
  float deltays[NUM_ACTORS];
  float total_secs;
  float last_update_total_secs;
  int last_sec;
  DirectionalLight* lights[16];

  float curr_light_angle;

  LightTestScene();

  void PreRender() { }
  void PostRender() { }
  std::vector<Sprite*> sprite_render_list;
  void PrepareSpriteListForRender();
  std::vector<Sprite*>* GetSpriteListForRender();
  void Update(float secs);
  void RenderHUD() { };
  void RenderHUD_D3D11() { };
  void RenderHUD_D3D12() { };
  void PrepareLights();

  MacroState GetMacroState(int* curr_sec);
  bool CanHideMenu() {
    return true;
  }
};
