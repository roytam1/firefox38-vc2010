/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

self.onmessage = e => {
  const { id, task, args } = e.data;

  switch (task) {
    case "plotTimestampsGraph":
      plotTimestampsGraph(id, args);
      break;
    default:
      self.postMessage({ id, error: e.message + "\n" + e.stack });
      break;
  }
};

/**
 * @see LineGraphWidget.prototype.setDataFromTimestamps in Graphs.jsm
 * @param number id
 * @param array timestamps
 * @param number interval
 */
function plotTimestampsGraph(id, args) {
  let plottedData = plotTimestamps(args.timestamps, args.interval);
  let plottedMinMaxSum = getMinMaxSum(plottedData);

  let response = { id, plottedData, plottedMinMaxSum };
  self.postMessage(response);
}

/**
 * Gets the min, max and average of the values in an array.
 * @param array source
 * @return object
 */
function getMinMaxSum(source) {
  let totalTicks = source.length;
  let maxValue = Number.MIN_SAFE_INTEGER;
  let minValue = Number.MAX_SAFE_INTEGER;
  let avgValue = 0;
  let sumValues = 0;

  for (let { value } of source) {
    maxValue = Math.max(value, maxValue);
    minValue = Math.min(value, minValue);
    sumValues += value;
  }
  avgValue = sumValues / totalTicks;

  return { minValue, maxValue, avgValue };
}

/**
 * Takes a list of numbers and plots them on a line graph representing
 * the rate of occurences in a specified interval.
 *
 * XXX: Copied almost verbatim from toolkit/devtools/server/actors/framerate.js
 * Remove that dead code after the Performance panel lands, bug 1075567.
 *
 * @param array timestamps
 *        A list of numbers representing time, ordered ascending. For example,
 *        this can be the raw data received from the framerate actor, which
 *        represents the elapsed time on each refresh driver tick.
 * @param number interval
 *        The maximum amount of time to wait between calculations.
 * @param number clamp
 *        The maximum allowed value.
 * @return array
 *         A collection of { delta, value } objects representing the
 *         plotted value at every delta time.
 */
function plotTimestamps(timestamps, interval = 100, clamp = 60) {
  let timeline = [];
  let totalTicks = timestamps.length;

  // If the refresh driver didn't get a chance to tick before the
  // recording was stopped, assume rate was 0.
  if (totalTicks == 0) {
    timeline.push({ delta: 0, value: 0 });
    timeline.push({ delta: interval, value: 0 });
    return timeline;
  }

  let frameCount = 0;
  let prevTime = +timestamps[0];

  for (let i = 1; i < totalTicks; i++) {
    let currTime = +timestamps[i];
    frameCount++;

    let elapsedTime = currTime - prevTime;
    if (elapsedTime < interval) {
      continue;
    }

    let rate = Math.min(1000 / (elapsedTime / frameCount), clamp);
    timeline.push({ delta: prevTime, value: rate });
    timeline.push({ delta: currTime, value: rate });

    frameCount = 0;
    prevTime = currTime;
  }

  return timeline;
}
