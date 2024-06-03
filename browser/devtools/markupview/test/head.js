/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const Cu = Components.utils;
let {devtools} = Cu.import("resource://gre/modules/devtools/Loader.jsm", {});
let TargetFactory = devtools.TargetFactory;
let {console} = Cu.import("resource://gre/modules/devtools/Console.jsm", {});
let promise = devtools.require("resource://gre/modules/Promise.jsm").Promise;
let {getInplaceEditorForSpan: inplaceEditor} = devtools.require("devtools/shared/inplace-editor");
let clipboard = devtools.require("sdk/clipboard");

// All test are asynchronous
waitForExplicitFinish();

// If a test times out we want to see the complete log and not just the last few
// lines.
SimpleTest.requestCompleteLog();

// Uncomment this pref to dump all devtools emitted events to the console.
// Services.prefs.setBoolPref("devtools.dump.emit", true);

// Set the testing flag on gDevTools and reset it when the test ends
gDevTools.testing = true;
registerCleanupFunction(() => gDevTools.testing = false);

// Clear preferences that may be set during the course of tests.
registerCleanupFunction(() => {
  Services.prefs.clearUserPref("devtools.inspector.htmlPanelOpen");
  Services.prefs.clearUserPref("devtools.inspector.sidebarOpen");
  Services.prefs.clearUserPref("devtools.inspector.activeSidebar");
  Services.prefs.clearUserPref("devtools.dump.emit");
  Services.prefs.clearUserPref("devtools.markup.pagesize");
  Services.prefs.clearUserPref("dom.webcomponents.enabled");
  Services.prefs.clearUserPref("devtools.inspector.showAllAnonymousContent");
});

// Auto close the toolbox and close the test tabs when the test ends
registerCleanupFunction(function*() {
  let target = TargetFactory.forTab(gBrowser.selectedTab);
  yield gDevTools.closeToolbox(target);

  while (gBrowser.tabs.length > 1) {
    gBrowser.removeCurrentTab();
  }
});

const TEST_URL_ROOT = "http://mochi.test:8888/browser/browser/devtools/markupview/test/";
const CHROME_BASE = "chrome://mochitests/content/browser/browser/devtools/markupview/test/";

/**
 * Add a new test tab in the browser and load the given url.
 * @param {String} url The url to be loaded in the new tab
 * @return a promise that resolves to the tab object when the url is loaded
 */
function addTab(url) {
  info("Adding a new tab with URL: '" + url + "'");
  let def = promise.defer();

  // Bug 921935 should bring waitForFocus() support to e10s, which would
  // probably cover the case of the test losing focus when the page is loading.
  // For now, we just make sure the window is focused.
  window.focus();

  let tab = window.gBrowser.selectedTab = window.gBrowser.addTab(url);
  let linkedBrowser = tab.linkedBrowser;

  linkedBrowser.addEventListener("load", function onload() {
    linkedBrowser.removeEventListener("load", onload, true);
    info("URL '" + url + "' loading complete");
    def.resolve(tab);
  }, true);

  return def.promise;
}

/**
 * Some tests may need to import one or more of the test helper scripts.
 * A test helper script is simply a js file that contains common test code that
 * is either not common-enough to be in head.js, or that is located in a separate
 * directory.
 * The script will be loaded synchronously and in the test's scope.
 * @param {String} filePath The file path, relative to the current directory.
 *                 Examples:
 *                 - "helper_attributes_test_runner.js"
 *                 - "../../../commandline/test/helpers.js"
 */
function loadHelperScript(filePath) {
  let testDir = gTestPath.substr(0, gTestPath.lastIndexOf("/"));
  Services.scriptloader.loadSubScript(testDir + "/" + filePath, this);
}

/**
 * Reload the current page
 * @return a promise that resolves when the inspector has emitted the event
 * new-root
 */
function reloadPage(inspector) {
  info("Reloading the page");
  let newRoot = inspector.once("new-root");
  content.location.reload();
  return newRoot;
}

/**
 * Open the toolbox, with the inspector tool visible.
 * @return a promise that resolves when the inspector is ready
 */
