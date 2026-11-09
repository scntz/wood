# Publishing a release

This page preserves the release procedure for reproducing the research artifacts or publishing from a fork.
Wood has no regular upstream release schedule. Tags, releases, repository visibility and Marketplace
publication require the repository owner's approval.

A published release is never replaced. Never move or recreate a release tag, or upload over a release's files. If a release
is wrong, say so in its notes and publish a new version.

## Before publishing

Build and test the release as [releasing](releasing.md) describes. Then, from a clean checkout:

```sh
git status --short
pwsh -NoProfile -File tools/release-audit.ps1 -CheckDocs
git fsck --full
```

In the assembled directory, check that `sha256sum -c SHA256SUMS` passes and that `qualification.json` names the commit you
expect. Upload only the listed files, never a build directory.

## Steps

`VERSION` is the version, such as `1.0.1`. `COMMIT` is the value in `SOURCE-COMMIT.txt`, and `ASSETS` is the assembled directory.

1. **Make sure `main` contains the release commit**, merging it if needed. Never force-push.

2. **Tag that commit itself**, not a later one:

   ```sh
   git merge-base --is-ancestor COMMIT origin/main
   git tag -a vVERSION -m "wood VERSION" COMMIT
   git push origin vVERSION
   ```

3. **Create the release:**

   ```sh
   gh release create vVERSION --repo scntz/wood --verify-tag --latest \
     --title "wood VERSION" --notes-file "$ASSETS/RELEASE_BODY.md" \
     "$ASSETS/wood-setup.exe" "$ASSETS/wood-core-windows-x86_64.tar.gz" \
     "$ASSETS/wood-setup-linux-x86_64" "$ASSETS/wood-core-linux-x86_64.tar.gz" \
     "$ASSETS"/wood-source-*.tar.gz "$ASSETS"/wood-*.vsix \
     "$ASSETS/SHA256SUMS" "$ASSETS/qualification.json" "$ASSETS/SOURCE-COMMIT.txt" "$ASSETS/RELEASE_BODY.md"
   ```

   Then download every file again and check it with `sha256sum -c SHA256SUMS`.

4. **Check it as a stranger would.** From a machine or shell with no GitHub sign-in:

   ```sh
   curl -fLO https://github.com/scntz/wood/releases/download/vVERSION/SHA256SUMS
   curl -fLO https://github.com/scntz/wood/releases/download/vVERSION/wood-core-linux-x86_64.tar.gz
   sha256sum -c SHA256SUMS --ignore-missing
   ```

   Then install it and check that official packages can be fetched:

   ```sh
   wood init first && cd first
   wood add wood@image
   wood run
   ```

## Making the repository public

These are the commands used for the original public releases. A fork changes the names, and turns on private
vulnerability reporting only if someone will read the reports:

```sh
gh repo edit scntz/wood --visibility public --accept-visibility-change-consequences
gh api --method PUT repos/scntz/wood/private-vulnerability-reporting
gh api --method PATCH repos/scntz/wood --input - <<< '{"security_and_analysis":{"secret_scanning":{"status":"enabled"},"secret_scanning_push_protection":{"status":"enabled"}}}'
gh repo edit scntz/wood --description "A native programming language. Built for humans, designed for AI." \
  --add-topic programming-language --add-topic compiler --add-topic self-hosting \
  --add-topic native --add-topic llvm --add-topic static-typing
```

The catalog lives in `catalog/`. Keep its archive hashes synchronized with the published
package artifacts. The downloadable 1.1.0 compiler uses the supplied local index file.
