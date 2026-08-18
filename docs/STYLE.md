# Coding style

Small written preferences for *our* C/C++. Do not apply to vendored `M5*` / `ft8_lib`. Campaign plan: [RFC 0002](rfcs/0002-extract-and-boundaries.md). Broader hygiene is I11.

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

Dead code is deleted in the unit you are extracting. No repo-wide unused-function hunt mixed with a feature.

Fixes that belong inside an extract go with that extract (B5 overlapping `sscanf` with Station parse). Unrelated fixes stay out.

Restyle and rename only the unit you are already changing.
