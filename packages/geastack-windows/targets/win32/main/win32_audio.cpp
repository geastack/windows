// SPDX-License-Identifier: Apache-2.0
//
// win32_audio.cpp -- the XAudio2 backend for gea::platform::audio (declared in
// core/include/audio.h), shared in substance with the Xbox/UWP prototype:
//   - AudioContext / OscillatorNode -> synthesized tones on XAudio2 source voices
//     (sine / square / sawtooth / triangle), retuned live via SetFrequencyRatio.
//   - AudioSystem::playPcm          -> one-shot PCM through a source voice.
//   - AudioSystem::playFile         -> Media Foundation decode (mp3/wav/...) -> playPcm.
//   - volume / setVolume            -> mastering-voice volume.
// Links xaudio2.lib + mfplat.lib + mfreadwrite.lib + mfuuid.lib.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "audio.h"

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

using Microsoft::WRL::ComPtr;

namespace gea::platform::audio {
namespace {

constexpr int kOutputRate = 48000;
constexpr double kTwoPi = 6.283185307179586476925;

// ---- one-shot voice: self-destructs (flagged) when its buffer finishes -----
struct OneShotCallback : public IXAudio2VoiceCallback {
  std::atomic<bool> done{false};
  void STDMETHODCALLTYPE OnBufferEnd(void *) override { done.store(true); }
  void STDMETHODCALLTYPE OnStreamEnd() override {}
  void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
  void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
  void STDMETHODCALLTYPE OnBufferStart(void *) override {}
  void STDMETHODCALLTYPE OnLoopEnd(void *) override {}
  void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) override {}
};

struct OneShot {
  IXAudio2SourceVoice *voice = nullptr;
  std::vector<std::int16_t> pcm;
  std::unique_ptr<OneShotCallback> cb;
};

struct Oscillator {
  OscillatorType type = OscillatorType::Sine;
  double frequency = 440.0;
  double baseFrequency = 440.0;  // frequency the looping buffer was rendered at
  IXAudio2SourceVoice *voice = nullptr;
  std::vector<std::int16_t> buffer;  // kept alive while the voice references it
  bool started = false;
};

// ---- engine singleton -------------------------------------------------------
struct Engine {
  std::mutex mutex;
  ComPtr<IXAudio2> xaudio;
  IXAudio2MasteringVoice *master = nullptr;
  bool ready = false;
  bool mfStarted = false;
  int volumePercent = 80;
  std::chrono::steady_clock::time_point start;
  NativeAudioHandle nextHandle = 1;
  std::unordered_map<NativeAudioHandle, Oscillator> oscillators;
  std::vector<std::unique_ptr<OneShot>> oneShots;

