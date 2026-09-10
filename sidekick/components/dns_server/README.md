# dns_server (vendored)

Verbatim copy of ESP-IDF 5.5.1's
`examples/protocols/http_server/captive_portal/components/dns_server`.

SPDX headers are intact: Unlicense OR CC0-1.0, Espressif Systems. Public-domain
equivalent, so vendoring is unencumbered.

Why vendored rather than referenced: it lives under `examples/` in the IDF tree,
which is not on any component search path and is not a registry package. Copying
is the supported way to use it.

It answers every A query with one address, which is what makes a captive portal
work — the phone's connectivity probe resolves to the sidekick, gets our page
instead of the expected 204/Success, and the OS opens the portal.

Do not restyle or refactor this file. Treat it the way `docs/STYLE.md` treats
`M5*` and `ft8_lib`: vendored, and diffable against upstream.
