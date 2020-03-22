#ifndef _GAME_HPP_
#define _GAME_HPP_

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "chunkindex.hpp"
#include "sprite.hpp"
#include "util.hpp"
#include "camera.hpp"

#include <GLFW/glfw3.h>

#include <vector>
#include <string>
#include <DirectXMath.h>
#undef max
#undef min

extern ID3D11ShaderResourceView* g_helpinfo_srv11;

//
class Particles {
public:
  Particles();
  void SpawnDefaultSprite(const glm::vec3& pos, float lifetime, float v0);
  void Spawn(ChunkIndex* src, const glm::vec3& pos, float lifetime, float v0);
  struct State {
    ChunkSprite* sprite;
    float lifetime, lifetime_full;
  };
  std::vector<State> particles;
  void Update(float secs);
  static void InitStatic(ChunkIndex* x);
  static ChunkIndex* default_particle;
};

class MainMenu {
public:
  enum MenuItemType {
    Text,
    Selectable,
    Toggle
  };
  class MenuItem {
  public:
    MenuItemType type;
    std::wstring text;
    std::vector<std::wstring> choices;
    int choice_idx;

    enum ValueType {
      ValueTypeBool,
      ValueTypeInt,
    };
    ValueType valuetype;
    typedef union {
      int asInt;
      bool asBool;
    } MenuChoiceValue ;
    std::vector<MenuChoiceValue> values;
    union {
      bool* pBool;
      int* pInt;
    } ptr;

    MenuItem(const wchar_t* _text) {
      type = Selectable;
      text = _text;
      choice_idx = 0;
    }

    std::wstring GetTextForDisplay() {
      std::wstring ret = text;
      if (type == Toggle) {
        if (choices.empty()) { }
        else ret = ret + L" <" + choices[choice_idx] + L">";
      }
      return ret;
    }
  };

  MainMenu();
  MenuItem GetMenuItem(const char*);

  std::vector<std::wstring> menutitle;
  std::vector<MenuItem> menuitems;

  void Render(const glm::mat4& uitransform);
  void Render_D3D11(const glm::mat4& uitransform);
  void OnUpDownPressed(int delta); // -1: up;  +1: down
  void OnLeftRightPressed(int delta); // -1: left; +1: right
  void OnEscPressed();
  void OnEnter();
  void EnterMenu(const int idx);
  void ExitMenu();
  bool IsInHelp() { return is_in_help; }

  Camera* cam_helpinfo;
  FullScreenQuad* fsquad;

  // Dynamic Sprites in Help Screen
  std::vector<Sprite*> sprites_helpinfo;
  std::vector<D3D11_VIEWPORT> viewports11_helpinfo;

  std::vector<int> curr_selection, curr_menu;
  bool is_in_help;
  unsigned fade_millis0, fade_millis1, fade_millis;
  float fade_alpha0, fade_alpha1, fade_alpha;
  static int FADE_DURATION;
  void FadeInHelpScreen();
  void FadeOutHelpScreen();
  void Update(float delta_secs);

  static void InitStatic(unsigned p) { program = p; }
  static unsigned program;
};

class TextMessage {
public:
  std::vector<std::wstring> messages;
  unsigned millis_expire;
  TextMessage() { millis_expire = 0; }
  void SetMessage(const std::wstring& _msg, float seconds) {
    unsigned elapsed = GetElapsedMillis();
    millis_expire = (elapsed + seconds * 1000);
    messages.clear();
    messages.push_back(_msg);
  }
  void AppendLine(const std::wstring& _msg) {
    messages.push_back(_msg);
  }
  bool IsExpired() {
    int x = int(GetElapsedMillis());
    return x >= millis_expire;
  }
  void Render();
  static void InitStatic(unsigned p) { program = p; }
  static unsigned program;
};

#endif
