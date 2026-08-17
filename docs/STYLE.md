# Coding style

Small written preferences for *our* code. Do not apply to vendored `M5*` / `ft8_lib`. Broader hygiene is I11.

## Prefer `switch` over chained `if`

When a value is matched against a closed set of constants (`char`, enum, integer
mode, tag name already reduced to an enum, …), use `switch`. Do not write a
chain of `if` / `else if` (or `||` equality tests) for that set.

Use `if` when the condition is not a constant match: ranges, compound
predicates, or a single test.

Do not use locale `isspace` unless those extra characters are intended.

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
