# RFC 0004: Headless Mini-FT8 — the phone is the UI, the sidekick is a peripheral

* **Status:** Draft. Design agreed in chat 2026-09-10; I28a protocol and I28b pairing are in tree (field proof still owed). **Storage model amended 2026-09-13** (§3/§4): the web root is a LittleFS partition, hydrated from firmware embed, not an empty blob filled only by POST. Supersedes nothing; extends [RFC 0001](0001-ble-companion.md) §5.0/§5.2d, which established WiFi-plus-browser as the phone path and moved sidekick updates from a PORTA push to an HTTPS pull. **§11 extended 2026-09-14** (Log's day-file model and third-party sync, a shared offline/online info-expand pattern, and the phone-grid map handoff) — a wireframe design pass ahead of the next I28d slice, same day as the screen-model lock.
* **Author / Lead:** Jeff Kalikstein, KB2SLO
* **Covers:** where the web application lives, how it is delivered and trusted, how the browser reaches the radio, and what crosses the sidekick-to-main link.
* **Does not cover:** the ESP32-P4 port itself ([I27](../ROADMAP.md), gated on B26), the decode pipeline, or anything about the ADV's existing screen and keyboard — which this design must not depend on and does not remove.
* **Ask:** do not start the web application before the sidekick-to-main protocol exists. The app is the visible half; the protocol is the half that makes it possible.

---

## 1. Why

Mini-FT8's UI is a 240-pixel screen and a thumb keyboard. The north star ([I27](../ROADMAP.md)) is a
headless two-MCU radio with no screen and no keyboard at all, where a phone is the only interface. That is
not a skin over the current UI — it means every operator-reachable function has to leave the device and
arrive in a browser, and the transport that carries it has to be designed rather than grown.

Three constraints were settled before any of this was drawn, and they decide most of what follows:

1. **Offline field operation is a requirement.** POTA sites and summits routinely have no cell service. A
   radio whose UI lives on the internet is a radio with no UI on a summit. **Followed to its conclusion
   2026-09-12, which this RFC had not done: a summit has no router either.** The only path from phone to
   sidekick there is the sidekick's own access point, so this constraint does not merely require surviving
   without internet — it requires **the whole application to run over AP mode**. §5 and
   [I19](../ROADMAP.md) each call that "the AP-mode fallback", which is backwards: station mode is the
   convenience case, and AP mode is the one this first constraint names. Nothing in this design may assume AP
   mode is a transient setup state. [B55](../ROADMAP.md) carries the investigation and the list of ways that
   assumption could get baked in before anyone notices.
2. **Internet may be required for initial station setup.** Not for operating. This is what makes the design
   affordable — the firmware does not need to carry a full UI as a fallback.
3. **No dependency on the ADV's keyboard or screen.** A build that works on P4 must also work on ADV, so the
   protocol is specified against the application, not the board.

## 2. The inversion this rests on

**The browser is the internet-connected device; the sidekick is a local peripheral.**

That is the whole idea, and everything good here follows from it. The phone already has a cellular radio, a
TLS stack, a trust store, a screen, and a browser. The sidekick has none of those and would have to grow
them at a cost measured in flash, maintenance, and failure modes. So it does not: **third-party services are
reached by the phone, never by the device.** QRZ, PSKReporter and map tiles are the browser's problem.

Taken to its conclusion, the sidekick never speaks TLS **at all** — see §4.

```mermaid
flowchart TB
    subgraph net["Internet — only the browser goes here"]
        gh["Bundle host, HTTPS<br/>GitHub Pages today<br/>app + firmware"]
        qrz["QRZ XML<br/>CORS open, called directly"]
        cf["Worker<br/>PSKReporter, filtered"]
        maps["Map tiles"]
    end
    subgraph phone["Phone or computer — the only device with internet"]
        app["Mini-FT8 web app<br/>downloaded from the sidekick, runs here"]
    end
    subgraph sk["Sidekick — no TLS, no CA bundle"]
        http["HTTP server<br/>control API"]
        part["Web FS · LittleFS<br/>sole served origin"]
        embed["Firmware embed<br/>hydrate seed only"]
        sig["Pairing token check<br/>no signature, no key"]
    end
    subgraph main["Main MCU — ADV today, P4 north star"]
        radio["Decode · TX · autoseq · log"]
        qmx["QMX over USB host"]
    end
    gh -. "fetch bundle over TLS, once<br/>this hop authenticates it" .-> app
    qrz -. "enrichment, when online" .-> app
    cf -. "enrichment, when online" .-> app
    maps -. "enrichment, when online" .-> app
    app -- "control · works offline" --> http
    app -- "POST bundle, once" --> sig
    embed -. "hydrate when empty / forced / digest mismatch" .-> part
    sig --> part
    part --> http
    http -- "framed protocol over Port A" --> radio
    radio --> qmx
```

Dashed edges require internet. Solid edges work without it. **Every edge below the phone is solid**, and
keeping it that way is the property to defend as this grows: the moment the sidekick needs one HTTPS call,
the TLS stack, the CA bundle and its expiry come back.

## 3. Where the application lives

**The sidekick serves the whole application; it is not bundled into firmware.**

Both halves of that matter and they were argued separately.

**Served by the sidekick, not loaded from the internet**, because of constraint 1. A page served from
`https://kb2slo.kalikstein.com` *cannot* reach `http://minift8.local` — browsers block active mixed content
with no exception for private addresses, so an internet-hosted app cannot talk to the device at all. The
inverse works: an HTTP page may load HTTPS subresources. But an app that fetches itself from the internet
each session is an app that does not exist on a summit. Serving it locally also makes the whole question
moot, since the app and the API are then same-origin.

The cost is that the page is a **non-secure context**. We lose service workers, the geolocation API, and
Web Serial/Bluetooth. `localStorage` and `fetch` to HTTPS survive. Losing geolocation is the one that stings
— auto-grid from the phone would have been nice — and it is the price of not owning a certificate for a name
that only resolves on one LAN.

**Not the weekly UI inside the OTA image**, because that app should ship on its own cadence and firmware
flash is the wrong vehicle for it. The **served web root** is a LittleFS data partition (`web`) at the
1.9 MB free tail of the 8 MB part, above `ota_1`, leaving both OTA slots and `nvs` untouched. That
filesystem is the **sole HTTP origin** for HTML/JS/CSS once the device is running: what you can list on
the FS is what the device serves — one place to debug.

**Firmware still carries the bootstrap pages** under `sidekick/main/web/` via `EMBED_TXTFILES`, but as a
**hydrate seed**, not as a live second backend. On boot (and on an explicit recovery gesture), if the FS
is empty, unmountable after format, or its seed digest does not match the embed, firmware copies those
bytes onto the FS. After that, HTTP reads the FS only. A phone-relayed **bundle may upgrade the whole
tree**, including pages that began as seed — one mutable web root, not a permanent quarantine under
`/app/` that leaves provision frozen in firmware forever.

Bundle install is **stage → verify manifest digests → atomic promote**, never “erase the partition and
hope.” Present-but-broken files after a bad POST do not look “missing,” so hydrate-on-empty alone will
not heal them: recovery is force re-hydrate from embed (or USB-C reflash). Random NOR bit-rot is not the
concern; sharing a write path with the only UI is, and staging plus force re-hydrate bound it.

Assets in the downloaded app are stored pre-compressed and served with `Content-Encoding: gzip`; a
disciplined app is 100–300 KB gzipped, so the budget is comfortable but not unlimited. **No bundled map
tiles. No heavyweight framework.**

The consequence, accepted deliberately: **a sidekick that has never had internet has no rich application**
— only the hydrated seed (provision, status, pairing, bootstrap fetch page). It can join WiFi and accept
a bundle, and that is all. Constraint 2 is what makes this acceptable.

## 4. How the application and firmware are delivered

**The phone relays, and the browser's own TLS connection is what proves the source.**

1. Phone opens `http://minift8.local/` — after hydrate, the sidekick serves the bootstrap page **from the
   web FS** (seeded from firmware embed on first boot or recovery).
2. That page fetches the bundle from the bundle host over HTTPS (HTTP page, HTTPS fetch: allowed, and the
   host must send `Access-Control-Allow-Origin` — see the host requirements below).
3. The page **POSTs the bundle to the sidekick**, which checks §7's pairing token, stages the tree, checks
   the manifest's per-asset digests, then atomically promotes onto the web FS (and may replace former seed
   pages).
