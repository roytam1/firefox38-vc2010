/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MediaKeySystemAccess.h"
#include "mozilla/dom/MediaKeySystemAccessBinding.h"
#include "mozilla/Preferences.h"
#include "nsContentTypeParser.h"
#ifdef MOZ_FMP4
#include "MP4Decoder.h"
#endif
#ifdef XP_WIN
#include "mozilla/WindowsVersion.h"
#include "WMFDecoderModule.h"
#endif
#include "nsContentCID.h"
#include "nsServiceManagerUtils.h"
#include "mozIGeckoMediaPluginService.h"
#include "VideoUtils.h"
#include "mozilla/Services.h"
#include "nsIObserverService.h"
#include "mozilla/EMEUtils.h"
#include "GMPUtils.h"
#include "gmp-audio-decode.h"
#include "gmp-video-decode.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(MediaKeySystemAccess,
                                      mParent)
NS_IMPL_CYCLE_COLLECTING_ADDREF(MediaKeySystemAccess)
NS_IMPL_CYCLE_COLLECTING_RELEASE(MediaKeySystemAccess)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MediaKeySystemAccess)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

MediaKeySystemAccess::MediaKeySystemAccess(nsPIDOMWindow* aParent,
                                           const nsAString& aKeySystem)
  : mParent(aParent)
  , mKeySystem(aKeySystem)
{
}

MediaKeySystemAccess::~MediaKeySystemAccess()
{
}

JSObject*
MediaKeySystemAccess::WrapObject(JSContext* aCx)
{
  return MediaKeySystemAccessBinding::Wrap(aCx, this);
}

nsPIDOMWindow*
MediaKeySystemAccess::GetParentObject() const
{
  return mParent;
}

void
MediaKeySystemAccess::GetKeySystem(nsString& aRetVal) const
{
  aRetVal = mKeySystem;
}

already_AddRefed<Promise>
MediaKeySystemAccess::CreateMediaKeys(ErrorResult& aRv)
{
  nsRefPtr<MediaKeys> keys(new MediaKeys(mParent, mKeySystem));
  return keys->Init(aRv);
}

static bool
HaveGMPFor(mozIGeckoMediaPluginService* aGMPService,
           const nsCString& aKeySystem,
           const nsCString& aAPI,
           const nsCString& aTag = EmptyCString())
{
  nsTArray<nsCString> tags;
  tags.AppendElement(aKeySystem);
  if (!aTag.IsEmpty()) {
    tags.AppendElement(aTag);
  }
  bool hasPlugin = false;
  if (NS_FAILED(aGMPService->HasPluginForAPI(aAPI,
                                             &tags,
                                             &hasPlugin))) {
    return false;
  }
  return hasPlugin;
}

static MediaKeySystemStatus
EnsureMinCDMVersion(mozIGeckoMediaPluginService* aGMPService,
                    const nsAString& aKeySystem,
                    int32_t aMinCdmVersion)
{
  if (aMinCdmVersion == NO_CDM_VERSION) {
    return MediaKeySystemStatus::Available;
  }

  nsTArray<nsCString> tags;
  tags.AppendElement(NS_ConvertUTF16toUTF8(aKeySystem));
  nsAutoCString versionStr;
  if (NS_FAILED(aGMPService->GetPluginVersionForAPI(NS_LITERAL_CSTRING(GMP_API_DECRYPTOR),
                                                    &tags,
                                                    versionStr)) &&
      // XXX to be removed later in bug 1147692
      NS_FAILED(aGMPService->GetPluginVersionForAPI(NS_LITERAL_CSTRING(GMP_API_DECRYPTOR_COMPAT),
                                                    &tags,
                                                    versionStr))) {
    return MediaKeySystemStatus::Error;
  }

  nsresult rv;
  int32_t version = versionStr.ToInteger(&rv);
  if (NS_FAILED(rv) || version < 0 || aMinCdmVersion > version) {
    return MediaKeySystemStatus::Cdm_insufficient_version;
  }

  return MediaKeySystemStatus::Available;
}

