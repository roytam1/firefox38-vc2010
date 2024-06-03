/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

let {Log} = Cu.import("resource://gre/modules/Log.jsm", {});
let {Weave} = Cu.import("resource://services-sync/main.js", {});
let {Notifications} = Cu.import("resource://services-sync/notifications.js", {});
// The BackStagePass allows us to get this test-only non-exported function.
let {getInternalScheduler} = Cu.import("resource:///modules/readinglist/Scheduler.jsm", {});

let stringBundle = Cc["@mozilla.org/intl/stringbundle;1"]
                   .getService(Ci.nsIStringBundleService)
                   .createBundle("chrome://weave/locale/services/sync.properties");

// ensure test output sees log messages.
Log.repository.getLogger("browserwindow.syncui").addAppender(new Log.DumpAppender());

function promiseObserver(topic) {
  return new Promise(resolve => {
    let obs = (subject, topic, data) => {
      Services.obs.removeObserver(obs, topic);
      resolve(subject);
    }
    Services.obs.addObserver(obs, topic, false);
  });
}

add_task(function* prepare() {
  let xps = Components.classes["@mozilla.org/weave/service;1"]
                              .getService(Components.interfaces.nsISupports)
                              .wrappedJSObject;
  yield xps.whenLoaded();
  // mock out the "_needsSetup()" function so we don't short-circuit.
  let oldNeedsSetup = window.gSyncUI._needsSetup;
  window.gSyncUI._needsSetup = () => false;
  registerCleanupFunction(() => {
    window.gSyncUI._needsSetup = oldNeedsSetup;
  });
});

add_task(function* testProlongedSyncError() {
  let promiseNotificationAdded = promiseObserver("weave:notification:added");
  Assert.equal(Notifications.notifications.length, 0, "start with no notifications");

  // Pretend we are in the "prolonged error" state.
  Weave.Status.sync = Weave.PROLONGED_SYNC_FAILURE;
  Weave.Status.login = Weave.LOGIN_SUCCEEDED;
  Services.obs.notifyObservers(null, "weave:ui:sync:error", null);

  let subject = yield promiseNotificationAdded;
  let notification = subject.wrappedJSObject.object; // sync's observer abstraction is abstract!
  Assert.equal(notification.title, stringBundle.GetStringFromName("error.sync.title"));
  Assert.equal(Notifications.notifications.length, 1, "exactly 1 notification");

  // Now pretend we just had a successful sync - the error notification should go away.
  let promiseNotificationRemoved = promiseObserver("weave:notification:removed");
  Weave.Status.sync = Weave.STATUS_OK;
  Services.obs.notifyObservers(null, "weave:ui:sync:finish", null);
  yield promiseNotificationRemoved;
  Assert.equal(Notifications.notifications.length, 0, "no notifications left");
});

add_task(function* testProlongedRLError() {
  let promiseNotificationAdded = promiseObserver("weave:notification:added");
  Assert.equal(Notifications.notifications.length, 0, "start with no notifications");

  // Pretend the reading-list is in the "prolonged error" state.
  let longAgo = new Date(Date.now() - 100 * 24 * 60 * 60 * 1000); // 100 days ago.
  Services.prefs.setCharPref("readinglist.scheduler.lastSync", longAgo.toString());
  getInternalScheduler().state = ReadingListScheduler.STATE_ERROR_OTHER;
  Services.obs.notifyObservers(null, "readinglist:sync:start", null);
  Services.obs.notifyObservers(null, "readinglist:sync:error", null);

  let subject = yield promiseNotificationAdded;
  let notification = subject.wrappedJSObject.object; // sync's observer abstraction is abstract!
  Assert.equal(notification.title, stringBundle.GetStringFromName("error.sync.title"));
  Assert.equal(Notifications.notifications.length, 1, "exactly 1 notification");

  // Now pretend we just had a successful sync - the error notification should go away.
  let promiseNotificationRemoved = promiseObserver("weave:notification:removed");
  Services.prefs.setCharPref("readinglist.scheduler.lastSync", Date.now().toString());
  Services.obs.notifyObservers(null, "readinglist:sync:start", null);
  Services.obs.notifyObservers(null, "readinglist:sync:finish", null);
  yield promiseNotificationRemoved;
  Assert.equal(Notifications.notifications.length, 0, "no notifications left");
});

