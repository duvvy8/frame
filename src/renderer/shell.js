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
    minimize: 'frame:window:minimize',
    maximize: 'frame:window:maximize',
    close: 'frame:window:close',
    dragRegions: 'frame:dragregions:'
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
          try {
            resolve(JSON.parse(response));
          } catch (err) {
            reject(err);
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

  // Resolves once the surface knows its geometry. Surfaces await this before
  // reporting themselves ready.
  function bootstrap() {
    return Promise.all([query(CHANNEL.layout), query(CHANNEL.shell)])
      .then(function (results) {
        var layout = results[0];
        var shell = results[1];

        px('--topbar-height', layout.topbarHeight);
        px('--sidebar-width', layout.sidebarWidth);
        px('--viewport-radius', layout.viewportRadius);
        px('--tab-min-width', layout.tabMinWidth);
        px('--tab-max-width', layout.tabMaxWidth);
        px('--tab-gap', layout.tabGap);
        px('--new-tab-width', layout.newTabWidth);

        // Offsets that keep the shell gradient continuous across surfaces.
        px('--shell-x', shell.surfaceX);
        px('--shell-y', shell.surfaceY);
        px('--win-w', shell.windowWidth);
        px('--win-h', shell.windowHeight);

        return { layout: layout, shell: shell };
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

  // Overwritten by a surface that cares. The browser process calls this
  // directly when the window is maximised or restored.
  function onWindowState() {}

  global.FrameShell = {
    CHANNEL: CHANNEL,
    query: query,
    bootstrap: bootstrap,
    reportDragRegions: reportDragRegions,
    onWindowState: onWindowState
  };
})(window);
