/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if invalid filter types are sanitized when loaded from the preferences.
 */

const BASIC_REQUESTS = [
  { url: "sjs_content-type-test-server.sjs?fmt=html&res=undefined" },
  { url: "sjs_content-type-test-server.sjs?fmt=css" },
  { url: "sjs_content-type-test-server.sjs?fmt=js" },
];

const REQUESTS_WITH_MEDIA = BASIC_REQUESTS.concat([
  { url: "sjs_content-type-test-server.sjs?fmt=font" },
  { url: "sjs_content-type-test-server.sjs?fmt=image" },
  { url: "sjs_content-type-test-server.sjs?fmt=audio" },
  { url: "sjs_content-type-test-server.sjs?fmt=video" },
]);

const REQUESTS_WITH_MEDIA_AND_FLASH = REQUESTS_WITH_MEDIA.concat([
  { url: "sjs_content-type-test-server.sjs?fmt=flash" },
]);

const REQUESTS_WITH_MEDIA_AND_FLASH_AND_WS = REQUESTS_WITH_MEDIA_AND_FLASH.concat([
  /* "Upgrade" is a reserved header and can not be set on XMLHttpRequest */
  { url: "sjs_content-type-test-server.sjs?fmt=ws" },
]);

add_task(function* () {
  Services.prefs.setCharPref("devtools.netmonitor.filters",
                             '["bogus", "js", "alsobogus"]');

  let { monitor } = yield initNetMonitor(FILTERING_URL);
  info("Starting test... ");

  let { gStore, windowRequire } = monitor.panelWin;
  let Actions = windowRequire("devtools/client/netmonitor/actions/index");
  let { Prefs } = windowRequire("devtools/client/netmonitor/utils/prefs");

  gStore.dispatch(Actions.batchEnable(false));

  is(Prefs.filters.length, 1,
    "Only the valid filter types should be loaded, the others should be ignored");
  is(Prefs.filters[0], "js",
    "The only filter type is correct.");

  let wait = waitForNetworkEvents(monitor, 9);
  loadCommonFrameScript();
  yield performRequestsInContent(REQUESTS_WITH_MEDIA_AND_FLASH_AND_WS);
  yield wait;

  testFilterButtons(monitor, "js");
  ok(true, "Only the correct filter type was taken into consideration.");

  yield teardown(monitor);

  let filters = Services.prefs.getCharPref("devtools.netmonitor.filters");
  is(filters, '["js"]',
    "The bogus filter type was ignored and removed from the preferences.");
});
