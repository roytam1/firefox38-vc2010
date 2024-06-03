/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

var expect = chai.expect;

describe("loop.store.ConversationStore", function () {
  "use strict";

  var CALL_STATES = loop.store.CALL_STATES;
  var WS_STATES = loop.store.WS_STATES;
  var WEBSOCKET_REASONS = loop.shared.utils.WEBSOCKET_REASONS;
  var FAILURE_DETAILS = loop.shared.utils.FAILURE_DETAILS;
  var sharedActions = loop.shared.actions;
  var sharedUtils = loop.shared.utils;
  var sandbox, dispatcher, client, store, fakeSessionData, sdkDriver;
  var contact, fakeMozLoop;
  var connectPromise, resolveConnectPromise, rejectConnectPromise;
  var wsCancelSpy, wsCloseSpy, wsMediaUpSpy, fakeWebsocket;

  function checkFailures(done, f) {
    try {
      f();
      done();
    } catch (err) {
      done(err);
    }
  }

  beforeEach(function() {
    sandbox = sinon.sandbox.create();

    contact = {
      name: [ "Mr Smith" ],
      email: [{
        type: "home",
        value: "fakeEmail",
        pref: true
      }]
    };

    fakeMozLoop = {
      getLoopPref: sandbox.stub(),
      addConversationContext: sandbox.stub(),
      calls: {
        setCallInProgress: sandbox.stub(),
        clearCallInProgress: sandbox.stub()
      },
      rooms: {
        create: sandbox.stub()
      }
    };

    dispatcher = new loop.Dispatcher();
    client = {
      setupOutgoingCall: sinon.stub(),
      requestCallUrl: sinon.stub()
    };
    sdkDriver = {
      connectSession: sinon.stub(),
      disconnectSession: sinon.stub(),
      retryPublishWithoutVideo: sinon.stub()
    };

    wsCancelSpy = sinon.spy();
    wsCloseSpy = sinon.spy();
    wsMediaUpSpy = sinon.spy();

    fakeWebsocket = {
      cancel: wsCancelSpy,
      close: wsCloseSpy,
      mediaUp: wsMediaUpSpy
    };

    store = new loop.store.ConversationStore(dispatcher, {
      client: client,
      mozLoop: fakeMozLoop,
      sdkDriver: sdkDriver
    });
    fakeSessionData = {
      apiKey: "fakeKey",
      callId: "142536",
      sessionId: "321456",
      sessionToken: "341256",
      websocketToken: "543216",
      windowId: "28",
      progressURL: "fakeURL"
    };

    var dummySocket = {
      close: sinon.spy(),
      send: sinon.spy()
    };

    connectPromise = new Promise(function(resolve, reject) {
      resolveConnectPromise = resolve;
      rejectConnectPromise = reject;
    });

    sandbox.stub(loop.CallConnectionWebSocket.prototype,
      "promiseConnect").returns(connectPromise);
  });

  afterEach(function() {
    sandbox.restore();
  });

  describe("#initialize", function() {
    it("should throw an error if the client is missing", function() {
      expect(function() {
        new loop.store.ConversationStore(dispatcher, {
          sdkDriver: sdkDriver
        });
      }).to.Throw(/client/);
    });

    it("should throw an error if the sdkDriver is missing", function() {
      expect(function() {
        new loop.store.ConversationStore(dispatcher, {
          client: client
        });
      }).to.Throw(/sdkDriver/);
    });

    it("should throw an error if mozLoop is missing", function() {
      expect(function() {
        new loop.store.ConversationStore(dispatcher, {
          sdkDriver: sdkDriver,
          client: client
        });
      }).to.Throw(/mozLoop/);
    });
  });

  describe("#connectionFailure", function() {
    beforeEach(function() {
      store._websocket = fakeWebsocket;
      store.setStoreState({windowId: "42"});
    });

    it("should retry publishing if on desktop, and in the videoMuted state", function() {
      store._isDesktop = true;

      store.connectionFailure(new sharedActions.ConnectionFailure({
        reason: FAILURE_DETAILS.UNABLE_TO_PUBLISH_MEDIA
      }));

      sinon.assert.calledOnce(sdkDriver.retryPublishWithoutVideo);
    });

    it("should set videoMuted to try when retrying publishing", function() {
      store._isDesktop = true;

      store.connectionFailure(new sharedActions.ConnectionFailure({
        reason: FAILURE_DETAILS.UNABLE_TO_PUBLISH_MEDIA
      }));

      expect(store.getStoreState().videoMuted).eql(true);
    });

    it("should disconnect the session", function() {
      store.connectionFailure(
        new sharedActions.ConnectionFailure({reason: "fake"}));

      sinon.assert.calledOnce(sdkDriver.disconnectSession);
    });

    it("should ensure the websocket is closed", function() {
      store.connectionFailure(
        new sharedActions.ConnectionFailure({reason: "fake"}));

      sinon.assert.calledOnce(wsCloseSpy);
    });

    it("should set the state to 'terminated'", function() {
      store.setStoreState({callState: CALL_STATES.ALERTING});

      store.connectionFailure(
        new sharedActions.ConnectionFailure({reason: "fake"}));

      expect(store.getStoreState("callState")).eql(CALL_STATES.TERMINATED);
      expect(store.getStoreState("callStateReason")).eql("fake");
    });

    it("should release mozLoop callsData", function() {
      store.connectionFailure(
        new sharedActions.ConnectionFailure({reason: "fake"}));

      sinon.assert.calledOnce(fakeMozLoop.calls.clearCallInProgress);
      sinon.assert.calledWithExactly(
        fakeMozLoop.calls.clearCallInProgress, "42");
    });
  });

  describe("#connectionProgress", function() {
    describe("progress: init", function() {
      it("should change the state from 'gather' to 'connecting'", function() {
        store.setStoreState({callState: CALL_STATES.GATHER});

        store.connectionProgress(
          new sharedActions.ConnectionProgress({wsState: WS_STATES.INIT}));

        expect(store.getStoreState("callState")).eql(CALL_STATES.CONNECTING);
      });
    });

    describe("progress: alerting", function() {
      it("should change the state from 'gather' to 'alerting'", function() {
        store.setStoreState({callState: CALL_STATES.GATHER});

        store.connectionProgress(
          new sharedActions.ConnectionProgress({wsState: WS_STATES.ALERTING}));

        expect(store.getStoreState("callState")).eql(CALL_STATES.ALERTING);
      });

      it("should change the state from 'init' to 'alerting'", function() {
        store.setStoreState({callState: CALL_STATES.INIT});

        store.connectionProgress(
          new sharedActions.ConnectionProgress({wsState: WS_STATES.ALERTING}));

        expect(store.getStoreState("callState")).eql(CALL_STATES.ALERTING);
      });
    });

    describe("progress: connecting", function() {
      beforeEach(function() {
        store.setStoreState({callState: CALL_STATES.ALERTING});
      });

      it("should change the state to 'ongoing'", function() {
        store.connectionProgress(
          new sharedActions.ConnectionProgress({wsState: WS_STATES.CONNECTING}));

        expect(store.getStoreState("callState")).eql(CALL_STATES.ONGOING);
      });

      it("should connect the session", function() {
        store.setStoreState(fakeSessionData);

        store.connectionProgress(
          new sharedActions.ConnectionProgress({wsState: WS_STATES.CONNECTING}));

        sinon.assert.calledOnce(sdkDriver.connectSession);
        sinon.assert.calledWithExactly(sdkDriver.connectSession, {
          apiKey: "fakeKey",
          sessionId: "321456",
          sessionToken: "341256"
        });
      });

      it("should call mozLoop.addConversationContext", function() {
        store.setStoreState(fakeSessionData);

        store.connectionProgress(
          new sharedActions.ConnectionProgress({wsState: WS_STATES.CONNECTING}));

        sinon.assert.calledOnce(fakeMozLoop.addConversationContext);
        sinon.assert.calledWithExactly(fakeMozLoop.addConversationContext,
                                       "28", "321456", "142536");
      });
    });
  });

  describe("#setupWindowData", function() {
    var fakeSetupWindowData;

    beforeEach(function() {
      store.setStoreState({callState: CALL_STATES.INIT});
      fakeSetupWindowData = {
        windowId: "123456",
        type: "outgoing",
        contact: contact,
        callType: sharedUtils.CALL_TYPES.AUDIO_VIDEO
      };
    });

    it("should set the state to 'gather'", function() {
      dispatcher.dispatch(
        new sharedActions.SetupWindowData(fakeSetupWindowData));

      expect(store.getStoreState("callState")).eql(CALL_STATES.GATHER);
    });

    it("should save the basic call information", function() {
      dispatcher.dispatch(
        new sharedActions.SetupWindowData(fakeSetupWindowData));

      expect(store.getStoreState("windowId")).eql("123456");
      expect(store.getStoreState("outgoing")).eql(true);
    });

    it("should save the basic information from the mozLoop api", function() {
      dispatcher.dispatch(
        new sharedActions.SetupWindowData(fakeSetupWindowData));

      expect(store.getStoreState("contact")).eql(contact);
      expect(store.getStoreState("callType"))
        .eql(sharedUtils.CALL_TYPES.AUDIO_VIDEO);
    });

    describe("outgoing calls", function() {
      it("should request the outgoing call data", function() {
        dispatcher.dispatch(
          new sharedActions.SetupWindowData(fakeSetupWindowData));

        sinon.assert.calledOnce(client.setupOutgoingCall);
        sinon.assert.calledWith(client.setupOutgoingCall,
          ["fakeEmail"], sharedUtils.CALL_TYPES.AUDIO_VIDEO);
      });

      it("should include all email addresses in the call data", function() {
        fakeSetupWindowData.contact = {
          name: [ "Mr Smith" ],
          email: [{
            type: "home",
            value: "fakeEmail",
            pref: true
          },
          {
            type: "work",
            value: "emailFake",
            pref: false
          }]
        };

        dispatcher.dispatch(
          new sharedActions.SetupWindowData(fakeSetupWindowData));

        sinon.assert.calledOnce(client.setupOutgoingCall);
        sinon.assert.calledWith(client.setupOutgoingCall,
          ["fakeEmail", "emailFake"], sharedUtils.CALL_TYPES.AUDIO_VIDEO);
      });

      it("should include trim phone numbers for the call data", function() {
        fakeSetupWindowData.contact = {
          name: [ "Mr Smith" ],
          tel: [{
            type: "home",
            value: "+44-5667+345 496(2335)45+ 456+",
            pref: true
          }]
        };

        dispatcher.dispatch(
          new sharedActions.SetupWindowData(fakeSetupWindowData));

        sinon.assert.calledOnce(client.setupOutgoingCall);
        sinon.assert.calledWith(client.setupOutgoingCall,
          ["+445667345496233545456"], sharedUtils.CALL_TYPES.AUDIO_VIDEO);
      });

      it("should include all email and telephone values in the call data", function() {
        fakeSetupWindowData.contact = {
          name: [ "Mr Smith" ],
          email: [{
            type: "home",
            value: "fakeEmail",
            pref: true
          }, {
            type: "work",
            value: "emailFake",
            pref: false
          }],
          tel: [{
            type: "work",
            value: "01234567890",
            pref: false
          }, {
            type: "home",
            value: "09876543210",
            pref: false
          }]
        };

        dispatcher.dispatch(
          new sharedActions.SetupWindowData(fakeSetupWindowData));

        sinon.assert.calledOnce(client.setupOutgoingCall);
        sinon.assert.calledWith(client.setupOutgoingCall,
          ["fakeEmail", "emailFake", "01234567890", "09876543210"],
          sharedUtils.CALL_TYPES.AUDIO_VIDEO);
      });

      describe("server response handling", function() {
        beforeEach(function() {
          sandbox.stub(dispatcher, "dispatch");
        });

        it("should dispatch a connect call action on success", function() {
          var callData = {
            apiKey: "fakeKey"
          };

          client.setupOutgoingCall.callsArgWith(2, null, callData);

          store.setupWindowData(
            new sharedActions.SetupWindowData(fakeSetupWindowData));

          sinon.assert.calledOnce(dispatcher.dispatch);
          // Can't use instanceof here, as that matches any action
          sinon.assert.calledWithMatch(dispatcher.dispatch,
            sinon.match.hasOwn("name", "connectCall"));
          sinon.assert.calledWithMatch(dispatcher.dispatch,
            sinon.match.hasOwn("sessionData", callData));
        });

        it("should dispatch a connection failure action on failure", function() {
          client.setupOutgoingCall.callsArgWith(2, {});

          store.setupWindowData(
            new sharedActions.SetupWindowData(fakeSetupWindowData));

          sinon.assert.calledOnce(dispatcher.dispatch);
          // Can't use instanceof here, as that matches any action
          sinon.assert.calledWithMatch(dispatcher.dispatch,
            sinon.match.hasOwn("name", "connectionFailure"));
          sinon.assert.calledWithMatch(dispatcher.dispatch,
            sinon.match.hasOwn("reason", "setup"));
        });
      });
    });
  });

  describe("#connectCall", function() {
    it("should save the call session data", function() {
      store.connectCall(
        new sharedActions.ConnectCall({sessionData: fakeSessionData}));

      expect(store.getStoreState("apiKey")).eql("fakeKey");
      expect(store.getStoreState("callId")).eql("142536");
      expect(store.getStoreState("sessionId")).eql("321456");
      expect(store.getStoreState("sessionToken")).eql("341256");
      expect(store.getStoreState("websocketToken")).eql("543216");
      expect(store.getStoreState("progressURL")).eql("fakeURL");
    });

    it("should initialize the websocket", function() {
      sandbox.stub(loop, "CallConnectionWebSocket").returns({
        promiseConnect: function() { return connectPromise; },
        on: sinon.spy()
      });

      store.connectCall(
        new sharedActions.ConnectCall({sessionData: fakeSessionData}));

      sinon.assert.calledOnce(loop.CallConnectionWebSocket);
      sinon.assert.calledWithExactly(loop.CallConnectionWebSocket, {
        url: "fakeURL",
        callId: "142536",
        websocketToken: "543216"
      });
    });

    it("should connect the websocket to the server", function() {
      store.connectCall(
        new sharedActions.ConnectCall({sessionData: fakeSessionData}));

      sinon.assert.calledOnce(store._websocket.promiseConnect);
    });

    describe("WebSocket connection result", function() {
      beforeEach(function() {
        store.connectCall(
          new sharedActions.ConnectCall({sessionData: fakeSessionData}));

        sandbox.stub(dispatcher, "dispatch");
      });

      it("should dispatch a connection progress action on success", function(done) {
        resolveConnectPromise(WS_STATES.INIT);

        connectPromise.then(function() {
          checkFailures(done, function() {
            sinon.assert.calledOnce(dispatcher.dispatch);
            // Can't use instanceof here, as that matches any action
            sinon.assert.calledWithMatch(dispatcher.dispatch,
              sinon.match.hasOwn("name", "connectionProgress"));
            sinon.assert.calledWithMatch(dispatcher.dispatch,
              sinon.match.hasOwn("wsState", WS_STATES.INIT));
          });
        }, function() {
          done(new Error("Promise should have been resolve, not rejected"));
        });
      });

      it("should dispatch a connection failure action on failure", function(done) {
        rejectConnectPromise();

        connectPromise.then(function() {
          done(new Error("Promise should have been rejected, not resolved"));
        }, function() {
          checkFailures(done, function() {
            sinon.assert.calledOnce(dispatcher.dispatch);
            // Can't use instanceof here, as that matches any action
            sinon.assert.calledWithMatch(dispatcher.dispatch,
              sinon.match.hasOwn("name", "connectionFailure"));
            sinon.assert.calledWithMatch(dispatcher.dispatch,
              sinon.match.hasOwn("reason", "websocket-setup"));
           });
        });
      });
    });
  });

  describe("#hangupCall", function() {
    var wsMediaFailSpy, wsCloseSpy;
    beforeEach(function() {
      wsMediaFailSpy = sinon.spy();
      wsCloseSpy = sinon.spy();

      store._websocket = {
        mediaFail: wsMediaFailSpy,
        close: wsCloseSpy
      };
      store.setStoreState({callState: CALL_STATES.ONGOING});
      store.setStoreState({windowId: "42"});
    });

    it("should disconnect the session", function() {
      store.hangupCall(new sharedActions.HangupCall());

      sinon.assert.calledOnce(sdkDriver.disconnectSession);
    });

    it("should send a media-fail message to the websocket if it is open", function() {
      store.hangupCall(new sharedActions.HangupCall());

      sinon.assert.calledOnce(wsMediaFailSpy);
    });

    it("should ensure the websocket is closed", function() {
      store.hangupCall(new sharedActions.HangupCall());

      sinon.assert.calledOnce(wsCloseSpy);
    });

    it("should set the callState to finished", function() {
      store.hangupCall(new sharedActions.HangupCall());

      expect(store.getStoreState("callState")).eql(CALL_STATES.FINISHED);
    });

    it("should release mozLoop callsData", function() {
      store.hangupCall(new sharedActions.HangupCall());

      sinon.assert.calledOnce(fakeMozLoop.calls.clearCallInProgress);
      sinon.assert.calledWithExactly(
        fakeMozLoop.calls.clearCallInProgress, "42");
    });
  });

  describe("#remotePeerDisconnected", function() {
    var wsMediaFailSpy, wsCloseSpy;
    beforeEach(function() {
      wsMediaFailSpy = sinon.spy();
      wsCloseSpy = sinon.spy();

      store._websocket = {
        mediaFail: wsMediaFailSpy,
        close: wsCloseSpy
      };
      store.setStoreState({callState: CALL_STATES.ONGOING});
      store.setStoreState({windowId: "42"});
    });

    it("should disconnect the session", function() {
      store.remotePeerDisconnected(new sharedActions.RemotePeerDisconnected({
        peerHungup: true
      }));

      sinon.assert.calledOnce(sdkDriver.disconnectSession);
    });

    it("should ensure the websocket is closed", function() {
      store.remotePeerDisconnected(new sharedActions.RemotePeerDisconnected({
        peerHungup: true
      }));

      sinon.assert.calledOnce(wsCloseSpy);
    });

    it("should release mozLoop callsData", function() {
      store.remotePeerDisconnected(new sharedActions.RemotePeerDisconnected({
        peerHungup: true
      }));

      sinon.assert.calledOnce(fakeMozLoop.calls.clearCallInProgress);
      sinon.assert.calledWithExactly(
        fakeMozLoop.calls.clearCallInProgress, "42");
    });

    it("should set the callState to finished if the peer hungup", function() {
      store.remotePeerDisconnected(new sharedActions.RemotePeerDisconnected({
        peerHungup: true
      }));

      expect(store.getStoreState("callState")).eql(CALL_STATES.FINISHED);
    });

    it("should set the callState to terminated if the peer was disconnected" +
      "for an unintentional reason", function() {
        store.remotePeerDisconnected(new sharedActions.RemotePeerDisconnected({
          peerHungup: false
        }));

        expect(store.getStoreState("callState")).eql(CALL_STATES.TERMINATED);
      });

    it("should set the reason to peerNetworkDisconnected if the peer was" +
      "disconnected for an unintentional reason", function() {
        store.remotePeerDisconnected(new sharedActions.RemotePeerDisconnected({
          peerHungup: false
        }));

        expect(store.getStoreState("callStateReason"))
          .eql("peerNetworkDisconnected");
    });
  });

  describe("#cancelCall", function() {
    beforeEach(function() {
      store._websocket = fakeWebsocket;

      store.setStoreState({callState: CALL_STATES.CONNECTING});
      store.setStoreState({windowId: "42"});
    });

    it("should disconnect the session", function() {
      store.cancelCall(new sharedActions.CancelCall());

      sinon.assert.calledOnce(sdkDriver.disconnectSession);
    });

    it("should send a cancel message to the websocket if it is open", function() {
      store.cancelCall(new sharedActions.CancelCall());

      sinon.assert.calledOnce(wsCancelSpy);
    });

    it("should ensure the websocket is closed", function() {
      store.cancelCall(new sharedActions.CancelCall());

      sinon.assert.calledOnce(wsCloseSpy);
    });

    it("should set the state to close if the call is connecting", function() {
      store.cancelCall(new sharedActions.CancelCall());

      expect(store.getStoreState("callState")).eql(CALL_STATES.CLOSE);
    });

    it("should set the state to close if the call has terminated already", function() {
      store.setStoreState({callState: CALL_STATES.TERMINATED});

      store.cancelCall(new sharedActions.CancelCall());

      expect(store.getStoreState("callState")).eql(CALL_STATES.CLOSE);
    });

    it("should release mozLoop callsData", function() {
      store.cancelCall(new sharedActions.CancelCall());

      sinon.assert.calledOnce(fakeMozLoop.calls.clearCallInProgress);
      sinon.assert.calledWithExactly(
        fakeMozLoop.calls.clearCallInProgress, "42");
    });
  });

  describe("#retryCall", function() {
    it("should set the state to gather", function() {
      store.setStoreState({callState: CALL_STATES.TERMINATED});

      store.retryCall(new sharedActions.RetryCall());

      expect(store.getStoreState("callState"))
        .eql(CALL_STATES.GATHER);
    });

    it("should request the outgoing call data", function() {
      store.setStoreState({
        callState: CALL_STATES.TERMINATED,
        outgoing: true,
        callType: sharedUtils.CALL_TYPES.AUDIO_VIDEO,
        contact: contact
      });

      store.retryCall(new sharedActions.RetryCall());

      sinon.assert.calledOnce(client.setupOutgoingCall);
      sinon.assert.calledWith(client.setupOutgoingCall,
        ["fakeEmail"], sharedUtils.CALL_TYPES.AUDIO_VIDEO);
    });
  });

  describe("#mediaConnected", function() {
    it("should send mediaUp via the websocket", function() {
      store._websocket = fakeWebsocket;

      store.mediaConnected(new sharedActions.MediaConnected());

      sinon.assert.calledOnce(wsMediaUpSpy);
    });
  });

  describe("#setMute", function() {
    it("should save the mute state for the audio stream", function() {
      store.setStoreState({"audioMuted": false});

      dispatcher.dispatch(new sharedActions.SetMute({
        type: "audio",
        enabled: true
      }));

      expect(store.getStoreState("audioMuted")).eql(false);
    });

    it("should save the mute state for the video stream", function() {
      store.setStoreState({"videoMuted": true});

      dispatcher.dispatch(new sharedActions.SetMute({
        type: "video",
        enabled: false
      }));

      expect(store.getStoreState("videoMuted")).eql(true);
    });
  });

  describe("#fetchRoomEmailLink", function() {
    it("should request a new call url to the server", function() {
      store.fetchRoomEmailLink(new sharedActions.FetchRoomEmailLink({
        roomOwner: "bob@invalid.tld",
        roomName: "FakeRoomName"
      }));

      sinon.assert.calledOnce(fakeMozLoop.rooms.create);
      sinon.assert.calledWithMatch(fakeMozLoop.rooms.create, {
        roomOwner: "bob@invalid.tld",
        roomName: "FakeRoomName"
      });
    });

    it("should update the emailLink attribute when the new room url is received",
      function() {
        fakeMozLoop.rooms.create = function(roomData, cb) {
          cb(null, {roomUrl: "http://fake.invalid/"});
        };
        store.fetchRoomEmailLink(new sharedActions.FetchRoomEmailLink({
          roomOwner: "bob@invalid.tld",
          roomName: "FakeRoomName"
        }));

        expect(store.getStoreState("emailLink")).eql("http://fake.invalid/");
      });

    it("should trigger an error:emailLink event in case of failure",
      function() {
        var trigger = sandbox.stub(store, "trigger");
        fakeMozLoop.rooms.create = function(roomData, cb) {
          cb(new Error("error"));
        };
        store.fetchRoomEmailLink(new sharedActions.FetchRoomEmailLink({
          roomOwner: "bob@invalid.tld",
          roomName: "FakeRoomName"
        }));

        sinon.assert.calledOnce(trigger);
        sinon.assert.calledWithExactly(trigger, "error:emailLink");
      });
  });

  describe("Events", function() {
    describe("Websocket progress", function() {
      beforeEach(function() {
        store.connectCall(
          new sharedActions.ConnectCall({sessionData: fakeSessionData}));

        sandbox.stub(dispatcher, "dispatch");
      });

      it("should dispatch a connection failure action on 'terminate'", function() {
        store._websocket.trigger("progress", {
          state: WS_STATES.TERMINATED,
          reason: WEBSOCKET_REASONS.REJECT
        });

        sinon.assert.calledOnce(dispatcher.dispatch);
        // Can't use instanceof here, as that matches any action
        sinon.assert.calledWithMatch(dispatcher.dispatch,
          sinon.match.hasOwn("name", "connectionFailure"));
        sinon.assert.calledWithMatch(dispatcher.dispatch,
          sinon.match.hasOwn("reason", WEBSOCKET_REASONS.REJECT));
      });

      it("should dispatch a connection progress action on 'alerting'", function() {
        store._websocket.trigger("progress", {state: WS_STATES.ALERTING});

        sinon.assert.calledOnce(dispatcher.dispatch);
        // Can't use instanceof here, as that matches any action
        sinon.assert.calledWithMatch(dispatcher.dispatch,
          sinon.match.hasOwn("name", "connectionProgress"));
        sinon.assert.calledWithMatch(dispatcher.dispatch,
          sinon.match.hasOwn("wsState", WS_STATES.ALERTING));
      });
    });
  });
});