/* static */
MediaKeySystemStatus
MediaKeySystemAccess::GetKeySystemStatus(const nsAString& aKeySystem,
                                         int32_t aMinCdmVersion)
{
  MOZ_ASSERT(Preferences::GetBool("media.eme.enabled", false));
  nsCOMPtr<mozIGeckoMediaPluginService> mps =
    do_GetService("@mozilla.org/gecko-media-plugin-service;1");
  if (NS_WARN_IF(!mps)) {
    return MediaKeySystemStatus::Error;
  }

  if (aKeySystem.EqualsLiteral("org.w3.clearkey")) {
    if (!Preferences::GetBool("media.eme.clearkey.enabled", true)) {
      return MediaKeySystemStatus::Cdm_disabled;
    }
    if (!HaveGMPFor(mps,
                    NS_LITERAL_CSTRING("org.w3.clearkey"),
                    NS_LITERAL_CSTRING(GMP_API_DECRYPTOR))) {
      return MediaKeySystemStatus::Cdm_not_installed;
    }
    return EnsureMinCDMVersion(mps, aKeySystem, aMinCdmVersion);
  }

#ifdef XP_WIN
  if ((aKeySystem.EqualsLiteral("com.adobe.access") ||
       aKeySystem.EqualsLiteral("com.adobe.primetime"))) {
    // Win Vista and later only.
    if (!IsVistaOrLater()) {
      return MediaKeySystemStatus::Cdm_not_supported;
    }
    if (!Preferences::GetBool("media.gmp-eme-adobe.enabled", false)) {
      return MediaKeySystemStatus::Cdm_disabled;
    }
    if ((!WMFDecoderModule::HasH264() || !WMFDecoderModule::HasAAC()) ||
        !EMEVoucherFileExists()) {
      // The system doesn't have the codecs that Adobe EME relies
      // on installed, or doesn't have a voucher for the plugin-container.
      // Adobe EME isn't going to work, so don't advertise that it will.
      return MediaKeySystemStatus::Cdm_not_supported;
    }
    if (!HaveGMPFor(mps,
                    NS_ConvertUTF16toUTF8(aKeySystem),
                    NS_LITERAL_CSTRING(GMP_API_DECRYPTOR)) &&
        // XXX to be removed later in bug 1147692
        !HaveGMPFor(mps,
                    NS_ConvertUTF16toUTF8(aKeySystem),
                    NS_LITERAL_CSTRING(GMP_API_DECRYPTOR_COMPAT))) {
      return MediaKeySystemStatus::Cdm_not_installed;
    }
    return EnsureMinCDMVersion(mps, aKeySystem, aMinCdmVersion);
  }
#endif

  return MediaKeySystemStatus::Cdm_not_supported;
}

static bool
GMPDecryptsAndDecodesAAC(mozIGeckoMediaPluginService* aGMPS,
                         const nsAString& aKeySystem)
{
  MOZ_ASSERT(HaveGMPFor(aGMPS,
                        NS_ConvertUTF16toUTF8(aKeySystem),
                        NS_LITERAL_CSTRING(GMP_API_DECRYPTOR)));
  return HaveGMPFor(aGMPS,
                    NS_ConvertUTF16toUTF8(aKeySystem),
                    NS_LITERAL_CSTRING(GMP_API_AUDIO_DECODER),
                    NS_LITERAL_CSTRING("aac"));
}

static bool
GMPDecryptsAndDecodesH264(mozIGeckoMediaPluginService* aGMPS,
                          const nsAString& aKeySystem)
{
  MOZ_ASSERT(HaveGMPFor(aGMPS,
                        NS_ConvertUTF16toUTF8(aKeySystem),
                        NS_LITERAL_CSTRING(GMP_API_DECRYPTOR)));
  return HaveGMPFor(aGMPS,
                    NS_ConvertUTF16toUTF8(aKeySystem),
                    NS_LITERAL_CSTRING(GMP_API_AUDIO_DECODER),
                    NS_LITERAL_CSTRING("aac"));
}

// If this keysystem's CDM explicitly says it doesn't support decoding,
// that means it's OK with passing the decrypted samples back to Gecko
// for decoding. Note we special case Clearkey on Windows, where we need
// to check for whether WMF is usable because the CDM uses that
// to decode.
static bool
GMPDecryptsAndGeckoDecodesH264(mozIGeckoMediaPluginService* aGMPService,
                               const nsAString& aKeySystem,
                               const nsAString& aContentType)
{
  MOZ_ASSERT(HaveGMPFor(aGMPService,
                        NS_ConvertUTF16toUTF8(aKeySystem),
                        NS_LITERAL_CSTRING(GMP_API_DECRYPTOR)));
  MOZ_ASSERT(IsH264ContentType(aContentType));
  return
    (!HaveGMPFor(aGMPService,
                 NS_ConvertUTF16toUTF8(aKeySystem),
                 NS_LITERAL_CSTRING(GMP_API_VIDEO_DECODER),
                 NS_LITERAL_CSTRING("h264"))
#ifdef XP_WIN
    // Clearkey on Windows XP can't decode, but advertises that it can
    // in its GMP info file.
    || (aKeySystem.EqualsLiteral("org.w3.clearkey") && !IsVistaOrLater())
#endif
    ) && MP4Decoder::CanHandleMediaType(aContentType);
}

