/* Frame — shared behaviour for the internal pages.
 *
 * settings, history, downloads and bookmarks are ordinary tabs served over
 * frame://, which means they reach the browser process through exactly the
 * same window.cefQuery bridge the chrome surfaces use. Nothing here is a
 * second mechanism.
 *
 * What IS here is the small amount of machinery all four need and none of them
 * should each reinvent: a query wrapper that fails loudly, list rendering that
 * never puts untrusted text through innerHTML, and formatting for the two
 * things every one of these pages shows — a timestamp and a byte count.
 */
(function (global) {
  'use strict';

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
            resolve(response);
          }
        },
        onFailure: function (code, message) {
          reject(new Error(message + ' (' + code + ')'));
        }
      });
    });
  }

  // --- formatting -----------------------------------------------------------

  var DAY_MS = 86400000;

  // Relative for anything recent, absolute once "3 days ago" stops being more
  // useful than the date itself.
  function formatWhen(ms) {
    if (!ms) {
      return '';
    }
    var when = new Date(ms);
    var now = new Date();
    var startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();

    var time = when.toLocaleTimeString(undefined, { hour: 'numeric', minute: '2-digit' });
    if (ms >= startOfToday) {
      return time;
    }
    if (ms >= startOfToday - DAY_MS) {
      return 'Yesterday ' + time;
    }
    if (ms >= startOfToday - 6 * DAY_MS) {
      return when.toLocaleDateString(undefined, { weekday: 'long' }) + ' ' + time;
    }
    return when.toLocaleDateString(undefined, { day: 'numeric', month: 'short', year: 'numeric' });
  }

  // Which day-group a timestamp belongs to, for the history page's headings.
  function dayLabel(ms) {
    var when = new Date(ms);
    var now = new Date();
    var startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
    if (ms >= startOfToday) { return 'Today'; }
    if (ms >= startOfToday - DAY_MS) { return 'Yesterday'; }
    return when.toLocaleDateString(undefined,
      { weekday: 'long', day: 'numeric', month: 'long' });
  }

  function formatBytes(bytes) {
    if (!bytes || bytes < 0) {
      return '';
    }
    var units = ['B', 'KB', 'MB', 'GB', 'TB'];
    var index = 0;
    var value = bytes;
    while (value >= 1024 && index < units.length - 1) {
      value /= 1024;
      index++;
    }
    // One decimal below 10, none above: "1.4 MB" and "247 MB" both read
    // cleanly, "1.4 B" and "247.3 MB" do not.
    var text = value < 10 && index > 0 ? value.toFixed(1) : Math.round(value);
    return text + ' ' + units[index];
  }

  // The bit of a URL worth reading at a glance.
  function prettyUrl(url) {
    return String(url || '').replace(/^https?:\/\//, '').replace(/\/$/, '');
  }

  function hostOf(url) {
    try {
      return new URL(String(url)).hostname;
    } catch (err) {
      return '';
    }
  }

  // --- DOM ------------------------------------------------------------------

  // Every element on these pages goes through here.
  //
  // `text` is set with textContent, never innerHTML. Page titles and URLs come
  // from the sites themselves and are hostile input — a history page that
  // renders a title as markup is a stored-XSS hole in the one origin that can
  // read the browser's own settings.
  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) {
      node.className = className;
    }
    if (text !== undefined && text !== null) {
      node.textContent = String(text);
    }
    return node;
  }

  function clear(node) {
    while (node.firstChild) {
      node.removeChild(node.firstChild);
    }
  }

  // The one place these pages say "there is nothing here". Written once so
  // four pages cannot each describe emptiness slightly differently.
  function emptyState(container, title, detail) {
    clear(container);
    var box = el('div', 'empty');
    box.appendChild(el('p', 'empty-title', title));
    if (detail) {
      box.appendChild(el('p', 'empty-detail', detail));
    }
    container.appendChild(box);
  }

  // A failure the user can see. The alternative — a page that silently stays
  // empty — is indistinguishable from having no data, which is the single most
  // misleading thing a page like this can do.
  function errorState(container, message) {
    clear(container);
    var box = el('div', 'empty is-error');
    box.appendChild(el('p', 'empty-title', 'Could not reach the browser'));
    box.appendChild(el('p', 'empty-detail', message || ''));
    container.appendChild(box);
  }

  // Debounce for the search fields. A query per keystroke is a round trip per
  // keystroke into a store that is read synchronously on the UI thread.
  function debounce(fn, ms) {
    var timer = 0;
    return function () {
      var args = arguments;
      var self = this;
      clearTimeout(timer);
      timer = setTimeout(function () { fn.apply(self, args); }, ms);
    };
  }

  global.FrameInternal = {
    query: query,
    el: el,
    clear: clear,
    emptyState: emptyState,
    errorState: errorState,
    debounce: debounce,
    formatWhen: formatWhen,
    dayLabel: dayLabel,
    formatBytes: formatBytes,
    prettyUrl: prettyUrl,
    hostOf: hostOf
  };
})(window);
