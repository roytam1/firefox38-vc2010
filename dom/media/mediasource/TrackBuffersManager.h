/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_TRACKBUFFERSMANAGER_H_
#define MOZILLA_TRACKBUFFERSMANAGER_H_

#include "SourceBufferContentManager.h"
#include "MediaDataDemuxer.h"
#include "MediaSourceDecoder.h"
#include "SourceBuffer.h"
#include "mozilla/Atomics.h"
#include "mozilla/Maybe.h"
#include "mozilla/Monitor.h"
#include "mozilla/Pair.h"
#include "nsProxyRelease.h"
#include "nsTArray.h"

namespace mozilla {

class ContainerParser;
class MediaLargeByteBuffer;
class MediaRawData;
class MediaSourceDemuxer;
class SourceBufferResource;

namespace dom {
  class SourceBufferAttributes;
}

using media::TimeUnit;
using media::TimeInterval;
using media::TimeIntervals;
using dom::SourceBufferAppendMode;

class TrackBuffersManager : public SourceBufferContentManager {
public:
  typedef MediaPromise<bool, nsresult, /* IsExclusive = */ true> CodedFrameProcessingPromise;
  typedef TrackInfo::TrackType TrackType;
  typedef MediaData::Type MediaType;
  typedef nsTArray<nsRefPtr<MediaRawData>> TrackBuffer;

  TrackBuffersManager(dom::SourceBufferAttributes* aAttributes,
                      MediaSourceDecoder* aParentDecoder,
                      const nsACString& aType);

  bool AppendData(MediaLargeByteBuffer* aData,
                  TimeUnit aTimestampOffset) override;

  nsRefPtr<AppendPromise> BufferAppend() override;

  void AbortAppendData() override;

  void ResetParserState() override;

  nsRefPtr<RangeRemovalPromise> RangeRemoval(TimeUnit aStart,
                                             TimeUnit aEnd) override;

  EvictDataResult
  EvictData(TimeUnit aPlaybackTime,
            uint32_t aThreshold,
            TimeUnit* aBufferStartTime) override;

  void EvictBefore(TimeUnit aTime) override;

  TimeIntervals Buffered() override;

  int64_t GetSize() override;

  void Ended() override;

  void Detach() override;

  AppendState GetAppendState() override
  {
    return mAppendState;
  }

  void SetGroupStartTimestamp(const TimeUnit& aGroupStartTimestamp) override;
  void RestartGroupStartTimestamp() override;
  TimeUnit GroupEndTimestamp() override;

  // Interface for MediaSourceDemuxer
  MediaInfo GetMetadata();
  const TrackBuffer& GetTrackBuffer(TrackInfo::TrackType aTrack);
  const TimeIntervals& Buffered(TrackInfo::TrackType);
  bool IsEnded() const
  {
    return mEnded;
  }
  TimeUnit Seek(TrackInfo::TrackType aTrack,
                const TimeUnit& aTime,
                const TimeUnit& aFuzz);
  uint32_t SkipToNextRandomAccessPoint(TrackInfo::TrackType aTrack,
                                       const TimeUnit& aTimeThreadshold,
                                       bool& aFound);
  already_AddRefed<MediaRawData> GetSample(TrackInfo::TrackType aTrack,
                                           const TimeUnit& aFuzz,
                                           bool& aError);
  TimeUnit GetNextRandomAccessPoint(TrackInfo::TrackType aTrack);

#if defined(DEBUG)
  void Dump(const char* aPath) override;
#endif

private:
  virtual ~TrackBuffersManager();
  // All following functions run on the taskqueue.
  nsRefPtr<AppendPromise> InitSegmentParserLoop();
  void ScheduleSegmentParserLoop();
  void SegmentParserLoop();
  void AppendIncomingBuffers();
  void InitializationSegmentReceived();
  void ShutdownDemuxers();
  void CreateDemuxerforMIMEType();
  void NeedMoreData();
  void RejectAppend(nsresult aRejectValue, const char* aName);
  // Will return a promise that will be resolved once all frames of the current
  // media segment have been processed.
  nsRefPtr<CodedFrameProcessingPromise> CodedFrameProcessing();
  void CompleteCodedFrameProcessing();
  // Called by ResetParserState.
  void CompleteResetParserState();
  nsRefPtr<RangeRemovalPromise>
    CodedFrameRemovalWithPromise(TimeInterval aInterval);
  bool CodedFrameRemoval(TimeInterval aInterval);
  void SetAppendState(AppendState aAppendState);

  bool HasVideo() const
  {
    return mVideoTracks.mNumTracks > 0;
  }
  bool HasAudio() const
  {
    return mAudioTracks.mNumTracks > 0;
  }

