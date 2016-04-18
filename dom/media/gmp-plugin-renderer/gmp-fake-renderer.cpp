/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <stdint.h>
#include <time.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <memory>
#include <assert.h>
#include <limits.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sstream>
#include <string>

#include "gmp-platform.h"
#include "gmp-video-host.h"
#include "gmp-video-encode.h"
#include "gmp-video-decode.h"
#include "gmp-video-frame-i420.h"
#include "gmp-video-frame-encoded.h"
#include "gmp-media-render.h"

#if defined(_MSC_VER)
#define PUBLIC_FUNC __declspec(dllexport)
#else
#define PUBLIC_FUNC
#endif

static int g_log_level = 4;

#ifdef MOZ_WIDGET_GONK
#include <android/log.h>
#define ANDLOG(...) __android_log_print(ANDROID_LOG_DEBUG, "GMPF", __VA_ARGS__)
#else
#define ANDLOG(...)
#endif
#define GMPLOG(l, x) do { \
        if (l <= g_log_level) { \
        const char *log_string = "unknown"; \
        if ((l >= 0) && (l <= 3)) {               \
        log_string = kLogStrings[l];            \
        } \
        std::cerr << log_string << ": " << x << std::endl; \
        } \
        ANDLOG(x); \
    } while(0)

#define GL_CRIT 0
#define GL_ERROR 1
#define GL_INFO  2
#define GL_DEBUG 3

const char* kLogStrings[] = {
  "Critical",
  "Error",
  "Info",
  "Debug"
};

#define NAMED_PIPE "/tmp/np_external_renderer"
int FIFO_HANDLE = 0;
template <class T>
std::string ConvertToString(T value) {
  std::stringstream ss;
  ss << value;
  return ss.str();
}

void SendMsg(std::string aData)
{
  GMPLOG(GL_DEBUG, "Send data, content = "<<aData);

  GMPLOG(GL_DEBUG, "Writing data to the pipe... \n");
  int num = 0;
  num = write(FIFO_HANDLE, &aData[0], aData.length() + 1);
  if (num < 0) {
    GMPLOG(GL_DEBUG, "[W] Write to pipe error ...\n");
  }
}


GMPPlatformAPI* g_platform_api = NULL;

class FakeMediaRenderer;

class FakeRenderer;
class FakeRendererVideoTask : public GMPTask {
 public:
  FakeRendererVideoTask(FakeMediaRenderer* renderer,
                        GMPVideoEncodedFrame* frame)
    : renderer_(renderer), frame_(frame) {}

  virtual void Run();
  virtual void Destroy() { delete this; }

  FakeMediaRenderer* renderer_;
  GMPVideoEncodedFrame* frame_;
  int64_t time_;
};

class FakeRendererAudioTask : public GMPTask {
 public:
  FakeRendererAudioTask(FakeMediaRenderer* renderer,
                        GMPAudioSamples* sample)
    : renderer_(renderer), audioSample_(sample) {}

  virtual void Run();
  virtual void Destroy() { delete this; }

  FakeMediaRenderer* renderer_;
  GMPAudioSamples* audioSample_;
};

class FakeMediaRenderer : public GMPMediaRenderer {
 public:
  explicit FakeMediaRenderer (GMPVideoHost* hostAPI) :
    host_ (hostAPI),
    callback_ (NULL) {}

  virtual ~FakeMediaRenderer() {
  }

  virtual void InitVideoRenderer(const GMPVideoCodec& aVideoCodecSettings,
                                 const uint8_t* aCodecSpecific,
                                 uint32_t aVideoCodecSpecificLength,
                                 int32_t aCoreCount,
                                 GMPRect aDisplayRect)
  {
    GMPLOG (GL_DEBUG, "InitVideoRenderer");
    SendMsg(std::string(__FUNCTION__));
  }

  virtual void InitAudioRenderer(const GMPAudioCodec& aAudioCodecSettings)
  {
    GMPLOG (GL_DEBUG, "InitAudioRenderer");
    SendMsg(std::string(__FUNCTION__));
  }

  virtual void SetRendererCallback(GMPMediaRendererCallback* aCallback)
  {
    GMPLOG (GL_DEBUG, "SetRendererCallback");

    callback_ = aCallback;
    if (aCallback == nullptr)
    {
      GMPLOG (GL_DEBUG, "\033[1;31m============SetRendererCallback aCallback == nullptr============\n\033[m");
    }
    //callback_->SetOverlayImageID(1);
    SendMsg(std::string(__FUNCTION__));
  }

  virtual void RenderVideoPacket(GMPVideoEncodedFrame* aInputFrame,
                                 bool aMissingFrames,
                                 const uint8_t* aCodecSpecificInfo,
                                 uint32_t aCodecSpecificInfoLength)
  {
    GMPLOG (GL_DEBUG, "RenderVideoPacket");
    g_platform_api->runonmainthread(new FakeRendererVideoTask(this, aInputFrame));
    SendMsg(std::string(__FUNCTION__));
  }