static bool
GMPDecryptsAndGeckoDecodesAAC(mozIGeckoMediaPluginService* aGMPService,
                              const nsAString& aKeySystem,
                              const nsAString& aContentType)
{
  MOZ_ASSERT(HaveGMPFor(aGMPService,
                        NS_ConvertUTF16toUTF8(aKeySystem),
                        NS_LITERAL_CSTRING(GMP_API_DECRYPTOR)));
  MOZ_ASSERT(IsAACContentType(aContentType));
  return
    (!HaveGMPFor(aGMPService,
                 NS_ConvertUTF16toUTF8(aKeySystem),
                 NS_LITERAL_CSTRING(GMP_API_AUDIO_DECODER),
                 NS_LITERAL_CSTRING("aac"))
#ifdef XP_WIN
    // Clearkey on Windows XP can't decode, but advertises that it can
    // in its GMP info file.
    || (aKeySystem.EqualsLiteral("org.w3.clearkey") && !IsVistaOrLater())
#endif
    ) && MP4Decoder::CanHandleMediaType(aContentType);
}

static bool
IsSupported(mozIGeckoMediaPluginService* aGMPService,
            const nsAString& aKeySystem,
            const MediaKeySystemOptions& aConfig)
{
  if (!aConfig.mInitDataType.EqualsLiteral("cenc")) {
    return false;
  }
  if (!aConfig.mAudioCapability.IsEmpty() ||
      !aConfig.mVideoCapability.IsEmpty()) {
    // Don't support any capabilities until we know we have a CDM with
    // capabilities...
    return false;
  }
 if (!aConfig.mAudioType.IsEmpty() &&
      (!IsAACContentType(aConfig.mAudioType) ||
       (!GMPDecryptsAndDecodesAAC(aGMPService, aKeySystem) &&
        !GMPDecryptsAndGeckoDecodesAAC(aGMPService, aKeySystem, aConfig.mAudioType)))) {
    return false;
  }
  if (!aConfig.mVideoType.IsEmpty() &&
      (!IsH264ContentType(aConfig.mVideoType) ||
       (!GMPDecryptsAndDecodesH264(aGMPService, aKeySystem) &&
        !GMPDecryptsAndGeckoDecodesH264(aGMPService, aKeySystem, aConfig.mVideoType)))) {
    return false;
  }
  return true;
}

/* static */
bool
MediaKeySystemAccess::IsSupported(const nsAString& aKeySystem,
                                  const Sequence<MediaKeySystemOptions>& aOptions)
{
  nsCOMPtr<mozIGeckoMediaPluginService> mps =
    do_GetService("@mozilla.org/gecko-media-plugin-service;1");
  if (NS_WARN_IF(!mps)) {
    return false;
  }

  if (!HaveGMPFor(mps,
                  NS_ConvertUTF16toUTF8(aKeySystem),
                  NS_LITERAL_CSTRING(GMP_API_DECRYPTOR))) {
    return false;
  }

  for (size_t i = 0; i < aOptions.Length(); i++) {
    const MediaKeySystemOptions& config = aOptions[i];
    if (mozilla::dom::IsSupported(mps, aKeySystem, config)) {
      return true;
    }
  }
  return false;
}

/* static */
void
MediaKeySystemAccess::NotifyObservers(nsIDOMWindow* aWindow,
                                      const nsAString& aKeySystem,
                                      MediaKeySystemStatus aStatus)
{
  RequestMediaKeySystemAccessNotification data;
  data.mKeySystem = aKeySystem;
  data.mStatus = aStatus;
  nsAutoString json;
  data.ToJSON(json);
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(aWindow, "mediakeys-request", json.get());
  }
}

} // namespace dom
} // namespace mozilla