  bool ensure() {
    if (ready) return true;
    if (FAILED(XAudio2Create(xaudio.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
    if (FAILED(xaudio->CreateMasteringVoice(&master))) { xaudio.Reset(); return false; }
    start = std::chrono::steady_clock::now();
    applyVolume();
    ready = true;
    return true;
  }
  void applyVolume() {
    if (master) master->SetVolume(static_cast<float>(std::clamp(volumePercent, 0, 100)) / 100.0f);
  }
  void sweepOneShots() {
    for (auto it = oneShots.begin(); it != oneShots.end();) {
      if ((*it)->cb && (*it)->cb->done.load()) {
        if ((*it)->voice) (*it)->voice->DestroyVoice();
        it = oneShots.erase(it);
      } else {
        ++it;
      }
    }
  }
};

Engine &engine() {
  static Engine e;
  return e;
}

WAVEFORMATEX pcmFormat(int rate, int channels) {
  WAVEFORMATEX wfx{};
  wfx.wFormatTag = WAVE_FORMAT_PCM;
  wfx.nChannels = static_cast<WORD>(channels);
  wfx.nSamplesPerSec = static_cast<DWORD>(rate);
  wfx.wBitsPerSample = 16;
  wfx.nBlockAlign = static_cast<WORD>(channels * 2);
  wfx.nAvgBytesPerSec = rate * wfx.nBlockAlign;
  wfx.cbSize = 0;
  return wfx;
}

// One integer number of cycles of the waveform at `freq` -> seamless loop.
std::vector<std::int16_t> renderWave(OscillatorType type, double freq) {
  if (freq < 1.0) freq = 1.0;
  const double samplesPerCycle = static_cast<double>(kOutputRate) / freq;
  const int cycles = std::max(1, static_cast<int>(std::lround(freq / 20.0)));
  const int len = std::max(2, static_cast<int>(std::lround(cycles * samplesPerCycle)));
  std::vector<std::int16_t> out(static_cast<std::size_t>(len));
  for (int i = 0; i < len; ++i) {
    const double phase = static_cast<double>(i) / samplesPerCycle;  // in cycles
    const double frac = phase - std::floor(phase);
    double s = 0.0;
    switch (type) {
      case OscillatorType::Sine:     s = std::sin(kTwoPi * frac); break;
      case OscillatorType::Square:   s = frac < 0.5 ? 1.0 : -1.0; break;
      case OscillatorType::Sawtooth: s = 2.0 * frac - 1.0; break;
      case OscillatorType::Triangle: s = 4.0 * std::fabs(frac - 0.5) - 1.0; break;
    }
    out[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::clamp(s, -1.0, 1.0) * 30000.0);
  }
  return out;
}

// Submit `osc.buffer` as an infinite loop on a fresh source voice.
void startOscVoice(Oscillator &osc) {
  Engine &e = engine();
  osc.baseFrequency = osc.frequency;
  osc.buffer = renderWave(osc.type, osc.frequency);
  WAVEFORMATEX wfx = pcmFormat(kOutputRate, 1);
  if (FAILED(e.xaudio->CreateSourceVoice(&osc.voice, &wfx, 0, XAUDIO2_MAX_FREQ_RATIO))) {
    osc.voice = nullptr;
    return;
  }
  XAUDIO2_BUFFER buf{};
  buf.AudioBytes = static_cast<UINT32>(osc.buffer.size() * sizeof(std::int16_t));
  buf.pAudioData = reinterpret_cast<const BYTE *>(osc.buffer.data());
  buf.LoopCount = XAUDIO2_LOOP_INFINITE;
  osc.voice->SubmitSourceBuffer(&buf);
  osc.voice->Start(0);
  osc.started = true;
}

void stopOscVoice(Oscillator &osc) {
  if (osc.voice) {
    osc.voice->Stop(0);
    osc.voice->FlushSourceBuffers();
    osc.voice->DestroyVoice();
    osc.voice = nullptr;
  }
  osc.started = false;
}

Oscillator *findOsc(NativeAudioHandle h) {
  auto it = engine().oscillators.find(h);
  return it == engine().oscillators.end() ? nullptr : &it->second;
}

// Play a finished PCM block once, self-cleaning via the buffer-end callback.
bool playOneShot(std::vector<std::int16_t> pcm, int rate, int channels) {
  Engine &e = engine();
  if (!e.ensure() || pcm.empty()) return false;
  e.sweepOneShots();
  auto shot = std::make_unique<OneShot>();
  shot->pcm = std::move(pcm);
  shot->cb = std::make_unique<OneShotCallback>();
  WAVEFORMATEX wfx = pcmFormat(rate, channels);
  if (FAILED(e.xaudio->CreateSourceVoice(&shot->voice, &wfx, 0, 1.0f, shot->cb.get()))) return false;
  XAUDIO2_BUFFER buf{};
  buf.AudioBytes = static_cast<UINT32>(shot->pcm.size() * sizeof(std::int16_t));
  buf.pAudioData = reinterpret_cast<const BYTE *>(shot->pcm.data());
  buf.Flags = XAUDIO2_END_OF_STREAM;
  shot->voice->SubmitSourceBuffer(&buf);
  shot->voice->Start(0);
  e.oneShots.push_back(std::move(shot));
  return true;
}

// Decode any MF-supported file (mp3/wav/m4a/…) to interleaved 16-bit PCM.
bool decodeFile(const std::string &path, std::vector<std::int16_t> &pcm, int &rate, int &channels) {
  Engine &e = engine();
  if (!e.mfStarted) {
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return false;
    e.mfStarted = true;
  }
  const int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
  std::wstring wpath(wlen > 0 ? wlen - 1 : 0, L'\0');
  if (wlen > 0) MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen);

  ComPtr<IMFSourceReader> reader;
  if (FAILED(MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, reader.GetAddressOf()))) return false;

  ComPtr<IMFMediaType> pcmType;
  MFCreateMediaType(pcmType.GetAddressOf());
  pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
  pcmType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
  if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pcmType.Get())))
    return false;

  ComPtr<IMFMediaType> actual;
  if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, actual.GetAddressOf())))
    return false;
  UINT32 r = kOutputRate, c = 2;
  actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r);
  actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &c);
  rate = static_cast<int>(r);
  channels = static_cast<int>(c);

  for (;;) {
    DWORD flags = 0;
    ComPtr<IMFSample> sample;
    if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr,
                                  sample.GetAddressOf())))
      return false;
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    if (!sample) continue;
    ComPtr<IMFMediaBuffer> mbuf;
    if (FAILED(sample->ConvertToContiguousBuffer(mbuf.GetAddressOf()))) continue;
    BYTE *data = nullptr;
    DWORD cur = 0;
    if (SUCCEEDED(mbuf->Lock(&data, nullptr, &cur))) {
      const std::size_t n = cur / sizeof(std::int16_t);
      const auto *s = reinterpret_cast<const std::int16_t *>(data);
      pcm.insert(pcm.end(), s, s + n);
      mbuf->Unlock();
    }
  }
  return !pcm.empty();
}

}  // namespace