4. Firmware travels the same path to an OTA endpoint.

This is the conclusion of §2 and it deletes an entire class of work: **no TLS stack on the device, no CA
bundle to maintain as roots rotate, no certificate expiry, no clock dependency for certificate validity.**
Measured earlier: TLS costs ~116 KB of flash on this target. It also resolves two of the three decisions
[RFC 0001](0001-ble-companion.md) §5.2d left open — CA maintenance stops existing, and the choice between
embedding the full app or a bootstrap resolves to *bootstrap*.

**Signing is rejected, decided 2026-09-12.** This section previously required signed images verified against
a public key pinned in firmware, on the reasoning that TLS authenticated the *source* and a plain POST on a
LAN authenticates nothing. The first half is right and the conclusion does not follow. Recorded here with the
argument, not merely deleted, because "add signing" is the reflexive answer and this needs to stay rejected on
purpose:

* **The source is already authenticated, one hop earlier.** Step 2 is an HTTPS fetch the browser validates
  against the public CA system. By the time the bytes reach step 3 they have been proven to come from the
  bundle host untampered. A signature would re-prove what TLS just proved.
* **Every inbound path passes through the operator's own browser.** §2's conclusion is that the device never
  speaks TLS, so it never fetches anything itself — bundle and firmware both arrive by relay. There is no
  route where a signature is the *only* available proof of origin, which is the situation that would justify
  one.
* **The key would share a trust domain with the host, so it would prove nothing extra.** CI signing means the
  private key lives in a GitHub Actions secret. Secrets are not readable in the UI, but any workflow can use
  one and anyone who can push a workflow can exfiltrate it — so the key's security boundary is the GitHub
  account's boundary, which is the same boundary already protecting the host. Whoever can serve a malicious
  bundle can sign it. Making the signature independent would mean an offline key and a manual signing step on
  every app release, buying defence against a GitHub account compromise alone.
* **The cost is permanent and one-way.** A pinned key with no revocation path is a liability for the life of
  every device, which §10 already admitted. Paying it for a duplicate of TLS is a bad trade.

**What the device actually needs is authorization, not authenticity**, and it is a different problem than the
one signing was aimed at: nothing must be able to write the web FS or key the transmitter merely by
being on the LAN. §7's pairing token is that control, and it is cheap — no key custody, no revocation, no CI
signing step. It is therefore built **before** the POST endpoint exists rather than two slices later.

Two things are kept that are easy to mistake for signing. The manifest carries a **SHA-256 per asset**, which
catches a truncated or garbled upload over flaky WiFi — integrity against corruption, not against an
adversary, since whoever supplies the bundle supplies the manifest too. And **version pairing** below is
about skew, not trust. The sidekick's existing `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` already covers an
image that boots but cannot talk, and first install and recovery need physical USB-C through the ADV — so
physical possession remains the real trust anchor, as it already was.

**This is a pre-alpha decision with a stated trigger for revisiting.** It rests on there being one operator
relaying to their own device. Signing earns its cost when content can reach a device by a path that is *not*
the owner's TLS-authenticated browser — distributing built sidekicks to other operators, or an unattended
update path. Reopen it then, with this argument in hand.

### Host requirements, and why CORS is on the critical path

**The design depends on two properties of the bundle host, not on who the host is.** Any static host with
both works; GitHub Pages is the implementation, not the architecture.

1. **HTTPS with a publicly-trusted certificate.** This is the only thing authenticating the bundle, per the
   signing decision above, so it is load-bearing rather than hygiene.
