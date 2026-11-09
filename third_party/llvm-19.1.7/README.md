# LLVM 19.1.7 redistribution notices

The Windows setup profile bundles unchanged `opt.exe`, `llc.exe`, `lld-link.exe`
from the [official signed release archive](https://github.com/llvm/llvm-project/releases/tag/llvmorg-19.1.7).
Archive and selected binary hashes are pinned in
[llvm-tools.json](../../tools/installer/llvm-tools.json). No LLVM source is compiled
or implementation code vendored here; these files are license/attribution text.

- `LICENSE.txt`: exact LLVM 19.1.7 project license, including Apache 2.0 with LLVM
  exceptions, third-party scope and the legacy University of Illinois grant.
- `COPYRIGHT.regex`: exact upstream Support regular-expression notice.
- `BLAKE3-LICENSE.txt`: exact upstream LLVM Support/BLAKE3 license.
- `xxhash-NOTICE.txt`, `MD5-NOTICE.txt`, `SHA1-NOTICE.txt`: leading license and
  attribution comments from the tagged `llvm/lib/Support` source files.
- `rpmalloc-NOTICE.txt`: the tagged LLVM allocator's Apache/LLVM notice.
- `libxml2-Copyright.txt`: libxml2 2.9.12's upstream MIT notice. LLVM's tagged
  [Windows release recipe](https://github.com/llvm/llvm-project/blob/llvmorg-19.1.7/llvm/utils/release/build_llvm_release.bat)
  pins that source release and statically links `libxml2s.lib`; the extracted
  LLVM configuration independently confirms libxml2 support.

Sources are pinned to `llvmorg-19.1.7` at
`https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-19.1.7/`.
These notices accompany the selected binary tools in Wood's setup payload. Upstream
LLVM licensing does not replace Wood's MIT OR Apache-2.0 license. Tool dependency
inspection and the qualified release profile are recorded in the setup report;
the source archive contains notices, not the downloaded toolchain.
