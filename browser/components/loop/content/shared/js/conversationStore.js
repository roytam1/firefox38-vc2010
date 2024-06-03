/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global loop:true */

var loop = loop || {};
loop.store = loop.store || {};

(function() {
  var sharedActions = loop.shared.actions;
  var CALL_TYPES = loop.shared.utils.CALL_TYPES;
  var REST_ERRNOS = loop.shared.utils.REST_ERRNOS;
  var FAILURE_DETAILS = loop.shared.utils.FAILURE_DETAILS;

  /**
   * Websocket states taken from:
   * https://docs.services.mozilla.com/loop/apis.html#call-progress-state-change-progress
   */
  var WS_STATES = loop.store.WS_STATES = {
    // The call is starting, and the remote party is not yet being alerted.
    INIT: "init",
    // The called party is being alerted.
    ALERTING: "alerting",
    // The call is no longer being set up and has been aborted for some reason.
    TERMINATED: "terminated",
    // The called party has indicated that he has answered the call,
    // but the media is not yet confirmed.
    CONNECTING: "connecting",
    // One of the two parties has indicated successful media set up,
    // but the other has not yet.
    HALF_CONNECTED: "half-connected",
    // Both endpoints have reported successfully establishing media.
    CONNECTED: "connected"
  };

  var CALL_STATES = loop.store.CALL_STATES = {
    // The initial state of the view.
    INIT: "cs-init",
    // The store is gathering the call data from the server.
    GATHER: "cs-gather",
    // The initial data has been gathered, the websocket is connecting, or has
    // connected, and waiting for the other side to connect to the server.
    CONNECTING: "cs-connecting",
    // The websocket has received information that we're now alerting
    // the peer.
    ALERTING: "cs-alerting",
    // The call is ongoing.
    ONGOING: "cs-ongoing",
    // The call ended successfully.
    FINISHED: "cs-finished",
    // The user has finished with the window.
    CLOSE: "cs-close",
    // The call was terminated due to an issue during connection.
    TERMINATED: "cs-terminated"
  };

  /**
   * Conversation store.
   *
   * @param {loop.Dispatcher} dispatcher  The dispatcher for dispatching actions
   *                                      and registering to consume actions.
   * @param {Object} options Options object:
   * - {client}           client           The client object.
   * - {mozLoop}          mozLoop          The MozLoop API object.
   * - {loop.OTSdkDriver} loop.OTSdkDriver The SDK Driver
   */
  loop.store.ConversationStore = loop.store.createStore({
    // Further actions are registered in setupWindowData when
    // we know what window type this is.
    actions: [
      "setupWindowData"
    ],

    getInitialStoreState: function() {
      return {
        // The id of the window. Currently used for getting the window id.
        windowId: undefined,
        // The current state of the call
        callState: CALL_STATES.INIT,
        // The reason if a call was terminated
        callStateReason: undefined,
        // True if the call is outgoing, false if not, undefined if unknown
        outgoing: undefined,
        // The contact being called for outgoing calls
        contact: undefined,
        // The call type for the call.
        // XXX Don't hard-code, this comes from the data in bug 1072323
        callType: CALL_TYPES.AUDIO_VIDEO,
        // A link for emailing once obtained from the server
        emailLink: undefined,

        // Call Connection information
        // The call id from the loop-server
        callId: undefined,
        // The caller id of the contacting side
        callerId: undefined,
        // The connection progress url to connect the websocket
        progressURL: undefined,
        // The websocket token that allows connection to the progress url
        websocketToken: undefined,
        // SDK API key
        apiKey: undefined,
        // SDK session ID
        sessionId: undefined,
        // SDK session token
        sessionToken: undefined,
        // If the audio is muted
        audioMuted: false,
        // If the video is muted
        videoMuted: false
      };
    },

    /**
     * Handles initialisation of the store.
     *
     * @param  {Object} options    Options object.
     */
    initialize: function(options) {
      options = options || {};

      if (!options.client) {
        throw new Error("Missing option client");
      }
      if (!options.sdkDriver) {
        throw new Error("Missing option sdkDriver");
      }
      if (!options.mozLoop) {
        throw new Error("Missing option mozLoop");
      }

      this.client = options.client;
      this.sdkDriver = options.sdkDriver;
      this.mozLoop = options.mozLoop;
      this._isDesktop = options.isDesktop || false;
    },

    /**
     * Handles the connection failure action, setting the state to
     * terminated.
     *
     * @param {sharedActions.ConnectionFailure} actionData The action data.
     */
    connectionFailure: function(actionData) {
      /**
       * XXX This is a workaround for desktop machines that do not have a
       * camera installed. As we don't yet have device enumeration, when
       * we do, this can be removed (bug 1138851), and the sdk should handle it.
       */
      if (this._isDesktop &&
          actionData.reason === FAILURE_DETAILS.UNABLE_TO_PUBLISH_MEDIA &&
          this.getStoreState().videoMuted === false) {
        // We failed to publish with media, so due to the bug, we try again without
        // video.
        this.setStoreState({videoMuted: true});
        this.sdkDriver.retryPublishWithoutVideo();
        return;
      }

      this._endSession();
      this.setStoreState({
        callState: CALL_STATES.TERMINATED,
        callStateReason: actionData.reason
      });
    },

    /**
     * Handles the connection progress action, setting the next state
     * appropriately.
     *
     * @param {sharedActions.ConnectionProgress} actionData The action data.
     */
    connectionProgress: function(actionData) {
      var state = this.getStoreState();

      switch(actionData.wsState) {
        case WS_STATES.INIT: {
          if (state.callState === CALL_STATES.GATHER) {
            this.setStoreState({callState: CALL_STATES.CONNECTING});
          }
          break;
        }
        case WS_STATES.ALERTING: {
          this.setStoreState({callState: CALL_STATES.ALERTING});
          break;
        }
        case WS_STATES.CONNECTING: {
          this.sdkDriver.connectSession({
            apiKey: state.apiKey,
            sessionId: state.sessionId,
            sessionToken: state.sessionToken
          });
          this.mozLoop.addConversationContext(
            state.windowId,
            state.sessionId,
            state.callId);
          this.setStoreState({callState: CALL_STATES.ONGOING});
          break;
        }
        case WS_STATES.HALF_CONNECTED:
        case WS_STATES.CONNECTED: {
          this.setStoreState({callState: CALL_STATES.ONGOING});
          break;
        }
        default: {
          console.error("Unexpected websocket state passed to connectionProgress:",
            actionData.wsState);
        }
      }
    },

    setupWindowData: function(actionData) {
      var windowType = actionData.type;
      if (windowType !== "outgoing" &&
          windowType !== "incoming") {
        // Not for this store, don't do anything.
        return;
      }

      this.dispatcher.register(this, [
        "connectionFailure",
        "connectionProgress",
        "connectCall",
        "hangupCall",
        "remotePeerDisconnected",
        "cancelCall",
        "retryCall",
        "mediaConnected",
        "setMute",
        "fetchRoomEmailLink"
      ]);

      this.setStoreState({
        contact: actionData.contact,
        outgoing: windowType === "outgoing",
        windowId: actionData.windowId,
        callType: actionData.callType,
        callState: CALL_STATES.GATHER,
        videoMuted: actionData.callType === CALL_TYPES.AUDIO_ONLY
      });

      if (this.getStoreState("outgoing")) {
        this._setupOutgoingCall();
      } // XXX Else, other types aren't supported yet.
    },

    /**
     * Handles the connect call action, this saves the appropriate
     * data and starts the connection for the websocket to notify the
     * server of progress.
     *
     * @param {sharedActions.ConnectCall} actionData The action data.
     */
    connectCall: function(actionData) {
      this.setStoreState(actionData.sessionData);
      this._connectWebSocket();
    },

    /**
     * Hangs up an ongoing call.
     */
    hangupCall: function() {
      if (this._websocket) {
        // Let the server know the user has hung up.
        this._websocket.mediaFail();
      }

      this._endSession();
      this.setStoreState({callState: CALL_STATES.FINISHED});
    },

    /**
     * The remote peer disconnected from the session.
     *
     * @param {sharedActions.RemotePeerDisconnected} actionData
     */
    remotePeerDisconnected: function(actionData) {
      this._endSession();

      // If the peer hungup, we end normally, otherwise
      // we treat this as a call failure.
      if (actionData.peerHungup) {
        this.setStoreState({callState: CALL_STATES.FINISHED});
      } else {
        this.setStoreState({
          callState: CALL_STATES.TERMINATED,
          callStateReason: "peerNetworkDisconnected"
        });
      }
    },

    /**
     * Cancels a call
     */
    cancelCall: function() {
      var callState = this.getStoreState("callState");
      if (this._websocket &&
          (callState === CALL_STATES.CONNECTING ||
           callState === CALL_STATES.ALERTING)) {
         // Let the server know the user has hung up.
        this._websocket.cancel();
      }

      this._endSession();
      this.setStoreState({callState: CALL_STATES.CLOSE});
    },

    /**
     * Retries a call
     */
    retryCall: function() {
      var callState = this.getStoreState("callState");
      if (callState !== CALL_STATES.TERMINATED) {
        console.error("Unexpected retry in state", callState);
        return;
      }

      this.setStoreState({callState: CALL_STATES.GATHER});
      if (this.getStoreState("outgoing")) {
        this._setupOutgoingCall();
      }
    },

    /**
     * Notifies that all media is now connected
     */
    mediaConnected: function() {
      this._websocket.mediaUp();
    },

    /**
     * Records the mute state for the stream.
     *
     * @param {sharedActions.setMute} actionData The mute state for the stream type.
     */
    setMute: function(actionData) {
      var newState = {};
      newState[actionData.type + "Muted"] = !actionData.enabled;
      this.setStoreState(newState);
    },

    /**
     * Fetches a new room URL intended to be sent over email when a contact
     * can't be reached.
     */
    fetchRoomEmailLink: function(actionData) {
      this.mozLoop.rooms.create({
        roomName: actionData.roomName,
        roomOwner: actionData.roomOwner,
        maxSize:   loop.store.MAX_ROOM_CREATION_SIZE,
        expiresIn: loop.store.DEFAULT_EXPIRES_IN
      }, function(err, createdRoomData) {
        if (err) {
          this.trigger("error:emailLink");
          return;
        }
        this.setStoreState({"emailLink": createdRoomData.roomUrl});
      }.bind(this));
    },

    /**
     * Obtains the outgoing call data from the server and handles the
     * result.
     */
    _setupOutgoingCall: function() {
      var contactAddresses = [];
      var contact = this.getStoreState("contact");

      this.mozLoop.calls.setCallInProgress(this.getStoreState("windowId"));

      function appendContactValues(property, strip) {
        if (contact.hasOwnProperty(property)) {
          contact[property].forEach(function(item) {
            if (strip) {
              contactAddresses.push(item.value
                .replace(/^(\+)?(.*)$/g, function(m, prefix, number) {
                  return (prefix || "") + number.replace(/[\D]+/g, "");
                }));
            } else {
              contactAddresses.push(item.value);
            }
          });
        }
      }

      appendContactValues("email");
      appendContactValues("tel", true);

      this.client.setupOutgoingCall(contactAddresses,
        this.getStoreState("callType"),
        function(err, result) {
          if (err) {
            console.error("Failed to get outgoing call data", err);
            var failureReason = "setup";
            if (err.errno == REST_ERRNOS.USER_UNAVAILABLE) {
              failureReason = REST_ERRNOS.USER_UNAVAILABLE;
            }
            this.dispatcher.dispatch(
              new sharedActions.ConnectionFailure({reason: failureReason}));
            return;
          }

          // Success, dispatch a new action.
          this.dispatcher.dispatch(
            new sharedActions.ConnectCall({sessionData: result}));
        }.bind(this)
      );
    },

    /**
     * Sets up and connects the websocket to the server. The websocket
     * deals with sending and obtaining status via the server about the
     * setup of the call.
     */
    _connectWebSocket: function() {
      this._websocket = new loop.CallConnectionWebSocket({
        url: this.getStoreState("progressURL"),
        callId: this.getStoreState("callId"),
        websocketToken: this.getStoreState("websocketToken")
      });

      this._websocket.promiseConnect().then(
        function(progressState) {
          this.dispatcher.dispatch(new sharedActions.ConnectionProgress({
            // This is the websocket call state, i.e. waiting for the
            // other end to connect to the server.
            wsState: progressState
          }));
        }.bind(this),
        function(error) {
          console.error("Websocket failed to connect", error);
          this.dispatcher.dispatch(new sharedActions.ConnectionFailure({
            reason: "websocket-setup"
          }));
        }.bind(this)
      );

      this.listenTo(this._websocket, "progress", this._handleWebSocketProgress);
    },

    /**
     * Ensures the session is ended and the websocket is disconnected.
     */
    _endSession: function(nextState) {
      this.sdkDriver.disconnectSession();
      if (this._websocket) {
        this.stopListening(this._websocket);

        // Now close the websocket.
        this._websocket.close();
        delete this._websocket;
      }

      this.mozLoop.calls.clearCallInProgress(
        this.getStoreState("windowId"));
    },

    /**
     * Used to handle any progressed received from the websocket. This will
     * dispatch new actions so that the data can be handled appropriately.
     */
    _handleWebSocketProgress: function(progressData) {
      var action;

      switch(progressData.state) {
        case WS_STATES.TERMINATED: {
          action = new sharedActions.ConnectionFailure({
            reason: progressData.reason
          });
          break;
        }
        default: {
          action = new sharedActions.ConnectionProgress({
            wsState: progressData.state
          });
          break;
        }
      }

      this.dispatcher.dispatch(action);
    }
  });
})();
