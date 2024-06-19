/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_SOURCEBUFFERCONTENTMANAGER_H_
#define MOZILLA_SOURCEBUFFERCONTENTMANAGER_H_

#include "mozilla/TypedEnum.h"
#include "TimeUnits.h"
#include "MediaData.h"
#include "MediaPromise.h"
#include "MediaSourceDecoder.h"
#include "nsString.h"

namespace mozilla {

using media::TimeUnit;
using media::TimeIntervals;

class SourceBufferContentManager {
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SourceBufferContentManager);

  typedef MediaPromise<bool, nsresult, /* IsExclusive = */ true> AppendPromise;

  static already_AddRefed<SourceBufferContentManager>
  CreateManager(MediaSourceDecoder* aParentDecoder, const nsACString& aType);

  // Append data to the current decoder.  Also responsible for calling
  // NotifyDataArrived on the decoder to keep buffered range computation up
  // to date.  Returns false if the append failed.
  virtual nsRefPtr<AppendPromise>
  AppendData(MediaLargeByteBuffer* aData, TimeUnit aTimestampOffset /* microseconds */) = 0;

  // Abort any pending AppendData.
  virtual void AbortAppendData() = 0;

  // Run MSE Reset Parser State Algorithm.
  // 3.5.2 Reset Parser State
  // http://w3c.github.io/media-source/#sourcebuffer-reset-parser-state
  virtual void ResetParserState() = 0;

  // Runs MSE range removal algorithm.
  // http://w3c.github.io/media-source/#sourcebuffer-coded-frame-removal
  virtual bool RangeRemoval(TimeUnit aStart, TimeUnit aEnd) = 0;

  MOZ_BEGIN_NESTED_ENUM_CLASS(EvictDataResult, int8_t)
    NO_DATA_EVICTED,
    DATA_EVICTED,
    CANT_EVICT,
  MOZ_END_NESTED_ENUM_CLASS(EvictDataResult)

  // Evicts data up to aPlaybackTime. aThreshold is used to
  // bound the data being evicted. It will not evict more than aThreshold
  // bytes. aBufferStartTime contains the new start time of the data after the
  // eviction.
  virtual EvictDataResult
  EvictData(TimeUnit aPlaybackTime, uint32_t aThreshold, TimeUnit* aBufferStartTime) = 0;

  // Evicts data up to aTime.
  virtual void EvictBefore(TimeUnit aTime) = 0;

  // Returns the buffered range currently managed.
  // This may be called on any thread.
  // Buffered must conform to http://w3c.github.io/media-source/index.html#widl-SourceBuffer-buffered
  virtual media::TimeIntervals Buffered() = 0;

  // Return the size of the data managed by this SourceBufferContentManager.
  virtual int64_t GetSize() = 0;

  // Indicate that the MediaSource parent object got into "ended" state.
  virtual void Ended() = 0;

  // The parent SourceBuffer is about to be destroyed.
  virtual void Detach() = 0;

#if defined(DEBUG)
  virtual void Dump(const char* aPath) { }
#endif

protected:
  virtual ~SourceBufferContentManager() { }
};

} // namespace mozilla
#endif /* MOZILLA_SOURCEBUFFERCONTENTMANAGER_H_ */