function openInspector() {
  info("Opening the inspector panel");
  let def = promise.defer();

  let target = TargetFactory.forTab(gBrowser.selectedTab);
  gDevTools.showToolbox(target, "inspector").then(function(toolbox) {
    info("The toolbox is open");
    let inspector = toolbox.getCurrentPanel();
    inspector.once("inspector-updated", () => {
      info("The inspector panel is active and ready");
      def.resolve({toolbox: toolbox, inspector: inspector});
    });
  }).then(null, console.error);

  return def.promise;
}

/**
 * Simple DOM node accesor function that takes either a node or a string css
 * selector as argument and returns the corresponding node
 * @param {String|DOMNode} nodeOrSelector
 * @return {DOMNode|CPOW} Note that in e10s mode a CPOW object is returned which
 * doesn't implement *all* of the DOMNode's properties
 */
function getNode(nodeOrSelector) {
  info("Getting the node for '" + nodeOrSelector + "'");
  return typeof nodeOrSelector === "string" ?
    content.document.querySelector(nodeOrSelector) :
    nodeOrSelector;
}

/**
 * Get the NodeFront for a given css selector, via the protocol
 * @param {String|NodeFront} selector
 * @param {InspectorPanel} inspector The instance of InspectorPanel currently
 * loaded in the toolbox
 * @return {Promise} Resolves to the NodeFront instance
 */
function getNodeFront(selector, {walker}) {
  if (selector._form) {
    return selector;
  }
  return walker.querySelector(walker.rootNode, selector);
}

/**
 * Highlight a node and set the inspector's current selection to the node or
 * the first match of the given css selector.
 * @param {String|DOMNode} nodeOrSelector
 * @param {InspectorPanel} inspector
 *        The instance of InspectorPanel currently loaded in the toolbox
 * @return a promise that resolves when the inspector is updated with the new
 * node
 */
function selectAndHighlightNode(nodeOrSelector, inspector) {
  info("Highlighting and selecting the node " + nodeOrSelector);

  let node = getNode(nodeOrSelector);
  let updated = inspector.toolbox.once("highlighter-ready");
  inspector.selection.setNode(node, "test-highlight");
  return updated;
}

/**
 * Set the inspector's current selection to the first match of the given css
 * selector
 * @param {String|NodeFront} selector
 * @param {InspectorPanel} inspector The instance of InspectorPanel currently
 * loaded in the toolbox
 * @param {String} reason Defaults to "test" which instructs the inspector not
 * to highlight the node upon selection
 * @return {Promise} Resolves when the inspector is updated with the new node
 */
let selectNode = Task.async(function*(selector, inspector, reason="test") {
  info("Selecting the node for '" + selector + "'");
  let nodeFront = yield getNodeFront(selector, inspector);
  let updated = inspector.once("inspector-updated");
  inspector.selection.setNodeFront(nodeFront, reason);
  yield updated;
});

/**
 * Get the MarkupContainer object instance that corresponds to the given
 * NodeFront
 * @param {NodeFront} nodeFront
 * @param {InspectorPanel} inspector The instance of InspectorPanel currently
 * loaded in the toolbox
 * @return {MarkupContainer}
 */
function getContainerForNodeFront(nodeFront, {markup}) {
  return markup.getContainer(nodeFront);
}

/**
 * Get the MarkupContainer object instance that corresponds to the given
 * selector
 * @param {String|NodeFront} selector
 * @param {InspectorPanel} inspector The instance of InspectorPanel currently
 * loaded in the toolbox
 * @return {MarkupContainer}
 */
let getContainerForSelector = Task.async(function*(selector, inspector) {
  info("Getting the markup-container for node " + selector);
  let nodeFront = yield getNodeFront(selector, inspector);
  let container = getContainerForNodeFront(nodeFront, inspector);
  info("Found markup-container " + container);
  return container;
});

/**
 * Using the markupview's _waitForChildren function, wait for all queued
 * children updates to be handled.
 * @param {InspectorPanel} inspector The instance of InspectorPanel currently
 * loaded in the toolbox
 * @return a promise that resolves when all queued children updates have been
 * handled
 */
function waitForChildrenUpdated({markup}) {
  info("Waiting for queued children updates to be handled");
  let def = promise.defer();
  markup._waitForChildren().then(() => {
    executeSoon(def.resolve);
  });
  return def.promise;
}

