// This is a list of all interfaces that are exposed to workers.
// Please only add things to this list with great care and proper review
// from the associated module peers.

// This file lists global interfaces we want exposed and verifies they
// are what we intend. Each entry in the arrays below can either be a
// simple string with the interface name, or an object with a 'name'
// property giving the interface name as a string, and additional
// properties which qualify the exposure of that interface. For example:
//
// [
//   "AGlobalInterface",
//   {name: "ExperimentalThing", release: false},
//   {name: "OptionalThing", pref: "some.thing.enabled"},
// ];
//
// See createInterfaceMap() below for a complete list of properties.

// IMPORTANT: Do not change this list without review from
//            a JavaScript Engine peer!
var ecmaGlobals =
  [
    "Array",
    "ArrayBuffer",
    "Boolean",
    "DataView",
    "Date",
    "Error",
    "EvalError",
    "Float32Array",
    "Float64Array",
    "Function",
    "Infinity",
    "Int16Array",
    "Int32Array",
    "Int8Array",
    "InternalError",
    {name: "Intl", b2g: false, android: false},
    "Iterator",
    "JSON",
    "Map",
    "Math",
    "NaN",
    "Number",
    "Object",
    "Proxy",
    "RangeError",
    "ReferenceError",
    "RegExp",
    "Set",
    {name: "SharedArrayBuffer", nightly: true},
    {name: "SharedInt8Array", nightly: true},
    {name: "SharedUint8Array", nightly: true},
    {name: "SharedUint8ClampedArray", nightly: true},
    {name: "SharedInt16Array", nightly: true},
    {name: "SharedUint16Array", nightly: true},
    {name: "SharedInt32Array", nightly: true},
    {name: "SharedUint32Array", nightly: true},
    {name: "SharedFloat32Array", nightly: true},
    {name: "SharedFloat64Array", nightly: true},
    {name: "SIMD", nightly: true},
    {name: "Atomics", nightly: true},
    "StopIteration",
    "String",
    "Symbol",
    "SyntaxError",
    {name: "TypedObject", nightly: true},
    "TypeError",
    "Uint16Array",
    "Uint32Array",
    "Uint8Array",
    "Uint8ClampedArray",
    "URIError",
    "WeakMap",
    "WeakSet",
  ];
// IMPORTANT: Do not change the list above without review from
//            a JavaScript Engine peer!

// IMPORTANT: Do not change the list below without review from a DOM peer!
var interfaceNamesInGlobalScope =
  [
// IMPORTANT: Do not change this list without review from a DOM peer!
    "Blob",
// IMPORTANT: Do not change this list without review from a DOM peer!
    { name: "BroadcastChannel", pref: "dom.broadcastChannel.enabled" },
// IMPORTANT: Do not change this list without review from a DOM peer!
    { name: "Cache", pref: "dom.caches.enabled" },
// IMPORTANT: Do not change this list without review from a DOM peer!
    { name: "CacheStorage", pref: "dom.caches.enabled" },
// IMPORTANT: Do not change this list without review from a DOM peer!
    "DedicatedWorkerGlobalScope",
// IMPORTANT: Do not change this list without review from a DOM peer!
    { name: "DataStore", b2g: true },
// IMPORTANT: Do not change this list without review from a DOM peer!
    { name: "DataStoreCursor", b2g: true },
// IMPORTANT: Do not change this list without review from a DOM peer!
    "DOMError",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "DOMException",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "DOMStringList",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "Event",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "EventTarget",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "File",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "FileReaderSync",
// IMPORTANT: Do not change this list without review from a DOM peer!
    { name: "Headers", pref: "dom.fetch.enabled" },
// IMPORTANT: Do not change this list without review from a DOM peer!
    "IDBCursor",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "IDBDatabase",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "IDBFactory",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "IDBIndex",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "IDBKeyRange",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "IDBObjectStore",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "IDBOpenDBRequest",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "IDBRequest",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "IDBTransaction",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "IDBVersionChangeEvent",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "ImageData",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "MessageEvent",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "MessagePort",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "Performance",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "Promise",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "TextDecoder",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "TextEncoder",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "XMLHttpRequest",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "XMLHttpRequestEventTarget",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "XMLHttpRequestUpload",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "URL",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "URLSearchParams",
// IMPORTANT: Do not change this list without review from a DOM peer!
   { name: "WebSocket", pref: "dom.workers.websocket.enabled" },
// IMPORTANT: Do not change this list without review from a DOM peer!
    "Worker",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "WorkerGlobalScope",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "WorkerLocation",
// IMPORTANT: Do not change this list without review from a DOM peer!
    "WorkerNavigator",
// IMPORTANT: Do not change this list without review from a DOM peer!
  ];