2. **An `Access-Control-Allow-Origin` that lets a plain-HTTP origin read the response.**

**Why the second one matters at all is worth stating, because it looks like a security concern and is not.**
CORS here is a *capability* question. The bootstrap page is an origin (`http://minift8.local`) fetching from
a different origin (the bundle host), so the browser will issue the request but withhold the response body
from our own script unless the host opts in. Nothing is being defended; the browser is simply refusing to let
us read bytes we need to read. And we need to read them because the browser is the courier: mixed content
forbids an HTTPS page from reaching `http://minift8.local` (§3), so the app must be served locally, so it
must first be carried onto the device, and a courier has to be able to read the parcel. Were the sidekick
willing to speak TLS it would fetch the bundle itself and CORS would never arise — that is the ~116 KB and
the CA bundle §2 declined. Cross-origin `<img>` and `<script>` loads need no CORS but yield rendering or
execution, never bytes to re-POST.

Since it is a precondition nobody here controls, it was measured rather than assumed — the same discipline
§8 applied to QRZ, where the answer inverted the assumption. Measured 2026-09-12 with
`Origin: http://minift8.local`:

| Host | `Access-Control-Allow-Origin` | Readable by browser JS |
| --- | --- | --- |
| Pages, `github.io` subdomain | `*` | Yes |
| Pages, custom domain | `*` | Yes |
| Pages, binary asset | `*` | Yes |
| Pages, preflight `OPTIONS` | — (HTTP 405) | Simple requests only |
| GitHub Releases asset | absent | **No** |

Observed behaviour, not documented policy — the same caveat §8 carries for QRZ. Re-check any row with:

```bash
curl -sS -D - -o /dev/null -H 'Origin: http://minift8.local' <url> | grep -i access-control
```

`pages.github.com` is the custom-domain row and `microsoft.github.io` the subdomain row — neither is ours,
because Pages is not yet enabled on this repo; they measure the *platform*, which is the thing being decided.
Two constraints follow, and both are free to adopt now and irritating to retrofit:

* **The bundle fetch must stay a *simple* request**: `GET`, no custom request headers. Pages answers a
  preflight with 405, so anything that triggers one fails. API version and the like belong in the manifest
  body or the URL path, never a request header. This does not touch §7's pairing token, which guards the
  sidekick's own same-origin API.
* **Manifest URLs stay relative to the manifest's own location.** A project-repo Pages site serves under
  `/Mini-FT8/` while a custom domain serves at the root, so absolute paths would break on any move.
* **The bundle host base URL is stored in NVS, not compiled in**, with a build-time default — set alongside
  the WiFi credentials at provisioning. This is what makes host-independence real instead of aspirational: a
  provider swap becomes a config change rather than a reflash of every device. It is a **token-guarded
  write** like any other config (§7), because a settable bundle host is otherwise the most attractive thing
  on the device for a LAN attacker to repoint.

**Host-independence has one rider: the CORS header is per-provider and must be measured, not assumed.** A
provider that fails requirement 2 breaks the relay outright — the browser fetches and the script cannot read
— so the `curl` above is the gate on adopting any new host, including our own. Note the two hostnames that
are easy to conflate: `minift8.local` is the *sidekick's own* mDNS name and stays in firmware; the bundle
host is the separate, NVS-stored one.

**`kb2slo.kalikstein.com` is a later step and costs nothing to defer.** CNAMEd onto the same Pages deployment
it is a DNS record, a `CNAME` file and GitHub's own certificate. The measurement survives the move because
the custom-domain row above *is* a custom domain on Pages; only the NVS base URL changes.

**Releases cannot be reused, which is why this is new infrastructure rather than a reuse.** CI already
publishes firmware as a GitHub Release asset (`continuous`), and that path sends no
`Access-Control-Allow-Origin` at all — so it is unreachable from browser JS and cannot carry the bundle.
Recorded because it is the obvious shortcut and it does not work.

**Version pairing.** Keeping the app out of firmware brings back the skew problem that bundling would have
solved: a downloaded app can outrank the API of the firmware serving it. The manifest therefore pairs them —
each bundle declares the minimum sidekick API version it needs — and the sidekick **refuses a bundle it
cannot serve**, saying so plainly rather than accepting it and failing later. Cheap to design in, nasty to
discover in a field.

**Manifest JSON (concrete).** Constrained shape, no unknown fields, no string escapes. Digests are exactly
64 lowercase hex digits. Paths are relative under the web root (no leading `/`, no `..`, no dotfiles).
Device constant `WEB_API_VERSION` starts at `1`.

```json
{
  "api_min": 1,
  "assets": [
    {
      "path": "app/index.html",
      "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "size": 0
    }
  ]
}
```

**Install wire protocol (concrete).** One logical “POST the bundle” is three token-guarded steps so a
truncated upload cannot promote, and so begin can refuse an incompatible `api_min` before any bytes hit the
live tree:

1. `POST /api/bundle/begin` — body = manifest JSON above. Parses, checks `api_min <= WEB_API_VERSION`,
   wipes `/web/.staging`, remembers the manifest.
2. `PUT /api/bundle/file?path=<relpath>` — raw body; `Content-Length` must equal the manifest `size`;
   SHA-256 of the body must equal the manifest digest. Path must be listed in the open begin.
3. `POST /api/bundle/commit` — every asset must be staged; re-hash from disk; rename each onto `/web/`;
   wipe staging. Partial promote failure leaves staging for retry and may leave a mixed live tree —
   force re-hydrate recovers seed pages.

All three are `PAIRING_REQUIRED` (§7). Seed page `/update` drives this sequence after the HTTPS fetch
from the bundle host.

**Hosted site layout (same repo, `webapp/`).** GitHub Pages publishes the `webapp/` tree:

| URL | Audience |
| --- | --- |
| `https://kb2slo.github.io/Mini-FT8/` | Humans — landing / about |
| `https://kb2slo.github.io/Mini-FT8/app/` | Machines — `manifest.json` + assets |

