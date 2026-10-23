#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
static unsigned frame;
static void fill(void *buffer, unsigned frames)
{
  float *out=buffer;
  for (unsigned i=0;i<frames;++i,++frame) {
    out[2*i]=0.25f*sinf(2.0f*3.14159265358979323846f*440.0f*(float)(frame%48000)/48000.0f);
    out[2*i+1]=out[2*i];
  }
}
int main(int argc, char **argv)
{
  InitAudioDevice();
  if (!IsAudioDeviceReady()) { fputs("RAYLIB_AUDIO_REFUSED\n",stderr); CloseAudioDevice(); return 1; }
  if (argc>1 && argv[1][0]=='w') {
    Wave wave=LoadWave("app://tone.wav");
    if (!IsWaveValid(wave)) return 2;
    Sound sound=LoadSoundFromWave(wave); UnloadWave(wave);
    if (!IsSoundValid(sound)) return 3;
    PlaySound(sound); struct timespec t={2,0}; nanosleep(&t,NULL);
    StopSound(sound); UnloadSound(sound);
    CloseAudioDevice(); InitAudioDevice();
    if (!IsAudioDeviceReady()) return 4;
    CloseAudioDevice(); puts("RAYLIB_WAV_REOPEN_DONE"); return IsAudioDeviceReady()?5:0;
  }
  AudioStream stream=LoadAudioStream(48000,32,2);
  if (!IsAudioStreamValid(stream)) { CloseAudioDevice(); return 6; }
  SetAudioStreamCallback(stream,fill); PlayAudioStream(stream);
  puts("RAYLIB_AUDIO_STARTED"); fflush(stdout);
  struct timespec duration={8,0}; nanosleep(&duration,NULL);
  bool ready=IsAudioDeviceReady();
  StopAudioStream(stream); UnloadAudioStream(stream); CloseAudioDevice();
  printf("RAYLIB_AUDIO_DONE frames=%u ready=%d closed=%d\n",frame,ready,!IsAudioDeviceReady());
  return ready&&!IsAudioDeviceReady()?0:7;
}
