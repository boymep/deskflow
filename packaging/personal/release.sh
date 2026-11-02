#!/bin/bash
# Publish a new Deskflow build of the personal fork to GitHub Releases (boymep/deskflow, private).
#
# usage: release.sh [--dry-run]
#   --dry-run   do every step (checks, build, package, checksum) but do not run `gh release create`
#
# Version and tag rule:
#   The app version is MAJOR.MINOR.PATCH.TWEAK, made by CMake from `git describe --match v* --exclude v*.*.*.*`:
#   MAJOR.MINOR.PATCH come from the nearest upstream tag (e.g. v1.27.0) and TWEAK is the number of commits
#   since that tag. HEAD 27 commits after v1.27.0 builds as 1.27.0.27.
#   The release tag is "v" + that exact version, e.g. v1.27.0.27. Four part tags are excluded from
#   `git describe`, so they never change the version of later builds.
#   The app compares all four parts as numbers, so every new commit on the (linear) deskflow branch, and
#   every newer upstream tag merged into it, gives a higher version. Do not rewrite published history.
#
# Release assets (names are fixed, the app looks for exactly these):
#   Deskflow-arm64.zip          the app, from deskflow-package-mac.sh
#   Deskflow-arm64.zip.sha256   `shasum -a 256` output with the bare file name
#   Deskflow-x64.msi            Windows installer, added by the "Windows release" GitHub Actions workflow
#   Deskflow-x64.msi.sha256     (.github/workflows/windows-release.yml) when the release is published
set -euo pipefail

REPO=/Users/boymep/Projects/deskflow
GH_REPO=boymep/deskflow
BRANCH=deskflow
ZIP=Deskflow-arm64.zip
SHA=$ZIP.sha256
MAX_ZIP_BYTES=$((300 * 1024 * 1024))

DRY_RUN=0
case "${1:-}" in
  "") ;;
  --dry-run) DRY_RUN=1 ;;
  *) echo "usage: $0 [--dry-run]" >&2; exit 2 ;;
esac

die() { echo "error: $*" >&2; exit 1; }

cd "$REPO"

# 1. Clean tree on the release branch, pushed to origin
[ "$(git rev-parse --abbrev-ref HEAD)" = "$BRANCH" ] || die "not on branch $BRANCH"
[ -z "$(git status --porcelain)" ] || die "working tree is not clean"
git fetch --quiet origin "$BRANCH"
[ "$(git rev-parse HEAD)" = "$(git rev-parse "origin/$BRANCH")" ] || die "HEAD is not pushed to origin/$BRANCH"
COMMIT=$(git rev-parse HEAD)

# 2. Configure, build, test and package (the package script re-configures so the version matches HEAD)
"$REPO/packaging/personal/deskflow-package-mac.sh"
[ -z "$(git status --porcelain)" ] || die "the build changed tracked files"

# 3. Version and tag from the build itself
VERSION=$(sed -n 's/^const auto kVersion = "\(.*\)";$/\1/p' build/src/lib/common/VersionInfo.h)
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "unexpected version '$VERSION'"
TAG="v$VERSION"
CORE_VERSION=$(env -u DYLD_PRINT_LIBRARIES dist/Deskflow.app/Contents/MacOS/deskflow-core --version 2>&1 | head -1)
[[ "$CORE_VERSION" == *"$VERSION"* ]] || die "packaged app reports '$CORE_VERSION', expected $VERSION"
TAG_CHECK=0
git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null || TAG_CHECK=$?
[ "$TAG_CHECK" != 0 ] || die "tag $TAG already exists on origin"
[ "$TAG_CHECK" = 2 ] || die "could not list the tags on origin"

# 4. Checksum file with the bare file name, verified right away
[ -f "dist/$ZIP" ] || die "dist/$ZIP is missing"
SIZE=$(stat -f %z "dist/$ZIP")
[ "$SIZE" -le "$MAX_ZIP_BYTES" ] || die "dist/$ZIP is larger than the 300 MB the app accepts"
(cd dist && shasum -a 256 "$ZIP" > "$SHA" && shasum -a 256 -c "$SHA" >/dev/null) || die "checksum check failed"

echo "version: $VERSION"
echo "tag:     $TAG"
echo "commit:  $COMMIT"
echo "zip:     $REPO/dist/$ZIP ($SIZE bytes)"
echo "sha256:  $(cat "dist/$SHA")"

# 5. Publish
PUBLISH=(gh release create "$TAG" --repo "$GH_REPO" --target "$COMMIT" --title "Deskflow $VERSION"
  --generate-notes --latest "dist/$ZIP" "dist/$SHA")
if [ "$DRY_RUN" = 1 ]; then
  echo "dry run, not running: ${PUBLISH[*]}"
  exit 0
fi
"${PUBLISH[@]}"

# 6. The Windows asset is added by the "Windows release" workflow, started by the published release
echo "macOS assets are published. The Windows installer (Deskflow-x64.msi) is added by GitHub Actions."
echo "Until it appears, the Windows app reports that the release has no Windows asset."
RUN_URL=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
  RUN_URL=$(gh run list --repo "$GH_REPO" --workflow windows-release.yml --event release --limit 5 \
    --json url,headBranch --jq "[.[] | select(.headBranch == \"$TAG\")][0].url // empty" 2>/dev/null || true)
  [ -z "$RUN_URL" ] || break
  sleep 3
done
if [ -n "$RUN_URL" ]; then
  echo "windows build: $RUN_URL"
  echo "watch it with: gh run watch --repo $GH_REPO ${RUN_URL##*/} --exit-status"
else
  echo "windows build: not started yet, see https://github.com/$GH_REPO/actions/workflows/windows-release.yml"
fi
