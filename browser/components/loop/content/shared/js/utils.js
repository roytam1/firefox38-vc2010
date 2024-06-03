/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global loop:true */

var loop = loop || {};
loop.shared = loop.shared || {};
loop.shared.utils = (function(mozL10n) {
  "use strict";

  /**
   * Call types used for determining if a call is audio/video or audio-only.
   */
  var CALL_TYPES = {
    AUDIO_VIDEO: "audio-video",
    AUDIO_ONLY: "audio"
  };

  var REST_ERRNOS = {
    INVALID_TOKEN: 105,
    EXPIRED: 111,
    USER_UNAVAILABLE: 122,
    ROOM_FULL: 202
  };

  var WEBSOCKET_REASONS = {
    ANSWERED_ELSEWHERE: "answered-elsewhere",
    BUSY: "busy",
    CANCEL: "cancel",
    CLOSED: "closed",
    MEDIA_FAIL: "media-fail",
    REJECT: "reject",
    TIMEOUT: "timeout"
  };

  var FAILURE_DETAILS = {
    MEDIA_DENIED: "reason-media-denied",
    UNABLE_TO_PUBLISH_MEDIA: "unable-to-publish-media",
    COULD_NOT_CONNECT: "reason-could-not-connect",
    NETWORK_DISCONNECTED: "reason-network-disconnected",
    EXPIRED_OR_INVALID: "reason-expired-or-invalid",
    UNKNOWN: "reason-unknown"
  };

  var STREAM_PROPERTIES = {
    VIDEO_DIMENSIONS: "videoDimensions",
    HAS_AUDIO: "hasAudio",
    HAS_VIDEO: "hasVideo"
  };

  var SCREEN_SHARE_STATES = {
    INACTIVE: "ss-inactive",
    // Pending is when the user is being prompted, aka gUM in progress.
    PENDING: "ss-pending",
    ACTIVE: "ss-active"
  };

  /**
   * Format a given date into an l10n-friendly string.
   *
   * @param {Integer} The timestamp in seconds to format.
   * @return {String} The formatted string.
   */
  function formatDate(timestamp) {
    var date = (new Date(timestamp * 1000));
    var options = {year: "numeric", month: "long", day: "numeric"};
    return date.toLocaleDateString(navigator.language, options);
  }

  /**
   * Used for getting a boolean preference. It will either use the browser preferences
   * (if navigator.mozLoop is defined) or try to get them from localStorage.
   *
   * @param {String} prefName The name of the preference. Note that mozLoop adds
   *                          'loop.' to the start of the string.
   *
   * @return The value of the preference, or false if not available.
   */
  function getBoolPreference(prefName) {
    if (navigator.mozLoop) {
      return !!navigator.mozLoop.getLoopPref(prefName);
    }

    return !!localStorage.getItem(prefName);
  }

  function isFirefox(platform) {
    return platform.indexOf("Firefox") !== -1;
  }

  function isFirefoxOS(platform) {
    // So far WebActivities are exposed only in FxOS, but they may be
    // exposed in Firefox Desktop soon, so we check for its existence
    // and also check if the UA belongs to a mobile platform.
    // XXX WebActivities are also exposed in WebRT on Firefox for Android,
    //     so we need a better check. Bug 1065403.
    return !!window.MozActivity && /mobi/i.test(platform);
  }

  /**
   * Helper to get the platform if it is unsupported.
   *
   * @param {String} platform The platform this is running on.
   * @return null for supported platforms, a string for unsupported platforms.
   */
  function getUnsupportedPlatform(platform) {
    if (/^(iPad|iPhone|iPod)/.test(platform)) {
      return "ios";
    }

    if (/Windows Phone/i.test(platform)) {
      return "windows_phone";
    }

    if (/BlackBerry/i.test(platform)) {
      return "blackberry";
    }

    return null;
  }

  /**
   * Helper to get the Operating System name.
   *
   * @param {String}  [platform]    The platform this is running on, will fall
   *                                back to navigator.oscpu and navigator.userAgent
   *                                respectively if not supplied.
   * @param {Boolean} [withVersion] Optional flag to keep the version number
   *                                included in the resulting string. Defaults to
   *                                `false`.
   * @return {String} The platform we're currently running on, in lower-case.
   */
  var getOS = _.memoize(function(platform, withVersion) {
    if (!platform) {
      if ("oscpu" in window.navigator) {
        // See https://developer.mozilla.org/en-US/docs/Web/API/Navigator/oscpu
        platform = window.navigator.oscpu.split(";")[0].trim();
      } else {
        // Fall back to navigator.userAgent as a last resort.
        platform = window.navigator.userAgent;
      }
    }

    if (!platform) {
      return "unknown";
    }

    // Support passing in navigator.userAgent.
    var platformPart = platform.match(/\((.*)\)/);
    if (platformPart) {
      platform = platformPart[1];
    }
    platform = platform.toLowerCase().split(";");
    if (/macintosh/.test(platform[0]) || /x11/.test(platform[0])) {
      platform = platform[1];
    } else {
      if (platform[0].indexOf("win") > -1 && platform.length > 4) {
        // Skip the security notation.
        platform = platform[2];
      } else {
        platform = platform[0];
      }
    }

    if (!withVersion) {
      platform = platform.replace(/\s[0-9.]+/g, "");
    }

    return platform.trim();
  }, function(platform, withVersion) {
    // Cache the return values with the following key.
    return (platform + "") + (withVersion + "");
  });

  /**
   * Helper to get the Operating System version.
   * See http://en.wikipedia.org/wiki/Windows_NT for a table of Windows NT
   * versions.
   *
   * @param {String} [platform] The platform this is running on, will fall back
   *                            to navigator.oscpu and navigator.userAgent
   *                            respectively if not supplied.
   * @return {String} The current version of the platform we're currently running
   *                  on.
   */
  var getOSVersion = _.memoize(function(platform) {
    var os = getOS(platform, true);
    var digitsRE = /\s([0-9.]+)/;

    var version = os.match(digitsRE);
    if (!version) {
      if (os.indexOf("win") > -1) {
        if (os.indexOf("xp")) {
          return { major: 5, minor: 2 };
        } else if (os.indexOf("vista") > -1) {
          return { major: 6, minor: 0 };
        }
      }
    } else {
      version = version[1];
      // Windows versions have an interesting scheme.
      if (os.indexOf("win") > -1) {
        switch (parseFloat(version)) {
          case 98:
            return { major: 4, minor: 1 };
          case 2000:
            return { major: 5, minor: 0 };
          case 2003:
            return { major: 5, minor: 2 };
          case 7:
          case 2008:
          case 2011:
            return { major: 6, minor: 1 };
          case 8:
            return { major: 6, minor: 2 };
          case 8.1:
          case 2012:
            return { major: 6, minor: 3 };
        }
      }

      version = version.split(".");
      return {
        major: parseInt(version[0].trim(), 10),
        minor: parseInt(version[1] ? version[1].trim() : 0, 10)
      };
    }

    return { major: Infinity, minor: 0 };
  });

  /**
   * Helper to allow getting some of the location data in a way that's compatible
   * with stubbing for unit tests.
   */
  function locationData() {
    return {
      hash: window.location.hash,
      pathname: window.location.pathname
    };
  }

  /**
   * Generates and opens a mailto: url with call URL information prefilled.
   * Note: This only works for Desktop.
   *
   * @param  {String} callUrl   The call URL.
   * @param  {String} recipient The recipient email address (optional).
   */
  function composeCallUrlEmail(callUrl, recipient) {
    if (typeof navigator.mozLoop === "undefined") {
      console.warn("composeCallUrlEmail isn't available for Loop standalone.");
      return;
    }
    navigator.mozLoop.composeEmail(
      mozL10n.get("share_email_subject5", {
        clientShortname2: mozL10n.get("clientShortname2")
      }),
      mozL10n.get("share_email_body5", {
        callUrl: callUrl,
        brandShortname: mozL10n.get("brandShortname"),
        clientShortname2: mozL10n.get("clientShortname2"),
        clientSuperShortname: mozL10n.get("clientSuperShortname"),
        learnMoreUrl: navigator.mozLoop.getLoopPref("learnMoreUrl")
      }).replace(/\r\n/g, "\n").replace(/\n/g, "\r\n"),
      recipient
    );
  }

  return {
    CALL_TYPES: CALL_TYPES,
    FAILURE_DETAILS: FAILURE_DETAILS,
    REST_ERRNOS: REST_ERRNOS,
    WEBSOCKET_REASONS: WEBSOCKET_REASONS,
    STREAM_PROPERTIES: STREAM_PROPERTIES,
    SCREEN_SHARE_STATES: SCREEN_SHARE_STATES,
    composeCallUrlEmail: composeCallUrlEmail,
    formatDate: formatDate,
    getBoolPreference: getBoolPreference,
    getOS: getOS,
    getOSVersion: getOSVersion,
    isFirefox: isFirefox,
    isFirefoxOS: isFirefoxOS,
    getUnsupportedPlatform: getUnsupportedPlatform,
    locationData: locationData
  };
})(document.mozL10n || navigator.mozL10n);