  typedef Pair<nsRefPtr<MediaLargeByteBuffer>, TimeUnit> IncomingBuffer;
  void AppendIncomingBuffer(IncomingBuffer aData);
  nsTArray<IncomingBuffer> mIncomingBuffers;

  // The input buffer as per http://w3c.github.io/media-source/index.html#sourcebuffer-input-buffer
  nsRefPtr<MediaLargeByteBuffer> mInputBuffer;
  // The current append state as per https://w3c.github.io/media-source/#sourcebuffer-append-state
  // Accessed on both the main thread and the task queue.
  AppendState mAppendState;
  // Buffer full flag as per https://w3c.github.io/media-source/#sourcebuffer-buffer-full-flag.
  // Accessed on both the main thread and the task queue.
  // TODO: Unused for now.
  bool mBufferFull;
  bool mFirstInitializationSegmentReceived;
  bool mActiveTrack;
  Maybe<TimeUnit> mGroupStartTimestamp;
  TimeUnit mGroupEndTimestamp;
  nsCString mType;

  // ContainerParser objects and methods.
  // Those are used to parse the incoming input buffer.

  // Recreate the ContainerParser and if aReuseInitData is true then
  // feed it with the previous init segment found.
  void RecreateParser(bool aReuseInitData);
  nsAutoPtr<ContainerParser> mParser;

  // Demuxer objects and methods.
  void AppendDataToCurrentInputBuffer(MediaLargeByteBuffer* aData);
  nsRefPtr<MediaLargeByteBuffer> mInitData;
  nsRefPtr<SourceBufferResource> mCurrentInputBuffer;
  nsRefPtr<MediaDataDemuxer> mInputDemuxer;
  // Length already processed in current media segment.
  uint64_t mProcessedInput;

  void OnDemuxerInitDone(nsresult);
  void OnDemuxerInitFailed(DemuxerFailureReason aFailure);
  MediaPromiseRequestHolder<MediaDataDemuxer::InitPromise> mDemuxerInitRequest;
  bool mEncrypted;

  void OnDemuxFailed(TrackType aTrack, DemuxerFailureReason aFailure);
  void DoDemuxVideo();
  void OnVideoDemuxCompleted(nsRefPtr<MediaTrackDemuxer::SamplesHolder> aSamples);
  void OnVideoDemuxFailed(DemuxerFailureReason aFailure)
  {
    mVideoTracks.mDemuxRequest.Complete();
    OnDemuxFailed(TrackType::kVideoTrack, aFailure);
  }
  void DoDemuxAudio();
  void OnAudioDemuxCompleted(nsRefPtr<MediaTrackDemuxer::SamplesHolder> aSamples);
  void OnAudioDemuxFailed(DemuxerFailureReason aFailure)
  {
    mAudioTracks.mDemuxRequest.Complete();
    OnDemuxFailed(TrackType::kAudioTrack, aFailure);
  }

  void DoEvictData(const TimeUnit& aPlaybackTime, uint32_t aThreshold);

  struct TrackData {
    TrackData()
      : mNumTracks(0)
      , mNeedRandomAccessPoint(true)
      , mSizeBuffer(0)
    {}
    uint32_t mNumTracks;
    // Definition of variables:
    // https://w3c.github.io/media-source/#track-buffers
    // Last decode timestamp variable that stores the decode timestamp of the
    // last coded frame appended in the current coded frame group.
    // The variable is initially unset to indicate that no coded frames have
    // been appended yet.
    Maybe<TimeUnit> mLastDecodeTimestamp;
    // Last frame duration variable that stores the coded frame duration of the
    // last coded frame appended in the current coded frame group.
    // The variable is initially unset to indicate that no coded frames have
    // been appended yet.
    Maybe<TimeUnit> mLastFrameDuration;
    // Highest end timestamp variable that stores the highest coded frame end
    // timestamp across all coded frames in the current coded frame group that
    // were appended to this track buffer.
    // The variable is initially unset to indicate that no coded frames have
    // been appended yet.
    Maybe<TimeUnit> mHighestEndTimestamp;
    // Longest frame duration seen in a coded frame group.
    Maybe<TimeUnit> mLongestFrameDuration;
    // Need random access point flag variable that keeps track of whether the
    // track buffer is waiting for a random access point coded frame.
    // The variable is initially set to true to indicate that random access
    // point coded frame is needed before anything can be added to the track
    // buffer.
    bool mNeedRandomAccessPoint;
    nsRefPtr<MediaTrackDemuxer> mDemuxer;
    MediaPromiseRequestHolder<MediaTrackDemuxer::SamplesPromise> mDemuxRequest;
    // If set, position where the next contiguous frame will be inserted.
    // If a discontinuity is detected, it will be unset and recalculated upon
    // the next insertion.
    Maybe<size_t> mNextInsertionIndex;
    // Samples just demuxed, but not yet parsed.
    TrackBuffer mQueuedSamples;
    // We only manage a single track of each type at this time.
    nsTArray<TrackBuffer> mBuffers;
    // Track buffer ranges variable that represents the presentation time ranges
    // occupied by the coded frames currently stored in the track buffer.
    TimeIntervals mBufferedRanges;
    // Sanitized mBufferedRanges with a fuzz of half a sample's duration applied
    // This buffered ranges is the basis of what is exposed to the JS.
    TimeIntervals mSanitizedBufferedRanges;
    // Byte size of all samples contained in this track buffer.
    uint32_t mSizeBuffer;
    // TrackInfo of the first metadata received.
    nsRefPtr<SharedTrackInfo> mInfo;
    // TrackInfo of the last metadata parsed (updated with each init segment.
    nsRefPtr<SharedTrackInfo> mLastInfo;

