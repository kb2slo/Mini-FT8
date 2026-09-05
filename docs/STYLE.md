# Coding style

Small written preferences for *our* C/C++. Do not apply to vendored `M5*` / `ft8_lib`. Campaign plan: [RFC 0002](rfcs/0002-extract-and-boundaries.md) (Done). Radio product picker is the table in `radio_profile.h` (B14).

Baseline is ESP-IDF’s guide where it matches this tree. We do not adopt Google function naming or a full LLVM restyle.

## Prefer `switch` over chained `if`

When a value is matched against a closed set of constants (`char`, enum, integer
mode, tag name already reduced to an enum, …), use `switch`. Do not write a
chain of `if` / `else if` (or `||` equality tests) for that set.

Use `if` when the condition is not a constant match: ranges, compound
predicates, or a single test.

Do not use locale `isspace` unless those extra characters are intended.

Exhaustive enum `switch`: no `default`, so `-Wswitch` catches new enumerators.
Non-enum `switch`: `default` required. Annotate intentional fall-through.

```cpp
bool is_space(char c) {
    switch (c) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            return true;
        default:
            return false;
    }
}
```

## Naming

Document the mix. Do not rename `main.cpp` to match a purer scheme.

- `g_` — process-lifetime app state in `main`
- `s_` — file-static
- `snake_case` functions
- `CamelCase` types and enums
- Component prefix on exported symbols (`adif_`, `storage_`, `autoseq_`, `radio_control_`)

Prefer a full word over an unexplained abbreviation in new and extracted names (`capabilities` not `caps`). Documented prefixes (`g_`, `s_`) stay. Do not restyle `main.cpp` or vendored `M5*` / `ft8_lib` to match.

New extracts follow this on day one. Old names in `main.cpp` change only when that function moves out.

## Headers and includes

Keep `.h`. `#pragma once`. `extern "C"` only on headers that C must include.

New files, include order: C/POSIX `<>`, then IDF, then other components, then own public, then own private. `""` for project and IDF headers.

## Braces and language

New files: function opening brace on its own line; `if` / `switch` / `for` brace on the same line (ESP-IDF). Until a file is extracted, match that file.

C++17. No exceptions, no RTTI. `std::string` / `std::vector` are fine in host-linkable units. Four spaces, LF, ~120 columns. No drive-by wrap of old lines.

No formatter on `main.cpp`, `M5*`, or `ft8_lib`. Optional `.clang-format` later, only on new extract directories.

## Structure

File size is a ratchet, not a rewrite target. New files stay under about 500 lines of real code. `main.cpp` may only shrink. Do not split a file just to game the number.

Require a **module**, not a class. New behavior gets a name and a header API. One tested function is enough. A strategy interface for one implementation is not.

Inheritance only when there is a closed set of variants that already exists or is about to (radio backends, later modes). Radio uses `radio_control_ops_t`, not `class Radio`. A station/mode object model is I6 and needs its own RFC.

No new logic in `main.cpp`. Call sites, wiring, and UI only. If it can be host-tested, it is not written in `main.cpp`.

**Where an extracted module lives** (2026-09-04). Default is a plain `.cpp` / `.h` pair in `main/`, next to `gps.cpp`, `porta.cpp`, `decode_sort.h`, `radio_profile.h`. A module earns its own `components/<name>/` directory only one of two ways:

**1. The build forces it.** Another component names it in `REQUIRES` / `PRIV_REQUIRES`, or more than one `idf.py` project compiles it (the ADV tree, `sidekick/`, `test_apps/`).

**2. It is a boundary we are deliberately holding.** This is a closed list, and adding to it means saying why here:

| Component | Why it stays |
|---|---|
| `M5Cardputer`, `M5GFX`, `M5Unified`, `ft8_lib` | Vendored third-party. RFC 0002 §6 vendor boundary — do not format, do not restructure. |
| `board_cardputer_adv` | Also forced (prong 1): `test_apps/cardputer_adv_audio_keyboard` requires it. Board/HAL seam [RFC 0001](rfcs/0001-ble-companion.md) I24 needs for a headless host. |
| `ui` | The display seam I24 needs to run headless. Demoting it would remove the abstraction that work depends on. 4200+ lines, 8 files. |
| `nano_flasher` | `EMBED_FILES` packaging of the `sidekick` image, a generated header, and conditional compile defines. That machinery is about shipping a payload, not about source layout. |

Anything not covered by prong 1 or listed above is a plain file in `main/`.

Host-testability does **not** force a component. `host_mock/Makefile` compiles sources by path and already carries `-I../main`, so a plain file in `main/` is host-tested exactly like a component is — `decode_sort.h`, `radio_profile.h`, and (since this rule landed) `station`, `band_config`, `usb_c_presence`, `adif`, `qso_browse`, `cts_time`, `file_list` all are.

Being a component is a cost: a directory, a `CMakeLists.txt`, an `include/` level, a possible `idf_component.yml`, and an entry in someone's `REQUIRES`. Pay it for one of the two reasons above, not by default.

Dead code is deleted in the unit you are extracting. No repo-wide unused-function hunt mixed with a feature.

Fixes that belong inside an extract go with that extract (B5 overlapping `sscanf` with Station parse). Unrelated fixes stay out.

Restyle and rename only the unit you are already changing.