add_task(function* testSyncLoginError() {
  let promiseNotificationAdded = promiseObserver("weave:notification:added");
  Assert.equal(Notifications.notifications.length, 0, "start with no notifications");

  // Pretend we are in the "prolonged error" state.
  Weave.Status.sync = Weave.LOGIN_FAILED;
  Weave.Status.login = Weave.LOGIN_FAILED_LOGIN_REJECTED;
  Services.obs.notifyObservers(null, "weave:ui:sync:error", null);

  let subject = yield promiseNotificationAdded;
  let notification = subject.wrappedJSObject.object; // sync's observer abstraction is abstract!
  Assert.equal(notification.title, stringBundle.GetStringFromName("error.login.title"));
  Assert.equal(Notifications.notifications.length, 1, "exactly 1 notification");

  // Now pretend we just had a successful login - the error notification should go away.
  Weave.Status.sync = Weave.STATUS_OK;
  Weave.Status.login = Weave.LOGIN_SUCCEEDED;
  let promiseNotificationRemoved = promiseObserver("weave:notification:removed");
  Services.obs.notifyObservers(null, "weave:service:login:start", null);
  Services.obs.notifyObservers(null, "weave:service:login:finish", null);
  yield promiseNotificationRemoved;
  Assert.equal(Notifications.notifications.length, 0, "no notifications left");
});

add_task(function* testRLLoginError() {
  let promiseNotificationAdded = promiseObserver("weave:notification:added");
  Assert.equal(Notifications.notifications.length, 0, "start with no notifications");

  // Pretend RL is in an auth error state
  getInternalScheduler().state = ReadingListScheduler.STATE_ERROR_AUTHENTICATION;
  Services.obs.notifyObservers(null, "readinglist:sync:start", null);
  Services.obs.notifyObservers(null, "readinglist:sync:error", null);

  let subject = yield promiseNotificationAdded;
  let notification = subject.wrappedJSObject.object; // sync's observer abstraction is abstract!
  Assert.equal(notification.title, stringBundle.GetStringFromName("error.login.title"));
  Assert.equal(Notifications.notifications.length, 1, "exactly 1 notification");

  // Now pretend we just had a successful sync - the error notification should go away.
  getInternalScheduler().state = ReadingListScheduler.STATE_OK;
  let promiseNotificationRemoved = promiseObserver("weave:notification:removed");
  Services.obs.notifyObservers(null, "readinglist:sync:start", null);
  Services.obs.notifyObservers(null, "readinglist:sync:finish", null);
  yield promiseNotificationRemoved;
  Assert.equal(Notifications.notifications.length, 0, "no notifications left");
});