  virtual void RenderAudioPacket(GMPAudioSamples* aEncodedSamples)
  {
    GMPLOG (GL_DEBUG, "RenderAudioPacket");
    g_platform_api->runonmainthread(new FakeRendererAudioTask(this, aEncodedSamples));
    SendMsg(std::string(__FUNCTION__));
  }

  virtual void SetPlaybackRate(double aRate)
  {
    GMPLOG (GL_DEBUG, "SetPlaybackRate");
    SendMsg(std::string(__FUNCTION__));
  }

  virtual void SetVolume(double aVolume)
  {
    GMPLOG (GL_DEBUG, "SetVolume");
    SendMsg(std::string(__FUNCTION__));
  }

  virtual void SetPreservesPitch(bool aPreservesPitch)
  {
    GMPLOG (GL_DEBUG, "SetPreservesPitch");
    SendMsg(std::string(__FUNCTION__));
  }

  virtual void Shutdown()
  {
    GMPLOG (GL_DEBUG, "Shutdown");
    //PerformShutdown...

    GMPLOG (GL_DEBUG, "\033[1;31m============ShutdownComplete============\n\033[m");
    SendMsg(std::string(__FUNCTION__));
    callback_->ShutdownComplete();
  }

  virtual void Start(int64_t aStartTime)
  {
    GMPLOG (GL_DEBUG, "Start");
    //Do something for the start command.
    //...................
    //Then callback for the a/v frame.
    callback_->VideoInputDataExhausted();
    callback_->AudioInputDataExhausted();
  }

  virtual void Stop()
  {
    GMPLOG (GL_DEBUG, "Stop");
    //Do something for the stop command.
    callback_->StopComplete();
  }

  virtual void ContentChanged(int64_t aTime)
  {
    GMPLOG (GL_DEBUG, "ContentChanged");
    //Do something for the ContentChanged command.
    //...................
    //Do some logic for maybe resolution changed for the next incoming samples.
    //Then callback for the a/v frame for the new content.
    callback_->VideoInputDataExhausted();
    callback_->AudioInputDataExhausted();
  }

  virtual void SetDisplayPosition(GMPRect aDisplayRect)
  {
    GMPLOG (GL_DEBUG, "SetDisplayPosition");
  }

  void SimulateRenderVideo(GMPVideoEncodedFrame* inputFrame)
  {
    GMPLOG (GL_DEBUG, "\033[1;31m============SimulateRenderVideo============\n\033[m");
    //Perform external video rendering...

    //callback to parent for acquiring next video frame.
    callback_->VideoInputDataExhausted();
  }

  void SimulateRenderAudio(GMPAudioSamples* audioSample)
  {
    GMPLOG (GL_DEBUG, "\033[1;31m============SimulateRenderAudio============\n\033[m");
    //Perform external audio rendering...

    //callback to parent for acquiring next audio sample.
    callback_->AudioInputDataExhausted();
  }
  GMPVideoHost* host_;
  GMPMediaRendererCallback* callback_;
};

void FakeRendererVideoTask::Run() {
  renderer_->SimulateRenderVideo(frame_);
  frame_->Destroy();
}

void FakeRendererAudioTask::Run() {
  renderer_->SimulateRenderAudio(audioSample_);
  audioSample_->Destroy();
}

extern "C" {

  PUBLIC_FUNC GMPErr
  GMPInit (GMPPlatformAPI* aPlatformAPI) {
    GMPLOG (GL_DEBUG, "GMPInit");
    g_platform_api = aPlatformAPI;
    int status;
    status = mkfifo(NAMED_PIPE, 0666);
    if (status < 0) {
      if (errno == EEXIST) {
          GMPLOG(GL_DEBUG, "create named pipe failed - existing!");
      } else {
          GMPLOG(GL_DEBUG, "create named pipe failed - unknown!, errno = "<<errno);
          unlink(NAMED_PIPE);
          return GMPGenericErr;
      }
    }
    FIFO_HANDLE = open(NAMED_PIPE, O_WRONLY);
    if (FIFO_HANDLE < 0) {
      GMPLOG(GL_DEBUG, "open named pipe failed !\n");
      return GMPGenericErr;
    }
    return GMPNoErr;
  }

  PUBLIC_FUNC GMPErr
  GMPGetAPI (const char* aApiName, void* aHostAPI, void** aPluginApi) {
    GMPLOG (GL_DEBUG,
            aApiName);
    if (!strcmp (aApiName, GMP_API_MEDIA_RENDERER)) {
      *aPluginApi = new FakeMediaRenderer (static_cast<GMPVideoHost*> (aHostAPI));
      return GMPNoErr;
    }
    return GMPGenericErr;
  }

  PUBLIC_FUNC void
  GMPShutdown (void) {
    g_platform_api = NULL;
    close(FIFO_HANDLE);
    unlink(NAMED_PIPE);
  }

} // extern "C"
