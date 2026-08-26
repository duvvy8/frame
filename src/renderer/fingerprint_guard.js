/* Frame — fingerprint minimisation, applied at V8 context creation.
 *
 * This is NOT injected into the page in the usual sense. It runs from
 * CefRenderProcessHandler::OnContextCreated, in the renderer, once per context,
 * before any page script exists. There is no per-request cost, no MutationObserver,
 * no polling, and nothing for a page to see in its own DOM.
 *
 * DESIGN RULE: standardise and minimise, never randomise.
 *
 * Randomising these values per page or per session makes a browser MORE
 * identifiable, not less -- a machine reporting a different CPU count every
 * visit is a machine running an anti-fingerprinting browser, which is itself a
 * rare and therefore identifying trait. Worse, randomised values contradict
 * each other: 2 cores next to a 4GB JS heap next to a workstation GPU is a
 * combination no real machine has.
 *
 * So every value below is either the spec-standard cap or the most common real
 * value, and each one stays consistent with the others and across reloads.
 */
(function () {
  'use strict';

  // Anything that throws here would break the page, and a privacy feature that
  // breaks pages gets turned off. Every step is individually guarded.
  function define(target, name, value) {
    try {
      var existing = Object.getOwnPropertyDescriptor(target, name);
      if (existing && !existing.configurable) {
        return;
      }
      Object.defineProperty(target, name, {
        get: function () { return value; },
        configurable: true,
        enumerable: existing ? existing.enumerable : true
      });
    } catch (e) { /* leave the real value rather than break the page */ }
  }

  // --- navigator.deviceMemory -------------------------------------------
  //
  // The spec says this is clamped to 8. Frame was reporting 32, the machine's
  // real RAM, which is not just high entropy -- it is a value Chrome cannot
  // produce, so it identified Frame specifically. 8 is both the standard cap
  // and what every machine with 8GB or more already reports.
  if ('deviceMemory' in Navigator.prototype) {
    define(Navigator.prototype, 'deviceMemory', 8);
  }

  // --- navigator.hardwareConcurrency -------------------------------------
  //
  // Capped, not faked. A 16-thread desktop is unusual enough to narrow a
  // fingerprint sharply; 8 is the most common value on the desktop web and is
  // never higher than the truth, so nothing can be caught claiming more
  // parallelism than it has.
  var real = navigator.hardwareConcurrency;
  if (typeof real === 'number' && real > 8) {
    define(Navigator.prototype, 'hardwareConcurrency', 8);
  }

  // --- WebGL vendor and renderer -----------------------------------------
  //
  // The renderer string carried "NVIDIA GeForce RTX 5070 Ti (0x00002C05)
  // Direct3D11" -- an exact model and PCI device ID. On a card that rare it is
  // close to a unique identifier on its own.
  //
  // The GPU FAMILY is kept and the model dropped. Reporting Intel on an NVIDIA
  // machine would be the contradiction this file exists to avoid: the timing
  // and capability of the real device would disagree with the claim, and a
  // fingerprinter that checks is better off than one that does not. Keeping the
  // family true and the model vague removes the entropy without lying about
  // anything measurable.
  var GENERIC = [
    [/nvidia/i, 'ANGLE (NVIDIA, NVIDIA GeForce Direct3D11 vs_5_0 ps_5_0, D3D11)'],
    [/amd|radeon/i, 'ANGLE (AMD, AMD Radeon Direct3D11 vs_5_0 ps_5_0, D3D11)'],
    [/intel/i, 'ANGLE (Intel, Intel(R) HD Graphics Direct3D11 vs_5_0 ps_5_0, D3D11)']
  ];

  function generalise(value) {
    if (typeof value !== 'string') {
      return value;
    }
    for (var i = 0; i < GENERIC.length; i++) {
      if (GENERIC[i][0].test(value)) {
        return GENERIC[i][1];
      }
    }
    // An unrecognised adapter still loses its device id, which is the part
    // that identifies a specific board rather than a model line.
    return value.replace(/\s*\(0x[0-9A-Fa-f]{4,}\)/g, '');
  }

  function patch(proto) {
    if (!proto || !proto.getParameter) {
      return;
    }
    var original = proto.getParameter;
    try {
      proto.getParameter = function (parameter) {
        var value = original.call(this, parameter);
        // 37445 UNMASKED_VENDOR_WEBGL, 37446 UNMASKED_RENDERER_WEBGL.
        // These only exist behind WEBGL_debug_renderer_info, which is exactly
        // the extension fingerprinters ask for.
        if (parameter === 37445) {
          return 'Google Inc.';
        }
        if (parameter === 37446) {
          return generalise(value);
        }
        return value;
      };
      // Keep it looking like the native function it replaces; a bare
      // toString() disclosing "function (parameter)" is itself a signal.
      proto.getParameter.toString = function () {
        return 'function getParameter() { [native code] }';
      };
    } catch (e) { /* as above */ }
  }

  if (typeof WebGLRenderingContext !== 'undefined') {
    patch(WebGLRenderingContext.prototype);
  }
  if (typeof WebGL2RenderingContext !== 'undefined') {
    patch(WebGL2RenderingContext.prototype);
  }
})();
