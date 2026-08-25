/* Frame — shared chrome-surface bootstrap.
 *
 * Every surface runs this first. It pulls the layout constants and this
 * surface's position from the browser process and writes them into CSS custom
 * properties, so no surface hardcodes geometry that C++ already owns.
 */
(function (global) {
  'use strict';

  // Channel names, kept in one place rather than spelled out at call sites —
  // the same discipline the C++ side applies to its constants.
  var CHANNEL = {
    layout: 'frame:layout',
    shell: 'frame:shell',
    windowState: 'frame:window:state',
    browserState: 'browser:state',
    minimize: 'frame:window:minimize',
    maximize: 'frame:window:maximize',
    close: 'frame:window:close',
    dragRegions: 'frame:dragregions:',

    sidebarToggle: 'sidebar:toggle',
    tabCreate: 'tab:create',
    tabClose: 'tab:close:',
    tabSelect: 'tab:select:',
    tabReorder: 'tab:reorder:',
    navBack: 'nav:back',
    navForward: 'nav:forward',
    navReload: 'nav:reload',
    navStop: 'nav:stop',
    navGo: 'nav:go:',
    favoriteAdd: 'favorite:add:',
    favoriteRemove: 'favorite:remove:',
    favoriteMove: 'favorite:move:'
  };

  function query(request) {
    return new Promise(function (resolve, reject) {
      if (typeof global.cefQuery !== 'function') {
        reject(new Error('bridge unavailable'));
        return;
      }
      global.cefQuery({
        request: request,
        persistent: false,
        onSuccess: function (response) {
          // State queries answer with JSON; commands just acknowledge with a
          // plain "ok". Treating a non-JSON reply as a failure turned every
          // command into an unhandled rejection.
          try {
            resolve(JSON.parse(response));
          } catch (err) {
            resolve(response);
          }
        },
        onFailure: function (code, message) {
          reject(new Error(message + ' (' + code + ')'));
        }
      });
    });
  }

  function px(name, value) {
    document.documentElement.style.setProperty(name, value + 'px');
  }

  // Called by the browser process whenever the window resizes or this surface
  // moves. The shell gradient is anchored to the window, so stale metrics leave
  // it laid out for the old size and it visibly stops part-way down.
  function onShellMetrics(shell) {
    // A surface narrower than its content is collapsed, not clipped: the
    // sidebar becomes an 8px rail and should render as one rather than as a
    // full sidebar with everything cut off.
    document.documentElement.classList.toggle(
      'is-rail', shell.surfaceWidth > 0 && shell.surfaceWidth <= 16);
    px('--shell-x', shell.surfaceX);
    px('--shell-y', shell.surfaceY);
    px('--win-w', shell.windowWidth);
    px('--win-h', shell.windowHeight);
  }

  // Resolves once the surface knows its geometry. Surfaces await this before
  // reporting themselves ready.
  function bootstrap() {
    return Promise.all([query(CHANNEL.layout), query(CHANNEL.shell)])
      .then(function (results) {
        var layout = results[0];
        var shell = results[1];

        px('--topbar-height', layout.topbarHeight);
        px('--sidebar-width', layout.sidebarWidth);
        px('--shell-inset', layout.shellInset);
        px('--viewport-radius', layout.viewportRadius);
        px('--tab-min-width', layout.tabMinWidth);
        px('--tab-max-width', layout.tabMaxWidth);
        px('--tab-gap', layout.tabGap);
        px('--new-tab-width', layout.newTabWidth);

        // Offsets that keep the shell gradient continuous across surfaces.
        onShellMetrics(shell);

        // Pull the current browser state rather than waiting for a push: by
        // the time a surface finishes loading, every push worth having may
        // already have gone out.
        return query(CHANNEL.browserState).then(function (state) {
          if (state && state.tabs) {
            global.FrameShell.onBrowserState(state);
          }
          return { layout: layout, shell: shell, browser: state };
        });
      });
  }

  // Tells the window which parts of the caption strip must NOT drag it.
  // Only the surface knows where its controls ended up after layout, so it has
  // to report them rather than C++ guessing.
  function reportDragRegions(elements) {
    var parts = [];
    for (var i = 0; i < elements.length; i++) {
      var el = elements[i];
      if (!el) {
        continue;
      }
      var r = el.getBoundingClientRect();
      if (r.width <= 0 || r.height <= 0) {
        continue;
      }
      parts.push(Math.round(r.left) + ',' + Math.round(r.top) + ',' +
                 Math.round(r.width) + ',' + Math.round(r.height));
    }
    // Flat list rather than JSON so the browser process needs no parser.
    return query(CHANNEL.dragRegions + parts.join(';'));
  }

  // All three are overwritten by whichever surface cares. The browser process
  // calls them directly — surfaces never poll for state.
  function onWindowState() {}
  function onBrowserState() {}
  // Ctrl+L and friends, implemented by the sidebar. A no-op here so the
  // browser process can call it unconditionally without knowing which surface
  // owns the address field.
  function onFocusAddress() {}

  // Fire-and-forget command. Nothing useful comes back from these beyond
  // acknowledgement, and a failed command should not break the surface.
  function send(channel, payload) {
    return query(payload === undefined ? channel : channel + payload)
      .catch(function () { /* command refused; surface stays usable */ });
  }

  global.FrameShell = {
    CHANNEL: CHANNEL,
    query: query,
    bootstrap: bootstrap,
    reportDragRegions: reportDragRegions,
    send: send,
    onShellMetrics: onShellMetrics,
    onWindowState: onWindowState,
    onBrowserState: onBrowserState,
    onFocusAddress: onFocusAddress
  };
})(window);