/**
 * Simulate a click on the markup-container (a line in the markup-view)
 * that corresponds to the selector passed.
 * @param {String|NodeFront} selector
 * @param {InspectorPanel} inspector The instance of InspectorPanel currently
 * loaded in the toolbox
 * @return {Promise} Resolves when the node has been selected.
 */
let clickContainer = Task.async(function*(selector, inspector) {
  info("Clicking on the markup-container for node " + selector);

  let nodeFront = yield getNodeFront(selector, inspector);
  let container = getContainerForNodeFront(nodeFront, inspector);

  let updated = container.selected ? promise.resolve() : inspector.once("inspector-updated");
  EventUtils.synthesizeMouseAtCenter(container.tagLine, {type: "mousedown"},
    inspector.markup.doc.defaultView);
  EventUtils.synthesizeMouseAtCenter(container.tagLine, {type: "mouseup"},
    inspector.markup.doc.defaultView);
  return updated;
});

/**
 * Checks if the highlighter is visible currently
 * @return {Boolean}
 */
function isHighlighterVisible() {
  let highlighter = gBrowser.selectedBrowser.parentNode
                            .querySelector(".highlighter-container .box-model-root");
  return highlighter && !highlighter.hasAttribute("hidden");
}

/**
 * Focus a given editable element, enter edit mode, set value, and commit
 * @param {DOMNode} field The element that gets editable after receiving focus
 * and <ENTER> keypress
 * @param {String} value The string value to be set into the edited field
 * @param {InspectorPanel} inspector The instance of InspectorPanel currently
 * loaded in the toolbox
 */
function setEditableFieldValue(field, value, inspector) {
  field.focus();
  EventUtils.sendKey("return", inspector.panelWin);
  let input = inplaceEditor(field).input;
  ok(input, "Found editable field for setting value: " + value);
  input.value = value;
  EventUtils.sendKey("return", inspector.panelWin);
}

/**
 * Focus the new-attribute inplace-editor field of a node's markup container
 * and enters the given text, then wait for it to be applied and the for the
 * node to mutates (when new attribute(s) is(are) created)
 * @param {String} selector The selector for the node to edit.
 * @param {String} text The new attribute text to be entered (e.g. "id='test'")
 * @param {InspectorPanel} inspector The instance of InspectorPanel currently
 * loaded in the toolbox
 * @return a promise that resolves when the node has mutated
 */
let addNewAttributes = Task.async(function*(selector, text, inspector) {
  info("Entering text '" + text + "' in node '" + selector + "''s new attribute field");

  let container = yield getContainerForSelector(selector, inspector);
  ok(container, "The container for '" + selector + "' was found");

  info("Listening for the markupmutation event");
  let nodeMutated = inspector.once("markupmutation");
  setEditableFieldValue(container.editor.newAttr, text, inspector);
  yield nodeMutated;
});

/**
 * Checks that a node has the given attributes
 *
 * @param {String} selector The node or node selector to check.
 * @param {Object} attrs An object containing the attributes to check.
 *        e.g. {id: "id1", class: "someclass"}
 *
 * Note that node.getAttribute() returns attribute values provided by the HTML
 * parser. The parser only provides unescaped entities so &amp; will return &.
 */
function assertAttributes(selector, attrs) {
  let node = getNode(selector);

  is(node.attributes.length, Object.keys(attrs).length,
    "Node has the correct number of attributes.");
  for (let attr in attrs) {
    is(node.getAttribute(attr), attrs[attr],
      "Node has the correct " + attr + " attribute.");
  }
}

/**
 * Undo the last markup-view action and wait for the corresponding mutation to
 * occur
 * @param {InspectorPanel} inspector The instance of InspectorPanel currently
 * loaded in the toolbox
 * @return a promise that resolves when the markup-mutation has been treated or
 * rejects if no undo action is possible
 */
function undoChange(inspector) {
  let canUndo = inspector.markup.undo.canUndo();
  ok(canUndo, "The last change in the markup-view can be undone");
  if (!canUndo) {
    return promise.reject();
  }

  let mutated = inspector.once("markupmutation");
  inspector.markup.undo.undo();
  return mutated;
}

