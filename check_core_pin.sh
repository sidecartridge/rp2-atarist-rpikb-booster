#!/bin/bash
# Check that the rp2-atarist-rpikb submodule committed at HEAD is pinned to the
# commit of the core firmware tag matching version.txt. Builds use whatever
# commit the submodule is pinned at; a release additionally needs that commit
# to be the core firmware's own release tag. Run before tagging a release.
set -euo pipefail

CORE=rp2-atarist-rpikb
VERSION=$(tr -d '\r\n ' < version.txt)
PIN=$(git rev-parse "HEAD:$CORE")
URL=$(git config -f .gitmodules "submodule.$CORE.url")

# An annotated tag lists its tag object and, as "^{}", the commit it points
# at; a lightweight tag lists only the commit. Sorting puts "^{}" last.
TAG_COMMIT=$(git ls-remote --tags "$URL" "refs/tags/$VERSION" "refs/tags/$VERSION^{}" \
  | sort -k2 | tail -n1 | cut -f1)

if [ -z "$TAG_COMMIT" ]; then
  echo "ERROR: $CORE has no tag $VERSION at $URL"
  echo "  Tag and push the core firmware release first."
  exit 1
fi
if [ "$TAG_COMMIT" != "$PIN" ]; then
  echo "ERROR: $CORE is pinned at ${PIN:0:7}, but its tag $VERSION is ${TAG_COMMIT:0:7}"
  echo "  Move the submodule to the tag's commit and commit the new pin."
  exit 1
fi
echo "$CORE pinned at its tag $VERSION (${PIN:0:7})"
