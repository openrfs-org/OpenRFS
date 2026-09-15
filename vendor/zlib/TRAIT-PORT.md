# Trait OS zlib port boundary

This directory vendors an unmodified subset of zlib 1.3.2. It is prepared for
the upstream-supported `Z_SOLO` profile: in-memory deflate/inflate streams and
checksums without host files, stdio, libc allocation defaults, or gzip file
handles. Vendoring alone does not integrate a Trait OS shared library or promise
compression support to applications.

The initial x86_64 Trait OS build should compile exactly these sources:

```
src/adler32.c
src/crc32.c
src/deflate.c
src/infback.c
src/inffast.c
src/inflate.c
src/inftrees.c
src/trees.c
src/zutil.c
```

Use both `include/` and `src/` as include directories. Define `Z_SOLO` for the
library and every consumer of `zlib.h`. On Trait OS's x86_64 LP64 target, also
define `Z_U4` as `unsigned` and `Z_U8` as `unsigned long long`; these declarations
select zlib's 32-bit CRC type and 64-bit braided implementation without asking
the freestanding headers to infer widths from `<limits.h>`. A compiler command
must pass the two-word `Z_U8` replacement as one argument, for example:

```
-DZ_SOLO -DZ_U4=unsigned '-DZ_U8=unsigned long long'
-DHAVE_UNISTD_H=0 -DHAVE_STDARG_H=0
```

The two explicit `HAVE_*` values keep Trait OS's `-Wundef -Werror` policy while
selecting the same no-host-header branches as zlib's unconfigured archive.

Keep `DYNAMIC_CRC_TABLE`, `BUILDFIXED`, `MAKECRCH`, `MAKEFIXED`, `ZLIB_DEBUG`,
and `ZLIB_INSECURE` undefined. The retained `crc32.h` and `inffixed.h` provide
the upstream-generated static tables, avoiding first-use generation and its
atomic/stdio support paths.

`Z_SOLO` deliberately removes zlib's default `malloc`/`free` adapters. Every
`z_stream` must set non-null `zalloc`, `zfree`, and a suitable `opaque` before
`deflateInit*`, `inflateInit*`, or `inflateBackInit`; otherwise initialization
returns `Z_STREAM_ERROR`. Trait OS's adapter must check multiplication and size
conversion before allocation, preserve allocator ownership in `opaque`, and
pair every successful initialization with the matching `*End` call on every
exit path.

The omitted `compress*`, `uncompress*`, and `gz*` functions are not part of
this profile and must not be advertised or referenced. If a future ABI enables
them, vendor the corresponding exact upstream sources and document the added
libc/VFS and allocation policy first. Do not patch files below `include/` or
`src/`; carry Trait OS adapters outside the upstream tree and refresh provenance
and hashes when updating the release.
