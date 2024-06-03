/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global loop, sinon */
/* jshint newcap:false */

var expect = chai.expect;

describe("loop.shared.utils", function() {
  "use strict";

  var sandbox;
  var sharedUtils = loop.shared.utils;

  beforeEach(function() {
    sandbox = sinon.sandbox.create();
  });

  afterEach(function() {
    navigator.mozLoop = undefined;
    sandbox.restore();
  });

  describe("#getUnsupportedPlatform", function() {
    it("should detect iOS", function() {
      expect(sharedUtils.getUnsupportedPlatform("iPad")).eql('ios');
      expect(sharedUtils.getUnsupportedPlatform("iPod")).eql('ios');
      expect(sharedUtils.getUnsupportedPlatform("iPhone")).eql('ios');
      expect(sharedUtils.getUnsupportedPlatform("iPhone Simulator")).eql('ios');
    });

    it("should detect Windows Phone", function() {
      expect(sharedUtils.getUnsupportedPlatform("Windows Phone"))
        .eql('windows_phone');
    });

    it("should detect BlackBerry", function() {
      expect(sharedUtils.getUnsupportedPlatform("BlackBerry"))
        .eql('blackberry');
    });

    it("shouldn't detect other platforms", function() {
      expect(sharedUtils.getUnsupportedPlatform("MacIntel")).eql(null);
    });
  });

  describe("#isFirefox", function() {
    it("should detect Firefox", function() {
      expect(sharedUtils.isFirefox("Firefox")).eql(true);
      expect(sharedUtils.isFirefox("Gecko/Firefox")).eql(true);
      expect(sharedUtils.isFirefox("Firefox/Gecko")).eql(true);
      expect(sharedUtils.isFirefox("Gecko/Firefox/Chuck Norris")).eql(true);
    });

    it("shouldn't detect Firefox with other platforms", function() {
      expect(sharedUtils.isFirefox("Opera")).eql(false);
    });
  });

  describe("#isFirefoxOS", function() {
    describe("without mozActivities", function() {
      it("shouldn't detect FirefoxOS on mobile platform", function() {
        expect(sharedUtils.isFirefoxOS("mobi")).eql(false);
      });

      it("shouldn't detect FirefoxOS on non mobile platform", function() {
        expect(sharedUtils.isFirefoxOS("whatever")).eql(false);
      });
    });

    describe("with mozActivities", function() {
      var realMozActivity;

      before(function() {
        realMozActivity = window.MozActivity;
        window.MozActivity = {};
      });

      after(function() {
        window.MozActivity = realMozActivity;
      });

      it("should detect FirefoxOS on mobile platform", function() {
        expect(sharedUtils.isFirefoxOS("mobi")).eql(true);
      });

      it("shouldn't detect FirefoxOS on non mobile platform", function() {
        expect(sharedUtils.isFirefoxOS("whatever")).eql(false);
      });
    });
  });

  describe("#formatDate", function() {
    beforeEach(function() {
      sandbox.stub(Date.prototype, "toLocaleDateString").returns("fake result");
    });

    it("should call toLocaleDateString with arguments", function() {
      sharedUtils.formatDate(1000);

      sinon.assert.calledOnce(Date.prototype.toLocaleDateString);
      sinon.assert.calledWithExactly(Date.prototype.toLocaleDateString,
        navigator.language,
        {year: "numeric", month: "long", day: "numeric"}
      );
    });

    it("should return the formatted string", function() {
      expect(sharedUtils.formatDate(1000)).eql("fake result");
    });
  });

  describe("#getBoolPreference", function() {
    afterEach(function() {
      localStorage.removeItem("test.true");
    });

    describe("mozLoop set", function() {
      beforeEach(function() {
        navigator.mozLoop = {
          getLoopPref: function(prefName) {
            return prefName === "test.true";
          }
        };
      });

      it("should return the mozLoop preference", function() {
        expect(sharedUtils.getBoolPreference("test.true")).eql(true);
      });

      it("should not use the localStorage value", function() {
        localStorage.setItem("test.false", true);

        expect(sharedUtils.getBoolPreference("test.false")).eql(false);
      });
    });

    describe("mozLoop not set", function() {
      it("should return the localStorage value", function() {
        localStorage.setItem("test.true", true);

        expect(sharedUtils.getBoolPreference("test.true")).eql(true);
      });
    });
  });

  describe("#composeCallUrlEmail", function() {
    var composeEmail;

    beforeEach(function() {
      // fake mozL10n
      sandbox.stub(navigator.mozL10n, "get", function(id) {
        switch(id) {
          case "share_email_subject5": return "subject";
          case "share_email_body5":    return "body";
        }
      });
      composeEmail = sandbox.spy();
      navigator.mozLoop = {
        getLoopPref: sandbox.spy(),
        composeEmail: composeEmail
      };
    });

    it("should compose a call url email", function() {
      sharedUtils.composeCallUrlEmail("http://invalid", "fake@invalid.tld");

      sinon.assert.calledOnce(composeEmail);
      sinon.assert.calledWith(composeEmail,
                              "subject", "body", "fake@invalid.tld");
    });
  });

  describe("#getOS", function() {
    it("should recognize the OSX userAgent string", function() {
      var UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.10; rv:37.0) Gecko/20100101 Firefox/37.0";
      var result = sharedUtils.getOS(UA);

      expect(result).eql("intel mac os x");
    });

    it("should recognize the OSX userAgent string with version", function() {
      var UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.10; rv:37.0) Gecko/20100101 Firefox/37.0";
      var result = sharedUtils.getOS(UA, true);

      expect(result).eql("intel mac os x 10.10");
    });

    it("should recognize the Windows userAgent string with version", function() {
      var UA = "Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:10.0) Gecko/20100101 Firefox/10.0";
      var result = sharedUtils.getOS(UA, true);

      expect(result).eql("windows nt 6.1");
    });

    it("should recognize the Linux userAgent string", function() {
      var UA = "Mozilla/5.0 (X11; Linux i686 on x86_64; rv:10.0) Gecko/20100101 Firefox/10.0";
      var result = sharedUtils.getOS(UA);

      expect(result).eql("linux i686 on x86_64");
    });

    it("should recognize the OSX oscpu string", function() {
      var oscpu = "Intel Mac OS X 10.10";
      var result = sharedUtils.getOS(oscpu, true);

      expect(result).eql("intel mac os x 10.10");
    });

    it("should recognize the Windows oscpu string", function() {
      var oscpu = "Windows NT 5.3; Win64; x64";
      var result = sharedUtils.getOS(oscpu, true);

      expect(result).eql("windows nt 5.3");
    });
  });

  describe("#getOSVersion", function() {
    it("should fetch the correct version info for OSX", function() {
      var UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.10; rv:37.0) Gecko/20100101 Firefox/37.0";
      var result = sharedUtils.getOSVersion(UA);

      expect(result).eql({ major: 10, minor: 10 });
    });

    it("should fetch the correct version info for Windows", function() {
      var UA = "Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:10.0) Gecko/20100101 Firefox/10.0";
      var result = sharedUtils.getOSVersion(UA);

      expect(result).eql({ major: 6, minor: 1 });
    });

    it("should fetch the correct version info for Windows XP", function() {
      var oscpu = "Windows XP";
      var result = sharedUtils.getOSVersion(oscpu);

      expect(result).eql({ major: 5, minor: 2 });
    });

    it("should fetch the correct version info for Linux", function() {
      var UA = "Mozilla/5.0 (X11; Linux i686 on x86_64; rv:10.0) Gecko/20100101 Firefox/10.0";
      var result = sharedUtils.getOSVersion(UA);

      // Linux version can't be determined correctly.
      expect(result).eql({ major: Infinity, minor: 0 });
    });
  });
});