    // If set, position of the next sample to be retrieved by GetSample().
    // If the position is equal to the TrackBuffer's length, it indicates that
    // we've reached EOS.
    Maybe<uint32_t> mNextGetSampleIndex;
    // Approximation of the next sample's decode timestamp.
    TimeUnit mNextSampleTimecode;
    // Approximation of the next sample's presentation timestamp.
    TimeUnit mNextSampleTime;

    void ResetAppendState()
    {
      mLastDecodeTimestamp.reset();
      mLastFrameDuration.reset();
      mHighestEndTimestamp.reset();
      mNeedRandomAccessPoint = true;

      mLongestFrameDuration.reset();
      mNextInsertionIndex.reset();
    }
  };

  void CheckSequenceDiscontinuity(const TimeUnit& aPresentationTime);
  void ProcessFrames(TrackBuffer& aSamples, TrackData& aTrackData);
  bool CheckNextInsertionIndex(TrackData& aTrackData,
                               const TimeUnit& aSampleTime);
  void InsertFrames(TrackBuffer& aSamples,
                    const TimeIntervals& aIntervals,
                    TrackData& aTrackData);
  void RemoveFrames(const TimeIntervals& aIntervals,
                    TrackData& aTrackData,
                    uint32_t aStartIndex);
  // Find index of sample. Return a negative value if not found.
  uint32_t FindSampleIndex(const TrackBuffer& aTrackBuffer,
                           const TimeInterval& aInterval);
  void UpdateBufferedRanges();
  void RejectProcessing(nsresult aRejectValue, const char* aName);
  void ResolveProcessing(bool aResolveValue, const char* aName);
  MediaPromiseRequestHolder<CodedFrameProcessingPromise> mProcessingRequest;
  MediaPromiseHolder<CodedFrameProcessingPromise> mProcessingPromise;

  MediaPromiseHolder<AppendPromise> mAppendPromise;

  // Trackbuffers definition.
  nsTArray<TrackData*> GetTracksList();
  TrackData& GetTracksData(TrackType aTrack)
  {
    switch(aTrack) {
      case TrackType::kVideoTrack:
        return mVideoTracks;
      case TrackType::kAudioTrack:
      default:
        return mAudioTracks;
    }
  }
  TrackData mVideoTracks;
  TrackData mAudioTracks;

  // TaskQueue methods and objects.
  AbstractThread* GetTaskQueue() {
    return mTaskQueue;
  }
  bool OnTaskQueue()
  {
    return !GetTaskQueue() || GetTaskQueue()->IsCurrentThreadIn();
  }
  RefPtr<MediaTaskQueue> mTaskQueue;

  TimeInterval mAppendWindow;
  TimeUnit mTimestampOffset;
  TimeUnit mLastTimestampOffset;
  void RestoreCachedVariables();

  // Strong references to external objects.
  nsRefPtr<dom::SourceBufferAttributes> mSourceBufferAttributes;
  nsMainThreadPtrHandle<MediaSourceDecoder> mParentDecoder;

  // Set to true if mediasource state changed to ended.
  bool mEnded;

  // Global size of this source buffer content.
  int64_t mSizeSourceBuffer;
  uint32_t mEvictionThreshold;
  bool mEvictionOccurred;

  // Monitor to protect following objects accessed across multiple threads.
  // mMonitor is also notified if the value of mAppendRunning becomes false.
  mutable Monitor mMonitor;
  // Set to true while a BufferAppend is running or is pending.
  bool mAppendRunning;
  // Stable audio and video track time ranges.
  TimeIntervals mVideoBufferedRanges;
  TimeIntervals mAudioBufferedRanges;
  TimeUnit mOfficialGroupEndTimestamp;
  // MediaInfo of the first init segment read.
  MediaInfo mInfo;
};

} // namespace mozilla

#endif /* MOZILLA_TRACKBUFFERSMANAGER_H_ */