// IMPORTANT: Do not change the list above without review from a DOM peer!

function createInterfaceMap(prefMap, permissionMap, version, userAgent, isB2G) {
  var isNightly = version.endsWith("a1");
  var isRelease = !version.includes("a");
  var isDesktop = !/Mobile|Tablet/.test(userAgent);
  var isAndroid = !!navigator.userAgent.includes("Android");

  var interfaceMap = {};

  function addInterfaces(interfaces)
  {
    for (var entry of interfaces) {
      if (typeof(entry) === "string") {
        interfaceMap[entry] = true;
      } else if ((entry.nightly === !isNightly) ||
                 (entry.desktop === !isDesktop) ||
                 (entry.android === !isAndroid) ||
                 (entry.b2g === !isB2G) ||
                 (entry.release === !isRelease) ||
                 (entry.pref && !prefMap[entry.pref])  ||
                 (entry.permission && !permissionMap[entry.permission])) {
        interfaceMap[entry.name] = false;
      } else {
        interfaceMap[entry.name] = true;
      }
    }
  }

  addInterfaces(ecmaGlobals);
  addInterfaces(interfaceNamesInGlobalScope);

  return interfaceMap;
}

function runTest(prefMap, permissionMap, version, userAgent, isB2G) {
  var interfaceMap = createInterfaceMap(prefMap, permissionMap, version, userAgent, isB2G);
  for (var name of Object.getOwnPropertyNames(self)) {
    // An interface name should start with an upper case character.
    if (!/^[A-Z]/.test(name)) {
      continue;
    }
    ok(interfaceMap[name],
       "If this is failing: DANGER, are you sure you want to expose the new interface " + name +
       " to all webpages as a property on the worker? Do not make a change to this file without a " +
       " review from a DOM peer for that specific change!!! (or a JS peer for changes to ecmaGlobals)");
    delete interfaceMap[name];
  }
  for (var name of Object.keys(interfaceMap)) {
    ok(name in self === interfaceMap[name],
       name + " should " + (interfaceMap[name] ? "" : " NOT") + " be defined on the global scope");
    if (!interfaceMap[name]) {
      delete interfaceMap[name];
    }
  }
  is(Object.keys(interfaceMap).length, 0,
     "The following interface(s) are not enumerated: " + Object.keys(interfaceMap).join(", "));
}

function appendPrefs(prefs, interfaces) {
  for (var entry of interfaces) {
    if (entry.pref !== undefined && prefs.indexOf(entry.pref) === -1) {
      prefs.push(entry.pref);
    }
  }
}

var prefs = [];
appendPrefs(prefs, ecmaGlobals);
appendPrefs(prefs, interfaceNamesInGlobalScope);

function appendPermissions(permissions, interfaces) {
  for (var entry of interfaces) {
    if (entry.permission !== undefined &&
        permissions.indexOf(entry.permission) === -1) {
      permissions.push(entry.permission);
    }
  }
}

var permissions = [];
appendPermissions(permissions, ecmaGlobals);
appendPermissions(permissions, interfaceNamesInGlobalScope);

workerTestGetPrefs(prefs, function(prefMap) {
  workerTestGetPermissions(permissions, function(permissionMap) {
    workerTestGetVersion(function(version) {
      workerTestGetUserAgent(function(userAgent) {
        workerTestGetIsB2G(function(isB2G) {
          runTest(prefMap, permissionMap, version, userAgent, isB2G);
          workerTestDone();
	});
      });
    });
  });
});
