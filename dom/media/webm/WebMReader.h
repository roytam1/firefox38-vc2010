/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#if !defined(WebMReader_h_)
#define WebMReader_h_

#include <stdint.h>

#include "MediaDecoderReader.h"
#include "nsAutoRef.h"
#include "nestegg/nestegg.h"

#define VPX_DONT_DEFINE_STDINT_TYPES
#include "vpx/vpx_codec.h"

#include "mozilla/layers/LayersTypes.h"

#include "NesteggPacketHolder.h"

#ifdef MOZ_TREMOR
#include "tremor/ivorbiscodec.h"
#else
#include "vorbis/codec.h"
#endif

#include "OpusParser.h"

#include "VorbisUtils.h"
#include "OggReader.h"

namespace mozilla {
static const unsigned NS_PER_USEC = 1000;
static const double NS_PER_S = 1e9;

class WebMBufferedState;
class WebMPacketQueue;

class WebMReader;

// Class to handle various video decode paths
class WebMVideoDecoder
{
public:
  virtual nsresult Init(unsigned int aWidth = 0, unsigned int aHeight = 0) = 0;
  virtual nsresult Flush() { return NS_OK; }
  virtual void Shutdown() = 0;
  virtual bool DecodeVideoFrame(bool &aKeyframeSkip,
                                int64_t aTimeThreshold) = 0;
  WebMVideoDecoder() {}
  virtual ~WebMVideoDecoder() {}
};

// Class to handle various audio decode paths
class WebMAudioDecoder
{
public:
  virtual nsresult Init() = 0;
  virtual void Shutdown() = 0;
  virtual nsresult ResetDecode() = 0;
  virtual nsresult DecodeHeader(const unsigned char* aData, size_t aLength) = 0;
  virtual nsresult FinishInit(AudioInfo& aInfo) = 0;
  virtual bool Decode(const unsigned char* aData, size_t aLength,
                      int64_t aOffset, uint64_t aTstampUsecs,
                      int64_t aDiscardPadding, int32_t* aTotalFrames) = 0;
  virtual ~WebMAudioDecoder() {}
};

class WebMReader : public MediaDecoderReader
{
public:
  explicit WebMReader(AbstractMediaDecoder* aDecoder);

protected:
  ~WebMReader();

public:
  virtual nsRefPtr<ShutdownPromise> Shutdown() override;
  virtual nsresult Init(MediaDecoderReader* aCloneDonor) override;
  virtual nsresult ResetDecode() override;
  virtual bool DecodeAudioData() override;

  virtual bool DecodeVideoFrame(bool &aKeyframeSkip,
                                int64_t aTimeThreshold) override;

  virtual bool HasAudio() override
  {
    MOZ_ASSERT(OnTaskQueue());
    return mHasAudio;
  }

  virtual bool HasVideo() override
  {
    MOZ_ASSERT(OnTaskQueue());
    return mHasVideo;
  }

  virtual nsresult ReadMetadata(MediaInfo* aInfo,
                                MetadataTags** aTags) override;
  virtual nsRefPtr<SeekPromise>
  Seek(int64_t aTime, int64_t aEndTime) override;

  virtual media::TimeIntervals GetBuffered() override;
  virtual void NotifyDataArrived(const char* aBuffer, uint32_t aLength,
                                 int64_t aOffset) override;
  virtual int64_t GetEvictionOffset(double aTime) override;

  virtual bool IsMediaSeekable() override;

  // Value passed to NextPacket to determine if we are reading a video or an
  // audio packet.
  enum TrackType {
    VIDEO = 0,
    AUDIO = 1
  };

  // Read a packet from the nestegg file. Returns nullptr if all packets for
  // the particular track have been read. Pass VIDEO or AUDIO to indicate the
  // type of the packet we want to read.
  nsRefPtr<NesteggPacketHolder> NextPacket(TrackType aTrackType);

  // Pushes a packet to the front of the video packet queue.
  virtual void PushVideoPacket(NesteggPacketHolder* aItem);

  int GetVideoCodec();
  nsIntRect GetPicture();
  nsIntSize GetInitialFrame();
  int64_t GetLastVideoFrameTime();
  void SetLastVideoFrameTime(int64_t aFrameTime);
  layers::LayersBackend GetLayersBackendType() { return mLayersBackendType; }
  FlushableMediaTaskQueue* GetVideoTaskQueue() { return mVideoTaskQueue; }
  uint64_t GetCodecDelay() { return mCodecDelay; }

protected:
  // Decode a nestegg packet of audio data. Push the audio data on the
  // audio queue. Returns true when there's more audio to decode,
  // false if the audio is finished, end of file has been reached,
  // or an un-recoverable read error has occured. The reader's monitor
  // must be held during this call. The caller is responsible for freeing
  // aPacket.
  bool DecodeAudioPacket(NesteggPacketHolder* aHolder);

