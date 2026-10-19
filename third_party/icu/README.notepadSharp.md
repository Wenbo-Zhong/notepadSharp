# ICU4C 78.1 / Unicode 17.0

Upstream: https://github.com/unicode-org/icu/releases/tag/release-78.1
Source archive: icu4c-78.1-sources.zip
SHA-256: fe027bcd7e52ab3f3bbbb2d5e9ff9c674e94bbe671bb6a8f58719f27485864da
License: Unicode License V3 and bundled third-party notices (LICENSE).

Unmodified common/ and i18n/ sources and headers, stubdata.cpp/.h, and the
upstream little-endian icudt78l.dat. cmake/Icu.cmake statically builds common
and the regex/cleanup subset of i18n; collation, formatting and transliteration
are disabled. Sources for other i18n modules are retained but not compiled.
Windows embeds the complete upstream data archive as resource 901. Other
platforms distribute it under unicode/ beside the executable. No ICU DLL,
system ICU installation or network download is required at build/run time.

The application explicitly registers aligned common data and uses ICU C APIs.
Regex execution has a 2000 ms time limit and 8 MiB stack limit per matcher.
The optional ICU search mode is separate from Boost compatibility mode.
Unicode folding follows ICU regex rules: full folding for literal sequences,
simple code-point closure for character sets; no implicit normalization.