/**
 * Redo the last markup-view action and wait for the corresponding mutation to
 * occur
 * @param {InspectorPanel} inspector The instance of InspectorPanel currently
 * loaded in the toolbox
 * @return a promise that resolves when the markup-mutation has been treated or
 * rejects if no redo action is possible
 */
function redoChange(inspector) {
  let canRedo = inspector.markup.undo.canRedo();
  ok(canRedo, "The last change in the markup-view can be redone");
  if (!canRedo) {
    return promise.reject();
  }

  let mutated = inspector.once("markupmutation");
  inspector.markup.undo.redo();
  return mutated;
}

/**
 * Get the selector-search input box from the inspector panel
 * @return {DOMNode}
 */
function getSelectorSearchBox(inspector) {
  return inspector.panelWin.document.getElementById("inspector-searchbox");
}

/**
 * Using the inspector panel's selector search box, search for a given selector.
 * The selector input string will be entered in the input field and the <ENTER>
 * keypress will be simulated.
 * This function won't wait for any events and is not async. It's up to callers
 * to subscribe to events and react accordingly.
 */
function searchUsingSelectorSearch(selector, inspector) {
  info("Entering \"" + selector + "\" into the selector-search input field");
  let field = getSelectorSearchBox(inspector);
  field.focus();
  field.value = selector;
  EventUtils.sendKey("return", inspector.panelWin);
}

/**
 * This shouldn't be used in the tests, but is useful when writing new tests or
 * debugging existing tests in order to introduce delays in the test steps
 * @param {Number} ms The time to wait
 * @return A promise that resolves when the time is passed
 */
function wait(ms) {
  let def = promise.defer();
  content.setTimeout(def.resolve, ms);
  return def.promise;
}

/**
 * Wait for eventName on target.
 * @param {Object} target An observable object that either supports on/off or
 * addEventListener/removeEventListener
 * @param {String} eventName
 * @param {Boolean} useCapture Optional, for addEventListener/removeEventListener
 * @return A promise that resolves when the event has been handled
 */
function once(target, eventName, useCapture=false) {
  info("Waiting for event: '" + eventName + "' on " + target + ".");

  let deferred = promise.defer();

  for (let [add, remove] of [
    ["addEventListener", "removeEventListener"],
    ["addListener", "removeListener"],
    ["on", "off"]
  ]) {
    if ((add in target) && (remove in target)) {
      target[add](eventName, function onEvent(...aArgs) {
        info("Got event: '" + eventName + "' on " + target + ".");
        target[remove](eventName, onEvent, useCapture);
        deferred.resolve.apply(deferred, aArgs);
      }, useCapture);
      break;
    }
  }

  return deferred.promise;
}

/**
 * Check to see if the inspector menu items for editing are disabled.
 * Things like Edit As HTML, Delete Node, etc.
 * @param {NodeFront} nodeFront
 * @param {InspectorPanel} inspector
 * @param {Boolean} assert Should this function run assertions inline.
 * @return A promise that resolves with a boolean indicating whether
 *         the menu items are disabled once the menu has been checked.
 */
let isEditingMenuDisabled = Task.async(function*(nodeFront, inspector, assert=true) {
  let deleteMenuItem = inspector.panelDoc.getElementById("node-menu-delete");
  let editHTMLMenuItem = inspector.panelDoc.getElementById("node-menu-edithtml");
  let pasteHTMLMenuItem = inspector.panelDoc.getElementById("node-menu-pasteouterhtml");

  // To ensure clipboard contains something to paste.
  clipboard.set("<p>test</p>", "html");

  let menu = inspector.nodemenu;
  yield selectNode(nodeFront, inspector);
  yield reopenMenu(menu);

  let isDeleteMenuDisabled = deleteMenuItem.hasAttribute("disabled");
  let isEditHTMLMenuDisabled = editHTMLMenuItem.hasAttribute("disabled");
  let isPasteHTMLMenuDisabled = pasteHTMLMenuItem.hasAttribute("disabled");

  if (assert) {
    ok(isDeleteMenuDisabled, "Delete menu item is disabled");
    ok(isEditHTMLMenuDisabled, "Edit HTML menu item is disabled");
    ok(isPasteHTMLMenuDisabled, "Paste HTML menu item is disabled");
  }

  return isDeleteMenuDisabled && isEditHTMLMenuDisabled && isPasteHTMLMenuDisabled;
});