NVS (and the compile-time default) stores the **app** base with no trailing slash:
`https://kb2slo.github.io/Mini-FT8/app`. Soft identity: a product rename changes that default string (and
optionally one NVS write), not the install protocol. Device day-to-day UI remains `http://minift8.local/`.
Seed page `/update` fetches `{base}/manifest.json` and drives the wire protocol above.

## 5. Provisioning and first run

```mermaid
flowchart LR
    subgraph s1["1 · Provision"]
        p1["Phone<br/>on sidekick AP"] --> cp["Captive portal<br/>credentials form"]
        cp --> ni["No internet here<br/>cannot fetch the app"]
    end
    subgraph s2["2 · Join"]
        rs["Sidekick restarts<br/>joins wifi"] --> md["Reachable by name<br/>minift8.local"]
    end
    subgraph s3["3 · Bootstrap"]
        p3["Phone<br/>wifi and internet"] --> fb["Fetch bundle<br/>from GitHub Pages"]
        fb --> po["POST to sidekick<br/>pairing token checked"]
    end
    s1 --> s2 --> s3
```

The phone cannot fetch a bundle while joined to the provisioning AP, because a captive portal generally takes
the default route and leaves the phone with no internet. As drawn, bootstrap therefore needs a network that
has internet.

**The phone's own hotspot resolves this, and it is a documentation answer before it is an engineering one.**
If the sidekick joins the operator's Personal Hotspot, the phone has cellular internet *and* is on the same
LAN as the sidekick — stages 1 and 3 collapse, a factory sidekick can be set up on a summit, and QRZ lookups
work in the field. The existing constraint applies: iPhone hotspots need *Maximize Compatibility* on, because
this radio is 2.4 GHz only.

[I19](../ROADMAP.md) — an AP whose DHCP omits the router and DNS options so the phone keeps its cellular
route — remains the answer for the AP-mode fallback where there is no cell signal at all. It is no longer on
the critical path, because in that situation the operator is offline regardless.

## 6. The sidekick-to-main protocol

**This is the critical path and the least designed part of the system.** Port A carries a version beacon
today. Everything the application needs crosses that link: decodes at slot cadence, TX control, configuration
read and write, and log access.

Requirements:

* **Framed messages over a byte pipe**, with framing that does not assume a stream. This is what keeps the
  transport swappable — see [B47](../ROADMAP.md), which defers the UART-versus-I2C choice deliberately. The
  application, the control API and this document must not depend on which wins.
* **Board-agnostic.** Specified against the application — decode, transmit, autoseq, config, log — never
  against board hardware, so an ADV build and a P4 build present the same surface.
* **Versioned**, for the same reason §4 pairs app and API.
* **Coexists with what is already on the bus.** `main/porta.cpp` currently arbitrates Port A between a GPS
  and the companion via `PortaRole`, a mechanism that exists only because UART makes the port exclusive.

Direction and latency are asymmetric in a way that shapes the design: commands are infrequent and want low
latency; decodes are periodic on a 15-second boundary and tolerate hundreds of milliseconds. A full slot of
decodes is roughly 3 KB, so sustained throughput is a couple of hundred bytes per second.

**Open, to be settled before implementation:** whether the browser-facing channel is polling, SSE, or a
WebSocket. Decodes are a one-way stream and commands are infrequent, so SSE plus POST is simpler and degrades
better on flaky WiFi than a WebSocket — but this deserves its own argument, not a default.

**CONFIG (I28d, 2026-09-14).** `CONFIG_GET` / `CONFIG_SET` / `CONFIG_VALUE` carry
`key_len` + key + value bytes. Empty key on GET means the whole Station.txt surface; SET/VALUE require a
key. ACK/NAK for SET and end-of-GET-all reuse `PORTA_MSG_ACK`/`NAK` with verb = the CONFIG message type.
HTTP: open `GET /api/config` (JSON object), token-guarded `PUT /api/config` (Station.txt lines). Entry app
asset is `app.html`. GET-all also includes live keys (`streaming`, `cat_ready`, `tune`, `band_name`,
`freq_khz`) that `station_key_known` refuses on SET. Band-ish CONFIG_SET pushes CAT via
`sync_radio_to_current_band` (headless has no STATUS exit).

**CONNECT / TUNE (I28d, 2026-09-14).** ACTION verbs `PORTA_ACT_CONNECT` (0x04, verb-only) and
`PORTA_ACT_TUNE` (0x05, `u8 on`) mirror STATUS keys 2 and 4. HTTP: token-guarded
`POST /api/radio/connect` and `POST /api/radio/tune` (body `0`/`1`).

**The remaining verbs are driven by the screens, not chosen here.** §11 fixes the screen model first and
derives what it needs of this link — a structured status event, decode identity for tap-to-queue, beacon
parity, and queue cancel by entry.

## 7. Who may key the transmitter

A headless radio driven by an unauthenticated local HTTP API means **any device on the network can start a
transmission**, and the licensee is answerable for it. **This section is now the whole of the security
design**, since §4 rejected signing on the grounds that authenticity already arrives with the browser's TLS
and what is actually missing is authorization on the LAN.

**Decided: a pairing token, minted during provisioning and held by the browser.** Every control request
carries it; requests without it are refused. This is the cheap version and it is proportionate — the threat is
an accident or a nuisance on a home LAN, not a targeted attacker, and the operator can re-provision to rotate
it.

**Reads are open on principle, decided 2026-09-12, not as a concession to convenience.** Anyone may listen to
amateur transmissions on licensed spectrum, and Part 97 forbids obscuring the meaning of a transmission in the
first place — so there is nothing in a decode to protect, and a design that hid one would be at odds with the
service it serves. That principle carries onto the LAN: decodes, status and the log are open to anyone who can
reach the device. What needs authorization is *changing* the device, because that is what the licensee answers
for.

