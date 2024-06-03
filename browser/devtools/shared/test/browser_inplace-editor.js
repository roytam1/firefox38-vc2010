/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let {Promise: promise} = Cu.import("resource://gre/modules/Promise.jsm", {});
let {editableField, getInplaceEditorForSpan: inplaceEditor} = devtools.require("devtools/shared/inplace-editor");

// Test the inplace-editor behavior.

add_task(function*() {
  yield promiseTab("data:text/html;charset=utf-8,inline editor tests");
  let [host, win, doc] = yield createHost();

  yield testReturnCommit(doc);
  yield testBlurCommit(doc);
  yield testAdvanceCharCommit(doc);

  host.destroy();
  gBrowser.removeCurrentTab();
});

function testReturnCommit(doc) {
  info("Testing that pressing return commits the new value");
  let def = promise.defer();

  createInplaceEditorAndClick({
    initial: "explicit initial",
    start: function(editor) {
      is(editor.input.value, "explicit initial", "Explicit initial value should be used.");
      editor.input.value = "Test Value";
      EventUtils.sendKey("return");
    },
    done: onDone("Test Value", true, def)
  }, doc);

  return def.promise;
}

function testBlurCommit(doc) {
  info("Testing that bluring the field commits the new value");
  let def = promise.defer();

  createInplaceEditorAndClick({
    start: function(editor) {
      is(editor.input.value, "Edit Me!", "textContent of the span used.");
      editor.input.value = "Test Value";
      editor.input.blur();
    },
    done: onDone("Test Value", true, def)
  }, doc);

  return def.promise;
}

function testAdvanceCharCommit(doc) {
  info("Testing that configured advanceChars commit the new value");
  let def = promise.defer();

  createInplaceEditorAndClick({
    advanceChars: ":",
    start: function(editor) {
      let input = editor.input;
      for each (let ch in "Test:") {
        EventUtils.sendChar(ch);
      }
    },
    done: onDone("Test", true, def)
  }, doc);

  return def.promise;
}

function testEscapeCancel(doc) {
  info("Testing that escape cancels the new value");
  let def = promise.defer();

  createInplaceEditorAndClick({
    initial: "initial text",
    start: function(editor) {
      editor.input.value = "Test Value";
      EventUtils.sendKey("escape");
    },
    done: onDone("initial text", false, def)
  }, doc);

  return def.promise;
}

function onDone(value, isCommit, def) {
  return function(actualValue, actualCommit) {
    info("Inplace-editor's done callback executed, checking its state");
    is(actualValue, value, "The value is correct");
    is(actualCommit, isCommit, "The commit boolean is correct");
    def.resolve();
  }
}

function createInplaceEditorAndClick(options, doc) {
  clearBody(doc);
  let span = options.element = createSpan(doc);

  info("Creating an inplace-editor field");
  editableField(options);

  info("Clicking on the inplace-editor field to turn to edit mode");
  span.click();
}

function clearBody(doc) {
  info("Clearing the page body");
  doc.body.innerHTML = "";
}

function createSpan(doc) {
  info("Creating a new span element");
  let span = doc.createElement("span");
  span.setAttribute("tabindex", "0");
  span.textContent = "Edit Me!";
  doc.body.appendChild(span);
  return span;
}