/**
 * Check to see if the inspector menu items for editing are enabled.
 * Things like Edit As HTML, Delete Node, etc.
 * @param {NodeFront} nodeFront
 * @param {InspectorPanel} inspector
 * @param {Boolean} assert Should this function run assertions inline.
 * @return A promise that resolves with a boolean indicating whether
 *         the menu items are enabled once the menu has been checked.
 */
let isEditingMenuEnabled = Task.async(function*(nodeFront, inspector, assert=true) {
  let deleteMenuItem = inspector.panelDoc.getElementById("node-menu-delete");
  let editHTMLMenuItem = inspector.panelDoc.getElementById("node-menu-edithtml");
  let pasteHTMLMenuItem = inspector.panelDoc.getElementById("node-menu-pasteouterhtml");

  // To ensure clipboard contains something to paste.
  clipboard.set("<p>test</p>", "html");

  let menu = inspector.nodemenu;
  yield selectNode(nodeFront, inspector);
  yield reopenMenu(menu);

  let isDeleteMenuDisabled = deleteMenuItem.hasAttribute("disabled");
  let isEditHTMLMenuDisabled = editHTMLMenuItem.hasAttribute("disabled");
  let isPasteHTMLMenuDisabled = pasteHTMLMenuItem.hasAttribute("disabled");

  if (assert) {
    ok(!isDeleteMenuDisabled, "Delete menu item is enabled");
    ok(!isEditHTMLMenuDisabled, "Edit HTML menu item is enabled");
    ok(!isPasteHTMLMenuDisabled, "Paste HTML menu item is enabled");
  }

  return !isDeleteMenuDisabled && !isEditHTMLMenuDisabled && !isPasteHTMLMenuDisabled;
});

/**
 * Open a menu (closing it first if necessary).
 * @param {DOMNode} menu A menu that implements hidePopup/openPopup
 * @return a promise that resolves once the menu is opened.
 */
let reopenMenu = Task.async(function*(menu) {
  // First close it is if it is already opened.
  if (menu.state == "closing" || menu.state == "open") {
    let popuphidden = once(menu, "popuphidden", true);
    menu.hidePopup();
    yield popuphidden;
  }

  // Then open it and return once
  let popupshown = once(menu, "popupshown", true);
  menu.openPopup();
  yield popupshown;
});

/**
 * Wait for all current promises to be resolved. See this as executeSoon that
 * can be used with yield.
 */
function promiseNextTick() {
  let deferred = promise.defer();
  executeSoon(deferred.resolve);
  return deferred.promise;
}

/**
 * Collapses the current text selection in an input field and tabs to the next
 * field.
 */
function collapseSelectionAndTab(inspector) {
  EventUtils.sendKey("tab", inspector.panelWin); // collapse selection and move caret to end
  EventUtils.sendKey("tab", inspector.panelWin); // next element
}

/**
 * Collapses the current text selection in an input field and tabs to the
 * previous field.
 */
function collapseSelectionAndShiftTab(inspector) {
  EventUtils.synthesizeKey("VK_TAB", { shiftKey: true },
    inspector.panelWin); // collapse selection and move caret to end
  EventUtils.synthesizeKey("VK_TAB", { shiftKey: true },
    inspector.panelWin); // previous element
}

/**
 * Check that the current focused element is an attribute element in the markup
 * view.
 * @param {String} attrName The attribute name expected to be found
 * @param {Boolean} editMode Whether or not the attribute should be in edit mode
 */
function checkFocusedAttribute(attrName, editMode) {
  let focusedAttr = Services.focus.focusedElement;
  is(focusedAttr ? focusedAttr.parentNode.dataset.attr : undefined,
    attrName, attrName + " attribute editor is currently focused.");
  is(focusedAttr ? focusedAttr.tagName : undefined,
    editMode ? "input": "span",
    editMode ? attrName + " is in edit mode" : attrName + " is not in edit mode");
}
