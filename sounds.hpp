// Reference: https://www.youtube.com/watch?v=kWQM1iQ1W0E
#pragma once

#include <inttypes.h>
#include <stdio.h>
#include <unordered_map>
#include <vector>

#include <AL/alc.h>
#include <AL/alext.h>
#include <sndfile.h>

enum class CyclimbSound {
  Tap,
  SmallHit,
  Whoosh,
  Whistle,
};

class SoundDevice {
public:
  static SoundDevice* get() {
    static SoundDevice* snd_device = new SoundDevice();
    return snd_device;
  }
  SoundDevice();
  ALCdevice* p_ALCDevice;
  ALCcontext* p_ALCConext;
};

class SoundBuffer {
public:
  static SoundBuffer* get() {
    static SoundBuffer* sndbuf = new SoundBuffer();
    return sndbuf;
  }
  SoundBuffer() {
    p_SoundEffectBuffers.clear();
  }
  ~SoundBuffer() {
    alDeleteBuffers(p_SoundEffectBuffers.size(), p_SoundEffectBuffers.data());
    p_SoundEffectBuffers.clear();
  }
  ALuint addSoundEffect(const char* filename);
  bool removeSoundEffect(const ALuint& buffer) {
    for (auto it = p_SoundEffectBuffers.begin(); it != p_SoundEffectBuffers.end(); it++) {
      if (*it == buffer) {
        alDeleteBuffers(1, &(*it));
        p_SoundEffectBuffers.erase(it);
        return true;
      }
    }
    return false;
  }

  std::vector<ALuint> p_SoundEffectBuffers;
};

class SoundSource {
public:
  SoundSource();
  ~SoundSource() {
    alDeleteSources(1, &p_Source);
  }
  void do_Play(const ALuint buffer_to_play) {
    if (buffer_to_play != p_Buffer) {
      alSourceStop(p_Source);
      p_Buffer = buffer_to_play;
      alSourcei(p_Source, AL_BUFFER, p_Buffer);
    }
    alSourcePlay(p_Source);
    printf("Started playing %u\n", p_Buffer);
    /*
    ALint state = AL_PLAYING;
    while (state == AL_PLAYING && alGetError() == AL_NO_ERROR) {
      alGetSourcei(p_Source, AL_SOURCE_STATE, &state);
    }
    printf("Done playing\n");
    */
  }
  void Play(CyclimbSound s) {
    if (sounds.count(s) > 0) {
      do_Play(sounds.at(s));
    }
  }
  void LoadSoundEffects();
private:
  ALuint p_Source;
  float p_Pitch = 1.0f;
  float p_Gain = 1.0f;
  float p_Position[3] = { 0,0,0 };
  float p_Velocity[3] = { 0,0,0 };
  bool p_LoopSound = false;
  ALuint p_Buffer = 0;
  std::unordered_map<CyclimbSound, ALuint> sounds;
};

void InitSounds();
void MyPlaySound(CyclimbSound s);