// Here we put readinglist into an "authentication error" state (should see
// the error bar reflecting this), then report a prolonged error from Sync (an
// infobar to reflect the sync error should replace it), then resolve the sync
// error - the authentication error from readinglist should remain.
add_task(function* testRLLoginErrorRemains() {
  let promiseNotificationAdded = promiseObserver("weave:notification:added");
  Assert.equal(Notifications.notifications.length, 0, "start with no notifications");

  // Pretend RL is in an auth error state
  getInternalScheduler().state = ReadingListScheduler.STATE_ERROR_AUTHENTICATION;
  Services.obs.notifyObservers(null, "readinglist:sync:start", null);
  Services.obs.notifyObservers(null, "readinglist:sync:error", null);

  let subject = yield promiseNotificationAdded;
  let notification = subject.wrappedJSObject.object; // sync's observer abstraction is abstract!
  Assert.equal(notification.title, stringBundle.GetStringFromName("error.login.title"));
  Assert.equal(Notifications.notifications.length, 1, "exactly 1 notification");

  // Now Sync into a prolonged auth error state.
  promiseNotificationAdded = promiseObserver("weave:notification:added");
  Weave.Status.sync = Weave.PROLONGED_SYNC_FAILURE;
  Weave.Status.login = Weave.LOGIN_FAILED_LOGIN_REJECTED;
  Services.obs.notifyObservers(null, "weave:ui:sync:error", null);
  subject = yield promiseNotificationAdded;
  // still exactly 1 notification with the "login" title.
  notification = subject.wrappedJSObject.object;
  Assert.equal(notification.title, stringBundle.GetStringFromName("error.login.title"));
  Assert.equal(Notifications.notifications.length, 1, "exactly 1 notification");

  // Resolve the sync problem.
  promiseNotificationAdded = promiseObserver("weave:notification:added");
  Weave.Status.sync = Weave.STATUS_OK;
  Weave.Status.login = Weave.LOGIN_SUCCEEDED;
  Services.obs.notifyObservers(null, "weave:ui:sync:finish", null);

  // Expect one notification - the RL login problem.
  subject = yield promiseNotificationAdded;
  // still exactly 1 notification with the "login" title.
  notification = subject.wrappedJSObject.object;
  Assert.equal(notification.title, stringBundle.GetStringFromName("error.login.title"));
  Assert.equal(Notifications.notifications.length, 1, "exactly 1 notification");

  // and cleanup - resolve the readinglist error.
  getInternalScheduler().state = ReadingListScheduler.STATE_OK;
  let promiseNotificationRemoved = promiseObserver("weave:notification:removed");
  Services.obs.notifyObservers(null, "readinglist:sync:start", null);
  Services.obs.notifyObservers(null, "readinglist:sync:finish", null);
  yield promiseNotificationRemoved;
  Assert.equal(Notifications.notifications.length, 0, "no notifications left");
});

function checkButtonsStatus(shouldBeActive) {
  let button = document.getElementById("sync-button");
  let panelbutton = document.getElementById("PanelUI-fxa-status");
  if (shouldBeActive) {
    Assert.equal(button.getAttribute("status"), "active");
    Assert.equal(panelbutton.getAttribute("syncstatus"), "active");
  } else {
    Assert.ok(!button.hasAttribute("status"));
    Assert.ok(!panelbutton.hasAttribute("syncstatus"));
  }
}

function testButtonActions(startNotification, endNotification) {
  checkButtonsStatus(false);
  // pretend a sync is starting.
  Services.obs.notifyObservers(null, startNotification, null);
  checkButtonsStatus(true);
  // and has stopped
  Services.obs.notifyObservers(null, endNotification, null);
  checkButtonsStatus(false);
}

add_task(function* testButtonActivities() {
  // add the Sync button to the panel so we can get it!
  CustomizableUI.addWidgetToArea("sync-button", CustomizableUI.AREA_PANEL);
  // check the button's functionality
  yield PanelUI.show();
  try {
    testButtonActions("weave:service:login:start", "weave:service:login:finish");
    testButtonActions("weave:service:login:start", "weave:service:login:error");

    testButtonActions("weave:service:sync:start", "weave:service:sync:finish");
    testButtonActions("weave:service:sync:start", "weave:service:sync:error");

    testButtonActions("readinglist:sync:start", "readinglist:sync:finish");
    testButtonActions("readinglist:sync:start", "readinglist:sync:error");

    // and ensure the counters correctly handle multiple in-flight syncs
    Services.obs.notifyObservers(null, "weave:service:sync:start", null);
    checkButtonsStatus(true);
    Services.obs.notifyObservers(null, "readinglist:sync:start", null);
    checkButtonsStatus(true);
    Services.obs.notifyObservers(null, "readinglist:sync:finish", null);
    // sync is still going...
    checkButtonsStatus(true);
    // another reading list starts
    Services.obs.notifyObservers(null, "readinglist:sync:start", null);
    checkButtonsStatus(true);
    // The initial sync stops.
    Services.obs.notifyObservers(null, "weave:service:sync:finish", null);
    // RL is still going...
    checkButtonsStatus(true);
    // RL finishes with an error, so no longer active.
    Services.obs.notifyObservers(null, "readinglist:sync:error", null);
    checkButtonsStatus(false);
  } finally {
    PanelUI.hide();
    CustomizableUI.removeWidgetFromArea("sync-button");
  }
});