This replaces an earlier and weaker justification — that reads stay open "so a second device can watch without
being able to transmit." Same behaviour, but the reason matters, because the weak version invites a future
change to trade it away for tidiness. It also retires the idea of a second read-scoped token: a watch-only
guest needs no credential at all.

**The line, and the one place the principle does not reach.** Guarded: transmit control, the bundle POST, the
firmware OTA POST, the bundle-host base URL and every other config write, and anything else that changes
device state — including the browser clock sync, which is a write even though an idempotent and self-correcting
one. Open: decodes, status, the log viewer, and the bootstrap pages that provisioning needs before a token
exists. **The carve-out is that a credential is not radio data** — a read that would hand back the WiFi
password or this token stays guarded.

That carve-out is why policy is a required argument at each route's registration rather than a rule derived
from the HTTP method. "GET is open" would be right for every route that exists today and silently wrong for
the first one that returns a secret, and a method rule offers nowhere to say so. Requiring the argument makes
an undeclared route a build error instead of an omission.

**Retrieval, decided 2026-09-12, and it is not the same problem as recovery.** The token lives in the
browser's `localStorage`, and the cases where the operator needs it again are ordinary rather than
catastrophic: a second device, a new phone, cleared browsing data, or Safari's ITP evicting script-writable
storage for a site untouched for seven days — which means a sidekick used monthly can lose it with no user
action at all. So the requirement is that the token be **readable on demand**, not merely resettable.

**The mechanism is the AtomS3 Lite's user button opening a short disclosure window** — press it and a
route serves the token. The window is **one-shot**: the first successful GET claims it and closes it, so a
completed re-auth does not leave the secret readable for the rest of a timer; if nobody claims it, it still
times out after a couple of minutes. GPIO41, plain input with the board's own pull-up, active low, read
straight out of the `M5Unified` this repo already vendors, so no pin is being guessed. Every page loads a
shared `/pairing.js` that polls for that claim and stores the token in `localStorage` — the operator presses
the button; they do not copy JSON or answer a `prompt()`. One gesture is enough for everything: with the
token in hand the operator can call the guarded `/forget` to change networks, which removes any need for a
button long-press, a boot-count trigger or an NVS reset path. The long-lived NVS token is **not** rotated on
retrieval: that would log out every already-paired browser. What stops after re-auth is further disclosure,
not the credential itself.

**Gating disclosure on the button rather than on AP mode is the point, not an implementation detail.**
Gating it on AP mode was the obvious design and §1 rules it out: if the application runs over AP mode, then
"disclosed only in AP mode" means disclosed during normal operation, which is no gate at all. A button press
is physical in both modes and stays correct whichever one turns out to be primary. It is also less
disruptive — the operator never has to leave their network to pair a second device.

**Its honest limit, recorded so it is not mistaken for a defect later.** The token travels in clear over
plain HTTP, so it can be captured and replayed by anyone who can read the traffic. On a WPA2/WPA3 network
per-station encryption makes that materially harder; on an open network it does not. That is consistent with
the threat this is scoped to and it is the reason the scope is written down: a nuisance control, not a
targeted-attacker control. Raising that bar would mean a certificate for a name that resolves on one LAN,
which §3 declined for its own reasons.

**Not decided:** what happens when two paired browsers both try to transmit. A single-writer model is the
likely answer, but multi-client behaviour is unspecified and should not be discovered in the field.

## 8. Third-party services

**Measured 2026-09-10, and the result was the opposite of the assumption.** Both are built for server-side
consumers and predate CORS by a wide margin, so both were expected to block browsers:

| Service | `Access-Control-Allow-Origin` | Reachable from browser JS |
| --- | --- | --- |
| QRZ XML (`xmldata.qrz.com/xml/current/`) | `*`, and a preflight `OPTIONS` returns `Access-Control-Allow-Headers: *` | **Yes, directly** |
| PSKReporter (`retrieve.pskreporter.info/query` and `cgi-bin/pskquery5.pl`) | absent from both | **No** |

**QRZ is called directly from the browser, and this is better than the proxy it replaces.** The concern was
that QRZ's XML interface requires a subscriber login, so a proxy holding *the author's* credentials would put
every user of this application on one subscription — a terms-of-service problem the moment a second person
runs it. Direct calls remove the problem rather than working around it: each operator's credentials travel
from their own browser to QRZ and never touch our infrastructure, which is also the right answer for privacy
and for liability.

**Caveat, and it belongs in the design rather than a footnote:** that header is *observed behaviour on
2026-09-10*, not documented policy. QRZ can drop it without notice, and if they do, this breaks silently in
the field with no signal on our side. The application must fail legibly when a QRZ lookup is refused by CORS
— treat it as a missing optional service, never as a fatal error — and the proxy below is the fallback if the
header ever goes away.

**PSKReporter needs a Cloudflare Worker**, free tier, 100k requests/day. Two things about it worth fixing in
the design now:

* It **carries no secrets**. PSKReporter needs no credentials, so the Worker is a pure CORS shim and there is
  nothing in it to leak or rotate. That is a materially smaller thing to operate than what §8 originally
  proposed.
* It must **filter, not relay**. One measured query — a single callsign over one hour — returned **1.25 MB of
  XML**. Passing that to a phone on cellular for every refresh is not acceptable, so the Worker parses and
  re-serves the subset the application needs as JSON. This is now the main reason it exists; CORS is the
  lesser half.

Google Maps is a browser API by design and needs no proxy. Its key ships in a static app and must therefore
be referrer-restricted.

## 9. What this becomes

Roadmap rows, sequenced. The protocol gates everything else.