  // Release context and set to null. Called when an error occurs during
  // reading metadata or destruction of the reader itself.
  void Cleanup();

  virtual nsresult SeekInternal(int64_t aTime);

  // Initializes mLayersBackendType if possible.
  void InitLayersBackendType();

  bool ShouldSkipVideoFrame(int64_t aTimeThreshold);

private:
  // Get the timestamp of keyframe greater than aTimeThreshold.
  int64_t GetNextKeyframeTime(int64_t aTimeThreshold);
  // Push the packets into aOutput which's timestamp is less than aEndTime.
  // Return false if we reach the end of stream or something wrong.
  bool FilterPacketByTime(int64_t aEndTime, WebMPacketQueue& aOutput);

  // Internal method that demuxes the next packet from the stream. The caller
  // is responsible for making sure it doesn't get lost.
  nsRefPtr<NesteggPacketHolder> DemuxPacket();

  // libnestegg context for webm container. Access on state machine thread
  // or decoder thread only.
  nestegg* mContext;

  nsAutoPtr<WebMAudioDecoder> mAudioDecoder;
  nsAutoPtr<WebMVideoDecoder> mVideoDecoder;

  // Queue of video and audio packets that have been read but not decoded. These
  // must only be accessed from the decode thread.
  WebMPacketQueue mVideoPackets;
  WebMPacketQueue mAudioPackets;

  // Index of video and audio track to play
  uint32_t mVideoTrack;
  uint32_t mAudioTrack;

  // Time in microseconds of the start of the first audio frame we've decoded.
  int64_t mAudioStartUsec;

  // Number of audio frames we've decoded since decoding began at mAudioStartMs.
  uint64_t mAudioFrames;

  // Number of microseconds that must be discarded from the start of the Stream.
  uint64_t mCodecDelay;

  // Nanoseconds to discard after seeking.
  uint64_t mSeekPreroll;

  // Calculate the frame duration from the last decodeable frame using the
  // previous frame's timestamp.  In NS.
  int64_t mLastVideoFrameTime;

  // Parser state and computed offset-time mappings.  Shared by multiple
  // readers when decoder has been cloned.  Main thread only.
  nsRefPtr<WebMBufferedState> mBufferedState;

  // Size of the frame initially present in the stream. The picture region
  // is defined as a ratio relative to this.
  nsIntSize mInitialFrame;

  // Picture region, as relative to the initial frame size.
  nsIntRect mPicture;

  // Codec ID of audio track
  int mAudioCodec;
  // Codec ID of video track
  int mVideoCodec;

  layers::LayersBackend mLayersBackendType;

  // For hardware video decoding.
  nsRefPtr<FlushableMediaTaskQueue> mVideoTaskQueue;

  // Booleans to indicate if we have audio and/or video data
  bool mHasVideo;
  bool mHasAudio;
};

class VorbisDecoder : public WebMAudioDecoder
{
public:
  nsresult Init();
  void Shutdown();
  nsresult ResetDecode();
  nsresult DecodeHeader(const unsigned char* aData, size_t aLength);
  nsresult FinishInit(AudioInfo& aInfo);
  bool Decode(const unsigned char* aData, size_t aLength,
              int64_t aOffset, uint64_t aTstampUsecs,
              int64_t aDiscardPadding, int32_t* aTotalFrames);
  explicit VorbisDecoder(WebMReader* aReader);
  ~VorbisDecoder();
private:
  nsRefPtr<WebMReader> mReader;

  // Vorbis decoder state
  vorbis_info mVorbisInfo;
  vorbis_comment mVorbisComment;
  vorbis_dsp_state mVorbisDsp;
  vorbis_block mVorbisBlock;
  int64_t mPacketCount;
};

class OpusDecoder : public WebMAudioDecoder
{
public:
  nsresult Init();
  void Shutdown();
  nsresult ResetDecode();
  nsresult DecodeHeader(const unsigned char* aData, size_t aLength);
  nsresult FinishInit(AudioInfo& aInfo);
  bool Decode(const unsigned char* aData, size_t aLength,
              int64_t aOffset, uint64_t aTstampUsecs,
              int64_t aDiscardPadding, int32_t* aTotalFrames);
  explicit OpusDecoder(WebMReader* aReader);
  ~OpusDecoder();
private:
  nsRefPtr<WebMReader> mReader;

  // Opus decoder state
  nsAutoPtr<OpusParser> mOpusParser;
  OpusMSDecoder* mOpusDecoder;
  uint16_t mSkip;        // Samples left to trim before playback.
  bool mDecodedHeader;

  // Opus padding should only be discarded on the final packet.  Once this
  // is set to true, if the reader attempts to decode any further packets it
  // will raise an error so we can indicate that the file is invalid.
  bool mPaddingDiscarded;
};

} // namespace mozilla

#endif
