# Reusable measurement library

This is Project A of the library-authoring walkthrough (`docs/libraries.md` in
the wood manuals). Author: wood contributors. License: MIT OR Apache-2.0;
the included [MIT](LICENSE-MIT) and [Apache-2.0](LICENSE-APACHE) texts apply.

`northstar_measurements` is a sample publisher-prefixed package name, not a
reserved namespace or registry entry. Its public module exports `Reading` and
`mean_kilograms`; `_total` is private. The dependency `units` is private to this
package's manifest and vendors the independently versioned `northstar_units`.

```sh
wood check .
wood lock .
wood test . --locked --report=json
```

Run these in this library's directory. Work on a copy for the tutorial. Locking
creates a lock alongside its manifest. The compiler repository's
`tests/community_packages.cmake` uses fresh directories, immutable Git commits
and a relocated, self-contained Project B.