| Row | Scope |
| --- | ----- |
| Protocol | Framed, bidirectional, transport-agnostic messages across Port A (§6). **First.** |
| Transport trial | [B47](../ROADMAP.md) — bench-prove ESP32-S3 as an I2C slave, and bus recovery after a live cable yank. Parallel; must not block. |
| Control API + pairing | The API surface and the token (§6, §7). **Before the partition row**, because the token is what keeps the bundle POST from being an open write into a partition the device then serves code from. §4's signing chain used to hold this slot and is now rejected outright. |
| Web FS + bootstrap | LittleFS at the 1.9 MB tail; hydrate from firmware embed; FS is the sole served origin; token-guarded phone-relayed POST with stage/verify/promote; force re-hydrate recovery; per-asset digests; NVS bundle-host URL; version pairing (§3, §4). |
| The application | The web app, plus a PSKReporter Worker that filters rather than relays. QRZ is called directly from the browser (§8). Screen model, and what it requires of the protocol: §11. |

## 10. Risks

| Risk | Bounding |
| --- | --- |
| The protocol is underestimated | It is treated as the critical path here, ahead of the visible work. If it slips, the application slips with it, and that is the correct order. |
| Non-secure context bites harder than expected | Geolocation and service workers are known losses (§3). A surprise beyond those would reopen the certificate question, which is why §3 records why it was declined rather than merely that it was. |
| App outgrows the partition | 1.9 MB against 100–300 KB gzipped is roughly 6x headroom, and the discipline is stated: no bundled tiles, no heavy framework. If it is ever breached, the fix is a partition change, which costs a USB-C reflash of every device in existence. |
| Bad bundle leaves present-but-broken pages | Staging + manifest verify before promote. Force re-hydrate from firmware embed restores the seed tree without a desk computer; USB-C remains the hard floor. |
| A LAN device writes the web FS or keys the transmitter | §7's token, built before the POST endpoint exists. The residual is a replay by someone who can read plain-HTTP traffic on the same network, which §7 records as a known limit rather than a defect — it is scoped to nuisance, not to a targeted attacker. |
| No independent proof of source | Accepted deliberately (§4). Authenticity rests entirely on the browser's TLS session to the bundle host, so a compromised operator browser, or a compromised host account, can deliver a hostile app. Signing was examined and rejected because a CI-held key shares the host's trust domain and re-proves what TLS proved. The trigger to reopen is content reaching a device by any path that is not the owner's own browser. |
| The bundle host drops its CORS header | Would break the relay outright and silently (§4). Observed behaviour, not policy, same as QRZ in §8. Bounded by host-independence: the base URL lives in NVS, so moving to a provider that cooperates is a config change. |
| QRZ stops sending its CORS header | Measured open on 2026-09-10, but that is observation, not policy (§8). The application treats a refused QRZ lookup as a missing optional service, and the PSKReporter Worker is the fallback path if it has to carry QRZ too. |
| Two browsers, one transmitter | Named and unresolved (§7). |

## 11. The application's screens

**Decided 2026-09-14, after surveying WSJT-X, FT8CN, iFTx and the standalone touchscreen builds
(DX-FT8 / Pocket FT8 / sBitx). The survey's result was not the one expected.** A phone FT8 UI has four
questions to settle — how the TX offset is chosen, how calling and answering are sequenced, how a QSO in
progress is represented, and how the screens divide. **Mini-FT8's existing paradigm already answers the
first three, and answers them better than the reference apps do**, leaving only the fourth genuinely
open. So this section is mostly a record of what *not* to import. It
sits next to §6 in spirit rather than in numbering — the screens decide the verbs, which is why the
design pass ran before I28d's autoseq slice rather than after it.

Appended rather than inserted because §3, §4, §6 and §7 are cited from code comments, `partitions.csv`,
host tests and the roadmap; renumbering would churn all of them for a section ordering.

### No waterfall, because `offset_src` already replaced its job