// ---- AudioParam (an oscillator's frequency) --------------------------------
AudioParam::AudioParam(NativeAudioHandle oscillator) : oscillator_(oscillator) {}
double AudioParam::value() const {
  std::lock_guard<std::mutex> lock(engine().mutex);
  Oscillator *o = findOsc(oscillator_);
  return o ? o->frequency : 0.0;
}
void AudioParam::setValue(double value) {
  std::lock_guard<std::mutex> lock(engine().mutex);
  Oscillator *o = findOsc(oscillator_);
  if (!o) return;
  o->frequency = value;
  if (o->voice && o->baseFrequency > 0.0)
    o->voice->SetFrequencyRatio(static_cast<float>(std::clamp(value / o->baseFrequency, 1.0 / 1024.0, 1024.0)));
}
void AudioParam::setValueAtTime(double value, double) { setValue(value); }

// ---- AudioNode / AudioDestinationNode --------------------------------------
AudioNode::AudioNode(NativeAudioHandle native) : native_(native) {}
NativeAudioHandle AudioNode::nativeId() const { return native_; }
AudioDestinationNode::AudioDestinationNode(NativeAudioHandle native) : AudioNode(native) {}

// ---- OscillatorNode ---------------------------------------------------------
OscillatorNode::OscillatorNode(NativeAudioHandle native) : AudioNode(native), frequency(native) {}
OscillatorType OscillatorNode::type() const {
  std::lock_guard<std::mutex> lock(engine().mutex);
  Oscillator *o = findOsc(nativeId());
  return o ? o->type : OscillatorType::Sine;
}
void OscillatorNode::setType(OscillatorType type) {
  std::lock_guard<std::mutex> lock(engine().mutex);
  Oscillator *o = findOsc(nativeId());
  if (!o) return;
  o->type = type;
  if (o->started) { stopOscVoice(*o); startOscVoice(*o); }  // re-render for the new shape
}
void OscillatorNode::connect(const AudioDestinationNode &) {}  // all voices route to the master voice
void OscillatorNode::start(double) {
  std::lock_guard<std::mutex> lock(engine().mutex);
  Engine &e = engine();
  if (!e.ensure()) return;
  Oscillator *o = findOsc(nativeId());
  if (o && !o->started) startOscVoice(*o);
}
void OscillatorNode::stop(double) {
  std::lock_guard<std::mutex> lock(engine().mutex);
  Oscillator *o = findOsc(nativeId());
  if (o) stopOscVoice(*o);
}

// ---- AudioContext -----------------------------------------------------------
double AudioContext::currentTime() const {
  Engine &e = engine();
  if (!e.ready) return 0.0;
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - e.start).count();
}
AudioDestinationNode AudioContext::destination() const { return AudioDestinationNode(1); }
OscillatorNode AudioContext::createOscillator() const {
  std::lock_guard<std::mutex> lock(engine().mutex);
  Engine &e = engine();
  e.ensure();
  const NativeAudioHandle h = e.nextHandle++;
  e.oscillators.emplace(h, Oscillator{});
  return OscillatorNode(h);
}

// ---- AudioSystem ------------------------------------------------------------
AudioContext AudioSystem::sharedContext() {
  engine().ensure();
  return AudioContext{};
}
int AudioSystem::volume() { return engine().volumePercent; }
void AudioSystem::setVolume(int volume_percent) {
  Engine &e = engine();
  e.volumePercent = std::clamp(volume_percent, 0, 100);
  e.applyVolume();
}
bool AudioSystem::playFile(const std::string &path) {
  std::vector<std::int16_t> pcm;
  int rate = kOutputRate, channels = 2;
  if (!decodeFile(path, pcm, rate, channels)) return false;
  return playOneShot(std::move(pcm), rate, channels);
}
bool AudioSystem::playPcm(const std::int16_t *samples, std::size_t sample_count, int sample_rate, int channels) {
  if (!samples || sample_count == 0) return false;
  return playOneShot(std::vector<std::int16_t>(samples, samples + sample_count),
                     sample_rate > 0 ? sample_rate : kOutputRate, channels > 0 ? channels : 1);
}
void AudioSystem::stopPlayback() {
  std::lock_guard<std::mutex> lock(engine().mutex);
  Engine &e = engine();
  for (auto &shot : e.oneShots) {
    if (shot->voice) { shot->voice->Stop(0); shot->voice->DestroyVoice(); shot->voice = nullptr; }
  }
  e.oneShots.clear();
  for (auto &kv : e.oscillators) stopOscVoice(kv.second);
}

}  // namespace gea::platform::audio
