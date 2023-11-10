#include "sounds.hpp"

SoundDevice::SoundDevice() {
  p_ALCDevice = alcOpenDevice(nullptr);
  if (!p_ALCDevice) {
    throw("Failed to get sound device.");
  }
  p_ALCConext = alcCreateContext(p_ALCDevice, nullptr);
  if (!p_ALCConext) {
    throw("Failed to create sound context.");
  }
  if (!alcMakeContextCurrent(p_ALCConext)) {
    throw("Could not make context current.");
  }
  const ALCchar* name = nullptr;
  if (alcIsExtensionPresent(p_ALCDevice, "ALC_ENUMERATE_ALL_EXT"))
    name = alcGetString(p_ALCDevice, ALC_ALL_DEVICES_SPECIFIER);
  if (!name || alcGetError(p_ALCDevice) != ALC_NO_ERROR)
    name = alcGetString(p_ALCDevice, ALC_DEVICE_SPECIFIER);
  printf("Opened [%s]\n", name);
}

ALuint SoundBuffer::addSoundEffect(const char* filename) {

  ALenum err, format;
  ALuint buffer;
  SNDFILE* sndfile;
  SF_INFO sfinfo;
  short* membuf;
  sf_count_t num_frames;
  ALsizei num_bytes;

  /* Open the audio file and check that it's usable. */
  sndfile = sf_open(filename, SFM_READ, &sfinfo);
  if (!sndfile)
  {
    fprintf(stderr, "Could not open audio in %s: %s\n", filename, sf_strerror(sndfile));
    return 0;
  }
  if (sfinfo.frames < 1 || sfinfo.frames >(sf_count_t)(INT_MAX / sizeof(short)) / sfinfo.channels)
  {
    fprintf(stderr, "Bad sample count in %s (%" PRId64 ")\n", filename, sfinfo.frames);
    sf_close(sndfile);
    return 0;
  }

  /* Get the sound format, and figure out the OpenAL format */
  if (sfinfo.channels == 1)
    format = AL_FORMAT_MONO16;
  else if (sfinfo.channels == 2)
    format = AL_FORMAT_STEREO16;
  else
  {
    fprintf(stderr, "Unsupported channel count: %d\n", sfinfo.channels);
    sf_close(sndfile);
    return 0;
  }

  /* Decode the whole audio file to a buffer. */
  membuf = (short*)malloc((size_t)(sfinfo.frames * sfinfo.channels) * sizeof(short));

  num_frames = sf_readf_short(sndfile, membuf, sfinfo.frames);
  if (num_frames < 1)
  {
    free(membuf);
    sf_close(sndfile);
    fprintf(stderr, "Failed to read samples in %s (%" PRId64 ")\n", filename, num_frames);
    return 0;
  }
  num_bytes = (ALsizei)(num_frames * sfinfo.channels) * (ALsizei)sizeof(short);

  /* Buffer the audio data into a new buffer object, then free the data and
   * close the file.
   */
  buffer = 0;
  alGenBuffers(1, &buffer);
  alBufferData(buffer, format, membuf, num_bytes, sfinfo.samplerate);

  free(membuf);
  sf_close(sndfile);

  /* Check if an error occured, and clean up if so. */
  err = alGetError();
  if (err != AL_NO_ERROR)
  {
    fprintf(stderr, "OpenAL Error: %s\n", alGetString(err));
    if (buffer && alIsBuffer(buffer))
      alDeleteBuffers(1, &buffer);
    return 0;
  }

  return buffer;
}

SoundSource::SoundSource() {
  alGenSources(1, &p_Source);
  alSourcef(p_Source, AL_PITCH, p_Pitch);
  alSourcef(p_Source, AL_GAIN, p_Gain);
  alSource3f(p_Source, AL_POSITION, p_Position[0], p_Position[1], p_Position[2]);
  alSource3f(p_Source, AL_VELOCITY, p_Velocity[0], p_Velocity[1], p_Velocity[2]);
  alSourcei(p_Source, AL_LOOPING, p_LoopSound);
  alSourcei(p_Source, AL_BUFFER, p_Buffer);
}

void SoundSource::LoadSoundEffects() {
  ALuint se;
  se = SoundBuffer::get()->addSoundEffect("climb/mixkit-game-ball-tap-2073.wav");
  sounds[CyclimbSound::Tap] = se;
  se = SoundBuffer::get()->addSoundEffect("climb/mixkit-arrow-whoosh-1491.wav");
  sounds[CyclimbSound::Whoosh] = se;
  se = SoundBuffer::get()->addSoundEffect("climb/mixkit-cartoon-toy-whistle-616.wav");
  sounds[CyclimbSound::Whistle] = se;
  se = SoundBuffer::get()->addSoundEffect("climb/mixkit-small-hit-in-a-game-2072.wav");
  sounds[CyclimbSound::SmallHit] = se;

  for (const auto& x : sounds) {
    printf("%d = %d\n", int(x.first), int(x.second));
  }
}

SoundSource* g_sound_source;

void InitSounds() {
  SoundDevice* sd = SoundDevice::get();
  printf("Got sound device.\n");
  g_sound_source = new SoundSource();
  g_sound_source->LoadSoundEffects();
}

void MyPlaySound(CyclimbSound s) {
  g_sound_source->Play(s);
}