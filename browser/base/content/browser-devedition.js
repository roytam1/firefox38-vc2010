# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

/**
 * Listeners for the DevEdition theme.  This adds an extra stylesheet
 * to browser.xul if a pref is set and no other themes are applied.
 */
let DevEdition = {
  _prefName: "browser.devedition.theme.enabled",
  _themePrefName: "general.skins.selectedSkin",
  _lwThemePrefName: "lightweightThemes.isThemeSelected",
  _devtoolsThemePrefName: "devtools.theme",

  styleSheetLocation: "chrome://browser/skin/devedition.css",
  styleSheet: null,

  init: function () {
    this._updateDevtoolsThemeAttribute();
    this._updateStyleSheetFromPrefs();

    // Listen for changes to all prefs except for complete themes.
    // No need for this since changing a complete theme requires a
    // restart.
    Services.prefs.addObserver(this._lwThemePrefName, this, false);
    Services.prefs.addObserver(this._prefName, this, false);
    Services.prefs.addObserver(this._devtoolsThemePrefName, this, false);
    Services.obs.addObserver(this, "lightweight-theme-styling-update", false);
  },

  observe: function (subject, topic, data) {
    if (topic == "lightweight-theme-styling-update") {
      let newTheme = JSON.parse(data);
      if (!newTheme) {
        // A lightweight theme has been unapplied, so just re-read prefs.
        this._updateStyleSheetFromPrefs();
      } else {
        // A lightweight theme has been applied, but the pref may not be
        // set yet if this happened from customize menu or addons page.
        this._toggleStyleSheet(false);
      }
    }

    if (topic == "nsPref:changed") {
      if (data == this._devtoolsThemePrefName) {
        this._updateDevtoolsThemeAttribute();
      } else {
        this._updateStyleSheetFromPrefs();
      }
    }
  },

  _inferBrightness: function() {
    ToolbarIconColor.inferFromText();
    // Get an inverted full screen button if the dark theme is applied.
    if (this.styleSheet &&
        document.documentElement.getAttribute("devtoolstheme") == "dark") {
      document.documentElement.setAttribute("brighttitlebarforeground", "true");
    } else {
      document.documentElement.removeAttribute("brighttitlebarforeground");
    }
  },

  _updateDevtoolsThemeAttribute: function() {
    // Set an attribute on root element to make it possible
    // to change colors based on the selected devtools theme.
    let devtoolsTheme = Services.prefs.getCharPref(this._devtoolsThemePrefName);
    if (devtoolsTheme != "dark") {
      devtoolsTheme = "light";
    }
    document.documentElement.setAttribute("devtoolstheme", devtoolsTheme);
    this._inferBrightness();
    this._updateStyleSheetFromPrefs();
  },

  _updateStyleSheetFromPrefs: function() {
    let lightweightThemeSelected = false;
    try {
      lightweightThemeSelected = Services.prefs.getBoolPref(this._lwThemePrefName);
    } catch(e) {}

    let defaultThemeSelected = false;
    try {
       defaultThemeSelected = Services.prefs.getCharPref(this._themePrefName) == "classic/1.0";
    } catch(e) {}

    let deveditionThemeEnabled = Services.prefs.getBoolPref(this._prefName) &&
      !lightweightThemeSelected && defaultThemeSelected;

    this._toggleStyleSheet(deveditionThemeEnabled);
  },

  handleEvent: function(e) {
    if (e.type === "load") {
      this.styleSheet.removeEventListener("load", this);
      gBrowser.tabContainer._positionPinnedTabs();
      this._inferBrightness();
      Services.obs.notifyObservers(window, "devedition-theme-state-changed", true);
    }
  },

  _toggleStyleSheet: function(deveditionThemeEnabled) {
    if (deveditionThemeEnabled && !this.styleSheet) {
      let styleSheetAttr = `href="${this.styleSheetLocation}" type="text/css"`;
      this.styleSheet = document.createProcessingInstruction(
        'xml-stylesheet', styleSheetAttr);
      this.styleSheet.addEventListener("load", this);
      document.insertBefore(this.styleSheet, document.documentElement);
      // NB: we'll notify observers once the stylesheet has fully loaded, see
      // handleEvent above.
    } else if (!deveditionThemeEnabled && this.styleSheet) {
      this.styleSheet.removeEventListener("load", this);
      this.styleSheet.remove();
      this.styleSheet = null;
      gBrowser.tabContainer._positionPinnedTabs();
      this._inferBrightness();
      Services.obs.notifyObservers(window, "devedition-theme-state-changed", false);
    }
  },

  uninit: function () {
    Services.prefs.removeObserver(this._lwThemePrefName, this);
    Services.prefs.removeObserver(this._prefName, this);
    Services.prefs.removeObserver(this._devtoolsThemePrefName, this);
    Services.obs.removeObserver(this, "lightweight-theme-styling-update", false);
    if (this.styleSheet) {
      this.styleSheet.removeEventListener("load", this);
    }
    this.styleSheet = null;
  }
};
