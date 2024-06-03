/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/* This list allows pre-existing or 'unfixable' JS issues to remain, while we
 * detect newly occurring issues in shipping JS. It is a list of regexes
 * matching files which have errors:
 */
const kWhitelist = new Set([
  /defaults\/profile\/prefs.js$/,
  /browser\/content\/browser\/places\/controller.js$/,
]);


let moduleLocation = gTestPath.replace(/\/[^\/]*$/i, "/parsingTestHelpers.jsm");
let {generateURIsFromDirTree} = Cu.import(moduleLocation, {});
let {Reflect} = Cu.import("resource://gre/modules/reflect.jsm", {});

/**
 * Check if an error should be ignored due to matching one of the whitelist
 * objects defined in kWhitelist
 *
 * @param uri the uri to check against the whitelist
 * @return true if the uri should be skipped, false otherwise.
 */
function uriIsWhiteListed(uri) {
  for (let whitelistItem of kWhitelist) {
    if (whitelistItem.test(uri.spec)) {
      return true;
    }
  }
  return false;
}

function parsePromise(uri) {
  let promise = new Promise((resolve, reject) => {
    let xhr = new XMLHttpRequest();
    xhr.open("GET", uri, true);
    xhr.onreadystatechange = function() {
      if (this.readyState == this.DONE) {
        let scriptText = this.responseText;
        let ast;
        try {
          info("Checking " + uri);
          ast = Reflect.parse(scriptText);
          resolve(true);
        } catch (ex) {
          let errorMsg = "Script error reading " + uri + ": " + ex;
          ok(false, errorMsg);
          resolve(false);
        }
      }
    };
    xhr.onerror = (error) => {
      ok(false, "XHR error reading " + uri + ": " + error);
      resolve(false);
    };
    xhr.overrideMimeType("application/javascript");
    xhr.send(null);
  });
  return promise;
}

add_task(function* checkAllTheJS() {
  // In debug builds, even on a fast machine, collecting the file list may take
  // more than 30 seconds, and parsing all files may take four more minutes.
  // For this reason, this test must be explictly requested in debug builds by
  // using the "--setpref parse=<filter>" argument to mach.  You can specify:
  //  - A case-sensitive substring of the file name to test (slow).
  //  - A single absolute URI printed out by a previous run (fast).
  //  - An empty string to run the test on all files (slowest).
  let parseRequested = Services.prefs.prefHasUserValue("parse");
  let parseValue = parseRequested && Services.prefs.getCharPref("parse");
  if (SpecialPowers.isDebugBuild) {
    if (!parseRequested) {
      ok(true, "Test disabled on debug build. To run, execute: ./mach" +
               " mochitest-browser --setpref parse=<case_sensitive_filter>" +
               " browser/base/content/test/general/browser_parsable_script.js");
      return;
    }
    // Request a 10 minutes timeout (30 seconds * 20) for debug builds.
    requestLongerTimeout(20);
  }

  let uris;
  // If an absolute URI is specified on the command line, use it immediately.
  if (parseValue && parseValue.contains(":")) {
    uris = [NetUtil.newURI(parseValue)];
  } else {
    let appDir = Services.dirsvc.get("XCurProcD", Ci.nsIFile);
    // This asynchronously produces a list of URLs (sadly, mostly sync on our
    // test infrastructure because it runs against jarfiles there, and
    // our zipreader APIs are all sync)
    let startTimeMs = Date.now();
    info("Collecting URIs");
    uris = yield generateURIsFromDirTree(appDir, [".js", ".jsm"]);
    info("Collected URIs in " + (Date.now() - startTimeMs) + "ms");

    // Apply the filter specified on the command line, if any.
    if (parseValue) {
      uris = uris.filter(uri => {
        if (uri.spec.contains(parseValue)) {
          return true;
        }
        info("Not checking filtered out " + uri.spec);
        return false;
      });
    }
  }

  // We create an array of promises so we can parallelize all our parsing
  // and file loading activity:
  let allPromises = [];
  for (let uri of uris) {
    if (uriIsWhiteListed(uri)) {
      info("Not checking whitelisted " + uri.spec);
      continue;
    }
    allPromises.push(parsePromise(uri.spec));
  }

  let promiseResults = yield Promise.all(allPromises);
  is(promiseResults.filter((x) => !x).length, 0, "There should be 0 parsing errors");
});

