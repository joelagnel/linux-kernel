#!/bin/bash -x
#
# This updates my local ChromeOS tree with the latest set of OOT patches in this
# tree.
# For this to work correctly, the $BASE_COMMIT should point to the base.
#
# Since BASE_COMMIT is for now set to cros/chromeos-4.19, it is required that the
# patches be rebased on cros/chromeos-4.19 before running this script, especially if
# "cros" was fetched once and cros/chromeos-4.19 is newer that when the branch was
# previously rebased.
#
BASE_COMMIT="cros/chromeos-4.19"

# Symlink to ../sys-kernel/chromeos-kernel-4_19/files/
OOT_FILES="$HOME/4.19-files/"

rm -rf $OOT_FILES/*.patch

# Add a tag for later reference
TAG="oot-$(git rev-parse --verify HEAD --short=10)"
git tag $TAG

# Add OOT patches
git format-patch $BASE_COMMIT..oot-end -o $OOT_FILES/

sed -i -e "s/TREE_EXTRA_VERSION/$TAG/g" $OOT_FILES/*.patch

# push the tag to github
git push joel $TAG

cd $OOT_FILES/
git add *.patch
git commit -asm "OOT patch updates with tag $TAG"
