/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PlatformDecoderModule.h"

#ifdef XP_WIN
#include "WMFDecoderModule.h"
#endif
#ifdef MOZ_FFMPEG
#include "FFmpegRuntimeLinker.h"
#endif
#ifdef MOZ_APPLEMEDIA
#include "AppleDecoderModule.h"
#endif
#ifdef MOZ_GONK_MEDIACODEC
#include "GonkDecoderModule.h"
#endif
#ifdef MOZ_WIDGET_ANDROID
#include "AndroidDecoderModule.h"
#endif
#include "GMPDecoderModule.h"

#include "mozilla/Preferences.h"
#include "mozilla/WindowsVersion.h"
#ifdef MOZ_EME
#include "EMEDecoderModule.h"
#include "mozilla/CDMProxy.h"
#endif
#include "SharedThreadPool.h"
#include "MediaTaskQueue.h"

#include "MediaInfo.h"
#include "H264Converter.h"

namespace mozilla {

extern already_AddRefed<PlatformDecoderModule> CreateBlankDecoderModule();

bool PlatformDecoderModule::sUseBlankDecoder = false;
#ifdef MOZ_FFMPEG
bool PlatformDecoderModule::sFFmpegDecoderEnabled = false;
#endif
#ifdef MOZ_GONK_MEDIACODEC
bool PlatformDecoderModule::sGonkDecoderEnabled = false;
#endif
#ifdef MOZ_WIDGET_ANDROID
bool PlatformDecoderModule::sAndroidMCDecoderEnabled = false;
bool PlatformDecoderModule::sAndroidMCDecoderPreferred = false;
#endif
bool PlatformDecoderModule::sGMPDecoderEnabled = false;

/* static */
void
PlatformDecoderModule::Init()
{
  MOZ_ASSERT(NS_IsMainThread());
  static bool alreadyInitialized = false;
  if (alreadyInitialized) {
    return;
  }
  alreadyInitialized = true;

  Preferences::AddBoolVarCache(&sUseBlankDecoder,
                               "media.use-blank-decoder");
#ifdef MOZ_FFMPEG
  Preferences::AddBoolVarCache(&sFFmpegDecoderEnabled,
                               "media.ffmpeg.enabled", false);
#endif
#ifdef MOZ_GONK_MEDIACODEC
  Preferences::AddBoolVarCache(&sGonkDecoderEnabled,
                               "media.gonk.enabled", false);
#endif
#ifdef MOZ_WIDGET_ANDROID
  Preferences::AddBoolVarCache(&sAndroidMCDecoderEnabled,
                               "media.android-media-codec.enabled", false);
  Preferences::AddBoolVarCache(&sAndroidMCDecoderPreferred,
                               "media.android-media-codec.preferred", false);
#endif

  Preferences::AddBoolVarCache(&sGMPDecoderEnabled,
                               "media.gmp.decoder.enabled", false);

#ifdef XP_WIN
  WMFDecoderModule::Init();
#endif
#ifdef MOZ_APPLEMEDIA
  AppleDecoderModule::Init();
#endif
}

#ifdef MOZ_EME
/* static */
already_AddRefed<PlatformDecoderModule>
PlatformDecoderModule::CreateCDMWrapper(CDMProxy* aProxy,
                                        bool aHasAudio,
                                        bool aHasVideo)
{
  bool cdmDecodesAudio;
  bool cdmDecodesVideo;
  {
    CDMCaps::AutoLock caps(aProxy->Capabilites());
    cdmDecodesAudio = caps.CanDecryptAndDecodeAudio();
    cdmDecodesVideo = caps.CanDecryptAndDecodeVideo();
  }

  nsRefPtr<PlatformDecoderModule> pdm;
  if ((!cdmDecodesAudio && aHasAudio) || (!cdmDecodesVideo && aHasVideo)) {
    // The CDM itself can't decode. We need to wrap a PDM to decode the
    // decrypted output of the CDM.
    pdm = Create();
    if (!pdm) {
      return nullptr;
    }
  }

  nsRefPtr<PlatformDecoderModule> emepdm(
    new EMEDecoderModule(aProxy, pdm, cdmDecodesAudio, cdmDecodesVideo));
  return emepdm.forget();
}
#endif

/* static */
already_AddRefed<PlatformDecoderModule>
PlatformDecoderModule::Create()
{
  // Note: This runs on the decode thread.
  MOZ_ASSERT(!NS_IsMainThread());

  nsRefPtr<PlatformDecoderModule> m(CreatePDM());

  if (m && NS_SUCCEEDED(m->Startup())) {
    return m.forget();
  }
  return nullptr;
}

/* static */
already_AddRefed<PlatformDecoderModule>
PlatformDecoderModule::CreatePDM()
{
#ifdef MOZ_WIDGET_ANDROID
  if(sAndroidMCDecoderPreferred && sAndroidMCDecoderEnabled){
    nsRefPtr<PlatformDecoderModule> m(new AndroidDecoderModule());
    return m.forget();
  }
#endif
  if (sUseBlankDecoder) {
    return CreateBlankDecoderModule();
  }
#ifdef XP_WIN
  if(IsVistaOrLater()&&!Preferences::GetBool("media.ffmpeg.enabled", false)){
  nsRefPtr<PlatformDecoderModule> m(new WMFDecoderModule());
  return m.forget();
  }
#endif
#ifdef MOZ_FFMPEG
  nsRefPtr<PlatformDecoderModule> mffmpeg = FFmpegRuntimeLinker::CreateDecoderModule();
  if (mffmpeg) {
    return mffmpeg.forget();
  }
#endif
#ifdef MOZ_APPLEMEDIA
  nsRefPtr<PlatformDecoderModule> m(new AppleDecoderModule());
  return m.forget();
#endif
#ifdef MOZ_GONK_MEDIACODEC
  if (sGonkDecoderEnabled) {
    nsRefPtr<PlatformDecoderModule> m(new GonkDecoderModule());
    return m.forget();
  }
#endif
#ifdef MOZ_WIDGET_ANDROID
  if(sAndroidMCDecoderEnabled){
    nsRefPtr<PlatformDecoderModule> m(new AndroidDecoderModule());
    return m.forget();
  }
#endif
  if (sGMPDecoderEnabled) {
    nsRefPtr<PlatformDecoderModule> m(new GMPDecoderModule());
    return m.forget();
  }
  return nullptr;
}

already_AddRefed<MediaDataDecoder>
PlatformDecoderModule::CreateDecoder(const TrackInfo& aConfig,
                                     FlushableMediaTaskQueue* aTaskQueue,
                                     MediaDataDecoderCallback* aCallback,
                                     layers::LayersBackend aLayersBackend,
                                     layers::ImageContainer* aImageContainer)
{
  nsRefPtr<MediaDataDecoder> m;

  if (aConfig.GetAsAudioInfo()) {
    m = CreateAudioDecoder(*aConfig.GetAsAudioInfo(),
                           aTaskQueue,
                           aCallback);
    return m.forget();
  }

  if (!aConfig.GetAsVideoInfo()) {
    return nullptr;
  }

  if (H264Converter::IsH264(aConfig)) {
    nsRefPtr<H264Converter> h
      = new H264Converter(this,
                          *aConfig.GetAsVideoInfo(),
                          aLayersBackend,
                          aImageContainer,
                          aTaskQueue,
                          aCallback);
    const nsresult rv = h->GetLastError();
    if (NS_SUCCEEDED(rv) || rv == NS_ERROR_NOT_INITIALIZED) {
      // The H264Converter either successfully created the wrapped decoder,
      // or there wasn't enough AVCC data to do so. Otherwise, there was some
      // problem, for example WMF DLLs were missing.
      m = h.forget();
    }
  } else {
    m = CreateVideoDecoder(*aConfig.GetAsVideoInfo(),
                           aLayersBackend,
                           aImageContainer,
                           aTaskQueue,
                           aCallback);
  }
  return m.forget();
}

bool
PlatformDecoderModule::SupportsMimeType(const nsACString& aMimeType)
{
  return aMimeType.EqualsLiteral("audio/mp4a-latm") ||
         aMimeType.EqualsLiteral("video/mp4") ||
         aMimeType.EqualsLiteral("video/avc");
}

} // namespace mozilla
