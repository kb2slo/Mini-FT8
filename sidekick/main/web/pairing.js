// Shared pairing for every sidekick page (RFC 0004 section 7).
//
// Served at /pairing.js. Pages include it with <script src=/pairing.js></script>
// and call MiniFT8Pairing.boot(...) so re-auth is the same gesture everywhere:
// press the sidekick button; this script claims the token while the disclosure
// window is open. No copy-paste, no prompt().
//
// The device-side window is one-shot: the first successful GET closes it, so a
// successful re-auth does not leave the secret readable for the rest of the
// old 120 s timer. The NVS token itself is not rotated -- that would log out
// every other already-paired browser.

(function (global) {
  var KEY = 'minift8_token';
  var HEADER = 'X-MiniFT8-Token';
  var POLL_MS = 1000;
  var polling = false;
  var statusEl = null;
  var onPaired = null;

  function getToken() {
    var t = localStorage.getItem(KEY);
    return t ? t.trim() : '';
  }

  function setToken(t) {
    if (t) localStorage.setItem(KEY, String(t).trim());
  }

  function clearToken() {
    localStorage.removeItem(KEY);
  }

  function setStatus(msg) {
    if (statusEl) statusEl.textContent = msg;
  }

  function authHeaders(extra) {
    var h = extra ? Object.assign({}, extra) : {};
    h[HEADER] = getToken();
    return h;
  }

  function authFetch(url, init) {
    init = init || {};
    init.headers = authHeaders(init.headers || {});
    return fetch(url, init);
  }

  async function tryClaim() {
    var r = await fetch('/api/pairing-token');
    if (r.status === 404) return null;
    if (!r.ok) return null;
    var j = await r.json();
    return j && j.token ? String(j.token).trim() : null;
  }

  async function pollUntilPaired() {
    if (polling) return;
    polling = true;
    setStatus('Press the sidekick button to pair');
    try {
      for (;;) {
        if (getToken()) {
          if (onPaired) onPaired(getToken());
          return;
        }
        try {
          var tok = await tryClaim();
          if (tok) {
            setToken(tok);
            setStatus('Paired');
            if (onPaired) onPaired(tok);
            return;
          }
        } catch (e) { /* keep waiting */ }
        setStatus('Press the sidekick button to pair');
        await new Promise(function (resolve) { setTimeout(resolve, POLL_MS) });
      }
    } finally {
      polling = false;
    }
  }

  // If a token is already stored, fire onPaired once. Otherwise start polling
  // for a button press. Safe to call from every page.
  function boot(opts) {
    opts = opts || {};
    statusEl = opts.status || null;
    onPaired = opts.onPaired || null;
    if (getToken()) {
      if (onPaired) onPaired(getToken());
      return;
    }
    pollUntilPaired();
  }

  // Drop a stored token that the server rejected, then wait for a new press.
  function needsReauth() {
    clearToken();
    pollUntilPaired();
  }

  global.MiniFT8Pairing = {
    KEY: KEY,
    HEADER: HEADER,
    getToken: getToken,
    setToken: setToken,
    clearToken: clearToken,
    authHeaders: authHeaders,
    authFetch: authFetch,
    boot: boot,
    needsReauth: needsReauth
  };
})(window);