In all three reference applications the waterfall's primary *control* role is picking the TX audio
offset: shift-click in WSJT-X, tap the waterfall in FT8CN, tap to set TX in iFTx. Mini-FT8 does not ask
the operator that question at all — `resolve_tx_offset()` takes Random (fresh roll in 500–2500 Hz per
transmission), RX (answer on the caller's own offset, except for CQ) or Fixed.

**Random is better than tapping a gap, not a cheap substitute for it.** A tapped gap was clear when the
slot was decoded, which is up to fifteen seconds stale, and every operator looking at the same waterfall
taps the same visible gaps. Re-rolling per transmission decorrelates our offset from both, costs no
screen area, and needs no interaction on a phone — where fat-finger frequency picking is worst. It is
also why we do not inherit FT8CN's landscape requirement, whose own reviewer wanted an 8-inch tablet
largely to fit waterfall beside decodes.

Constraint and preference happen to agree here: §6 budgets a couple of hundred bytes per second, and
spectrum data for a waterfall is a different order of magnitude, so it could not be afforded even if it
were wanted.

What we give up is the visual "how busy is the band" read. If that is ever missed, the cheap answer is
occupancy derived from the offsets decode events already carry, shown numerically — not a spectrum.
**Reopen if** field operation shows repeated collisions that per-TX re-rolling does not avoid.

### Not a mode machine, because beacon plus the priority queue already sequences

iFTx exposes Listen / Reply / Call / Exchange as a segmented control, and WSJT-X splits Band Activity
from Rx Frequency into two panes. Both exist to answer "am I calling or answering", because the
application has to be told. Autoseq already knows: the queue sorts
`IDLE > SIGNOFF > ROGERS > ROGER_REPORT > REPORT > REPLYING > CALLING`, so every live QSO outranks a CQ,
and a CQ is short-lived — one transmission, then `tick()` pops it (`AUTOSEQ_ARCHITECTURE.md`).

So the operator turns the beacon on with a parity and the machine interleaves; replies take priority
without anyone choosing. **There is therefore no mode control on the operate screen, and no mode the
operator can be in the wrong one of.** EVEN/ODD is the one genuinely Mini-FT8-specific control, because
it decides which half of the cycle we occupy.

### The queue is a set of concurrent QSOs, not a conversation

iFTx's Exchange mode shows *the* contact — one status line, singular. Autoseq holds up to `AUTOSEQ_MAX_QUEUE`
contexts — **30**, active plus inactive (`main/autoseq.h`; corrected 2026-09-14, this section previously
said 120, checked against the constant rather than assumed while building the protocol slice against it) —
sorted by state, with an inactive zone that preserves metadata across retry exhaustion so a patient DX
can reactivate a dormant QSO minutes later (`AUTOSEQ_INACTIVE_QUEUE.md`). A single "current QSO" panel
would actively misrepresent that state. The queue is a first-class region of the screen, not a detail
of the decode list.

### Screens

| Screen | Contents |
| --- | --- |
| **Operate** (default, `app.html`) | Three stacked regions: state header, queue, decode stream. |
| **Log** | QSO browse, backed by the ADV's files (needs `FILE_*`, §6). |
| **Settings** | Station identity, `offset_src`, bands, radio profile, protocol — today's form. |
| *(later)* PSKReporter | The only genuine new destination among the enrichments (§8). |

**The phone's contribution is collapsing, not extending.** The ADV splits stream, queue and live state
across RX, TX and STATUS because 240×135 forces it. A phone shows all three at once, which is the actual
opportunity — and it is a smaller change to §6 than a mode machine would have been. Growth is by
destination, never by feature: **QRZ is not a screen**, it is per-callsign data that expands on a decode
or log row.

The state header carries what STATUS shows plus what only the phone can show comfortably: slot countdown
and parity, beacon state, band and frequency, CAT and audio liveness, and the resolved TX offset.

### Log: one view, many day files

The ADV keeps one ADIF file per UTC day — a good on-device shape (bounded file size, a natural rotation
boundary) but not a concept the UI should expose. §11 already ruled out a view-per-feature; a day-file
picker would be exactly that, a second navigational axis layered onto a screen this RFC just collapsed to
one. So: **one continuous scrolling QSO list, file boundaries invisible**, with day-section dividers
("Today", "Sep 13", ...) rather than a picker, and **lazy-load backward by day** — the current day's file
fetches on open, an older day fetches only when the operator scrolls into it.

This is not just tidiness. §6 budgets a couple of hundred bytes per second; pulling every day file on
every `/log` open does not fit that budget, and the file-per-day boundary already on disk is exactly the
natural pagination unit, so a `FILE_LIST` plus `FILE_GET(day)` pair replaces "dump the whole log" for
free — see §6 below.

**Open:** search. Filtering what is already loaded is the cheap default for first ship. Full-history
search needs either on-device grep across day files or an app-side index built as days stream in, and
neither is designed yet — a protocol decision to make deliberately, not one to back into.

### Log sync, and where "already synced" lives

§2's inversion holds here unchanged: the sidekick never talks to QRZ; the browser does, with the
operator's own credentials, exactly as §8 already has it for direct QRZ lookups. Sync is therefore a
**Log-screen action**, not a device feature — read QSOs off the device log (the day-file API above), let
the browser push new ones to QRZ, and, later, to whichever of eQSL / LoTW / Cloudlog / Wavelog the
operator uses (named because a comparable project already ships all four — see the QMX panadapter watch
in [ROADMAP.md](../ROADMAP.md)).

That raises a question §8 never had to answer: **where does "already synced" live?** The device's log is
an offline ADIF file with no concept of upload status, and nothing on it should learn about QRZ. The only
place that can hold sync state is the browser — **`localStorage`, the same honest-limit shape as §7's
pairing token**: lost on cleared storage or a new device, with no server-side record to reconcile against.
Unlike the pairing token, losing it has a real consequence — it risks re-uploading QSOs a destination
already has. **Not decided:** whether that risk is acceptable, resting on the destination's own dedup
(most logbook services dedupe on call/band/mode/date-time), or whether the app needs its own
reconciliation pass. That is a policy decision belonging in its own write-up before the sync action is
built, recorded here so it is not invented silently in code later.

What *is* settled is the shape: a small per-destination status chip on each QSO row (filled = synced,
outline = not) and a "Sync N new" action scoped to unsynced rows, sized to hold more than one destination
per row without a new screen — the Screens table's rule that growth here is "by destination, never by
feature" extends past QRZ/PSKReporter to logging destinations too.

### Rich info: offline-derived vs. online-enriched

The Screens table already states the shape for QRZ — "not a screen, it is per-callsign data that expands
on a decode or log row." Grid squares deserve the same split, made explicit: **offline-derived** facts
(DXCC/country and CQ/ITU zone from the callsign prefix, distance/bearing from grid arithmetic) are pure
computation with no protocol dependency, so they render on tap with no wait. **Online-enriched** facts
(QRZ bio, photo, place name) are optional, fetched only when reachable, and fail legible exactly as §8
already requires of QRZ. One shared expand-on-tap component carries both, reused wherever a callsign or
grid appears — a decode row, a queue entry, a QSO row — rather than a bespoke treatment per screen. This
is a UI-pattern decision, not a new §8 destination, so it does not reopen §9's sequencing: the online half
still waits behind whatever enrichment work reaches it.

### Grid square from the phone, and why it needs a second origin

The operator's grid square is a manually-edited field today, same as every reference app. Auto-filling it
from the phone's own location looks like a browser-API question; it is actually the same one §3 already
paid for and declined to solve. `navigator.geolocation` is gated on a **secure context**, and
`http://minift8.local` is deliberately not one — a fact that holds regardless of whether the phone has
internet at that moment, because the browser refuses the API on the origin's scheme alone. §3 already
named this loss ("losing geolocation is the one that stings... the price of not owning a certificate")
without a way around it. There is one, but it is not the obvious one.

**An iframe does not work, and the reason is worth recording because it is not obvious.** The Secure
Contexts spec walks the whole ancestor chain, not just the document calling the API: an HTTPS iframe
nested inside `http://minift8.local` is still not a secure context, because its parent is not. Google's or
Apple's own "locate me" code, run inside such an iframe, is denied exactly like our own code would be.
Only a real top-level navigation reaches a genuinely secure context — embedding cannot fake one.

**The fix reuses infrastructure §4 already paid for, instead of adding new infrastructure.** The bundle
host (`kb2slo.github.io` today) already carries a publicly-trusted certificate, for the unrelated reason
that §4's host requirement #1 needs one to serve the app bundle over HTTPS. A page hosted there is already
inside a valid secure context, so it can call `navigator.geolocation` without the sidekick ever owning a
certificate of its own. **Settings' "Set from map" therefore does a full top-level navigation out to that
page and a redirect back carrying the result** — not a fetch, not an iframe. §2's "the sidekick never
speaks TLS" is untouched: the device is not a party to this exchange at all. The phone leaves it, gets a
fix, and comes back.

```mermaid
flowchart LR
    s1["Settings<br/>Set from map"] -- "top-level navigation<br/>(not fetch, not iframe)" --> s2["Handoff page<br/>bundle host, HTTPS"]
    s2 -- "navigator.geolocation<br/>real secure context" --> s3["Auto-locate,<br/>drag to correct"]
    s3 -- "redirect back<br/>with the resulting grid" --> s4["Settings<br/>grid field filled"]
```

That page auto-locates on open and drops a pin; the operator can drag to correct it before confirming —
auto by default, manual as the fallback in the same screen, which matters near a grid-square boundary or
with GPS jitter and indoor multipath. Geolocation permission is granted per origin, so once the operator
allows it on the handoff page's origin the first time, later uses do not re-prompt.

**Open:** the tile provider. Google Maps' JS API needs an API key, and referrer-restricting a key to a
page whose referrer is always the same LAN-local hostname is awkward. Leaning OpenStreetMap/Leaflet — no
key, no vendor dependency — in the same spirit as §8's discipline of not adding infrastructure before it
is measured necessary, but not yet decided.

**Scope flag, separate from the design above.** [ROADMAP.md](../ROADMAP.md)'s I28d row locked
*"GPS-as-primary time/grid... out of this ship (phone clock + edited grid cover QSO)"* on 2026-09-14. That
line was written about the ADV's own GPS module; whether the operator's reasoning there — manual grid is
good enough for first ship — extends to this phone-GPS path too is a roadmap-scope call, not a design one.
This section describes how the feature would work if and when it is built, not that it is in I28d.

### What this requires of §6, which is why the design pass came first

* **A structured status event.** Per-entry autoseq state, retry counter and dxcall, plus slot parity,
  beacon state and resolved offset. Log lines cannot carry this; the queue region is unbuildable without
  it, and nothing else in I28d needs a new event type this badly.
* **Decode identity.** Tap-to-queue must name a decode, so a reply references an event rather than
  re-parsing rendered text on the phone.
* **Beacon as an ACTION carrying parity**, matching STATUS key `1`'s three-way cycle.
* **Queue cancel by entry**, which is what makes the queue region a control rather than a readout.
* **`FILE_LIST` + `FILE_GET(day)` for Log.** Paginated by the day-file boundary already on disk, not a
  bulk dump — see the Log subsection above.

### Borrowed deliberately

* **FT8CN's slot-timing affordance.** It transmits in the current cycle if you swipe within 2.5 s of
  cycle start and the next cycle otherwise. The lesson is not the threshold but that the operator must be
  able to see *which slot a tap lands in*, so the countdown belongs next to the tap target.
* **iFTx's worked-before colouring** on CQ rows — green for new, red for worked. Cheap for us because the
  ADV owns the log, which is also why it must be computed there and not in the browser.
* **WSJT-X's two information needs** — "everything on the band" versus "what concerns me" — kept as
  emphasis within one stream rather than as two panes or two modes.

### Rejected

| Imported idea | Why not |
| --- | --- |
| Waterfall | `offset_src` already does its control job better on a phone, and §6 cannot afford spectrum. |
| Mode control (Listen / Call / Exchange) | Autoseq's priority queue sequences without being told; a mode would add an error state that does not currently exist. |
| A view per feature | FT8CN's own review says there is too much going on for a phone, and mid-QSO tab-switching runs against a 15-second clock. |
| Single "current QSO" panel | Misrepresents a multi-entry concurrent queue (`AUTOSEQ_MAX_QUEUE` — 30, §11's "queue is a set" section). |
| Modifier-key interactions | The Hinson operating guide's standing complaint about WSJT-X; there are no modifiers on a phone anyway. |

### Open

* Whether the decode stream needs explicit filtering (CQ-only, addressed-to-me) or whether emphasis is
  enough. Related to [I1](../ROADMAP.md), which is sort/filter on the ADV side.
* ~~Whether the queue region is scrollable on the operate screen or truncates to the active zone with the
  inactive zone behind disclosure.~~ **Resolved 2026-09-14, once the real ceiling was checked rather than
  assumed:** `AUTOSEQ_MAX_QUEUE` is 30, not the 120 this section previously said. Thirty rows is nothing to
  scroll on a phone, so the two-tier active/inactive disclosure the wireframe pass had sketched for "a lot
  of phone" was solving a problem that does not exist at this size — plain scroll, no split.
* B37's beacon time limit is the one beacon change the phone makes natural — "beaconing 1h23m, stop at
  2h" is a phone control and an awkward Cardputer one. Not in this ship; recorded so the screen leaves
  room for it.
* ~~Nav pattern~~ **Decided 2026-09-14:** a bottom tab bar (Operate/Log/Settings), replacing today's
  `app.html` top text-link nav. Chosen over top nav because the queue and decode-stream regions need the
  vertical space more than a header does.
* Sync-status persistence and dedup policy for QRZ (and later destinations) — see the Log sync subsection
  above. `localStorage` only, same honest-limit shape as §7's token; whether that is sufficient or needs a
  reconciliation pass against the destination is undecided.
* Cross-day log search — first ship scoped to loaded days only (see the Log subsection above); whether
  full-history search needs on-device grep or an app-side index is a protocol decision, not yet made.
* Map tile provider for the grid-from-map handoff — leaning OpenStreetMap/Leaflet, not decided.
