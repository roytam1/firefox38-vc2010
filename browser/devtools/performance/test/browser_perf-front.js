/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test basic functionality of PerformanceFront, emitting start and endtime values
 */

let WAIT_TIME = 1000;

function spawnTest () {
  let { target, front } = yield initBackend(SIMPLE_URL);

  let {
    profilerStartTime,
    timelineStartTime,
    memoryStartTime
  } = yield front.startRecording({
    withAllocations: true
  });

  ok(typeof profilerStartTime === "number",
    "The front.startRecording() emits a profiler start time.");
  ok(typeof timelineStartTime === "number",
    "The front.startRecording() emits a timeline start time.");
  ok(typeof memoryStartTime === "number",
    "The front.startRecording() emits a memory start time.");

  yield busyWait(WAIT_TIME);

  let {
    profilerEndTime,
    timelineEndTime,
    memoryEndTime
  } = yield front.stopRecording({
    withAllocations: true
  });

  ok(typeof profilerEndTime === "number",
    "The front.stopRecording() emits a profiler end time.");
  ok(typeof timelineEndTime === "number",
    "The front.stopRecording() emits a timeline end time.");
  ok(typeof memoryEndTime === "number",
    "The front.stopRecording() emits a memory end time.");

  ok(profilerEndTime > profilerStartTime,
    "The profilerEndTime is after profilerStartTime.");
  ok(timelineEndTime > timelineStartTime,
    "The timelineEndTime is after timelineStartTime.");
  ok(memoryEndTime > memoryStartTime,
    "The memoryEndTime is after memoryStartTime.");

  yield removeTab(target.tab);
  finish();
}
