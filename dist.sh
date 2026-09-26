#!/bin/bash
# Sign, notarize and zip Jammer.app for handing out: Jammer.zip, which opens
# on any Mac new enough to run it without Gatekeeper refusing it.
#
#   make dist
#
# Needs, once:
#
#   * A "Developer ID Application" certificate in the login keychain, from
#     developer.apple.com (Certificates, +, Developer ID Application).  Not
#     "Apple Distribution": that's for the App Store.  Picked up on its own,
#     or set DEVELOPER_ID to its name.
#   * The notary service's credentials, saved under a profile:
#
#       xcrun notarytool store-credentials jammer \
#         --apple-id you@example.com --team-id TEAMID
#
#     which asks for an app-specific password (appleid.apple.com, Sign-In
#     and Security).  Or set NOTARY_PROFILE to another profile's name.
#
# With DEVELOPER_ID=- it signs ad-hoc and skips notarizing, to check the
# bundle over without either.

set -e

APP=Jammer.app
ZIP=Jammer.zip
NOTARY_PROFILE="${NOTARY_PROFILE:-jammer}"

if [ -z "$DEVELOPER_ID" ]; then
    DEVELOPER_ID=$(security find-identity -v -p codesigning |
                   sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p' |
                   head -1)
fi
if [ -z "$DEVELOPER_ID" ]; then
    echo "no Developer ID Application certificate in the keychain; see dist.sh"
    exit 1
fi
echo "signing as $DEVELOPER_ID"

# The oldest macOS it'll run on is the newest any of its binaries was built
# for -- homebrew's libraries are built for the Mac that installed them --
# so say so, rather than let an older Mac try and fail.
minos=$(for f in "$APP/Contents/MacOS/jammer" "$APP"/Contents/Frameworks/*; do
          otool -l "$f" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}'
        done | sort -t. -k1,1n -k2,2n | tail -1)
plutil -replace LSMinimumSystemVersion -string "$minos" \
    "$APP/Contents/Info.plist"
echo "runs on macOS $minos and later, $(lipo -archs "$APP/Contents/MacOS/jammer")"

# Inside out: each library, then the app around them.  The hardened runtime
# and a secure timestamp are what notarizing asks for.
sign() {
    codesign --force --options runtime --sign "$DEVELOPER_ID" \
        $([ "$DEVELOPER_ID" = - ] || echo --timestamp) "$@"
}
for lib in "$APP"/Contents/Frameworks/*; do
    sign "$lib"
done
sign --entitlements jammer.entitlements "$APP"
codesign --verify --deep --strict "$APP"

rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
if [ "$DEVELOPER_ID" = - ]; then
    echo "signed ad-hoc, not notarized: $ZIP is for checking, not handing out"
    exit 0
fi

xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
# The ticket goes in the app itself, so it opens offline too; then zip that.
xcrun stapler staple "$APP"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
spctl --assess --type execute -vv "$APP"
echo "built $ZIP"
