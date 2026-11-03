#!/bin/bash
# Build a self-contained signed Deskflow.app (arm64) from brew Qt and zip it.
# Note: the official `cmake --install` path (deploy/mac/deploy.cmake -> macdeployqt) misses
# brew's split Qt kegs (QtSvg/QtPdf/QtVirtualKeyboard), so macdeployqt is run manually with -libpath.
set -eo pipefail
REPO=/Users/boymep/Projects/deskflow
LOG_DIR="${LOG_DIR:-$REPO/build}"
cd "$REPO"
cmake build >/dev/null                         # re-configure so GIT_SHA_SHORT matches HEAD
cmake --build build -j"$(sysctl -n hw.ncpu)" | tail -3 || cmake --build build | tail -3   # retry once: OSXKeyStateTests is occasionally flaky (uses live keyboard state)
git checkout -- translations
rm -rf dist && mkdir dist                       # dist/ is git-ignored
ditto build/bin/Deskflow.app dist/Deskflow.app
C=dist/Deskflow.app/Contents
"$(brew --prefix qt)/bin/macdeployqt" dist/Deskflow.app \
  -executable=$C/MacOS/deskflow-core \
  -libpath=/opt/homebrew/lib -libpath="$(brew --prefix openssl@3)/lib" > "$LOG_DIR/deskflow-macdeployqt.log" 2>&1
# QtSvg (needed for the app's SVG icon theme) is not picked up from the qtsvg keg: bundle by hand
ditto /opt/homebrew/lib/QtSvg.framework/ $C/Frameworks/QtSvg.framework
rm -rf $C/Frameworks/QtSvg.framework/Headers $C/Frameworks/QtSvg.framework/Versions/A/Headers
chmod -R u+w $C/Frameworks/QtSvg.framework
B=$C/Frameworks/QtSvg.framework/Versions/A/QtSvg
install_name_tool -id @rpath/QtSvg.framework/Versions/A/QtSvg $B 2>/dev/null
install_name_tool -change /opt/homebrew/opt/qtbase/lib/QtGui.framework/Versions/A/QtGui @rpath/QtGui.framework/Versions/A/QtGui \
  -change /opt/homebrew/opt/qtbase/lib/QtCore.framework/Versions/A/QtCore @rpath/QtCore.framework/Versions/A/QtCore $B 2>/dev/null
P=$C/PlugIns/imageformats/libqsvg.dylib
cp /opt/homebrew/share/qt/plugins/imageformats/libqsvg.dylib $P; chmod u+w $P
install_name_tool -change /opt/homebrew/opt/qtbase/lib/QtGui.framework/Versions/A/QtGui @loader_path/../../Frameworks/QtGui.framework/Versions/A/QtGui \
  -change /opt/homebrew/opt/qtbase/lib/QtCore.framework/Versions/A/QtCore @loader_path/../../Frameworks/QtCore.framework/Versions/A/QtCore $P 2>/dev/null
for p in $P $C/PlugIns/iconengines/libqsvgicon.dylib; do install_name_tool -add_rpath @loader_path/../../Frameworks $p 2>/dev/null; done
# Unused plugins whose frameworks (QtPdf, QtVirtualKeyboard) are not bundled
rm -f $C/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib $C/PlugIns/imageformats/libqpdf.dylib
rmdir $C/PlugIns/platforminputcontexts 2>/dev/null || true
# Strip absolute brew LC_RPATHs left by macdeployqt, give the GUI a bundle rpath
for f in $(find $C -type f); do
  file "$f" | grep -q Mach-O || continue
  for rp in $(otool -l "$f" | grep -A2 LC_RPATH | awk '/ path /{print $2}' | grep -E '^/opt/homebrew|^/usr/local' || true); do
    install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null
  done
done
install_name_tool -add_rpath @executable_path/../Frameworks $C/MacOS/Deskflow 2>/dev/null || true
printf '[Paths]\nPlugins = PlugIns\nImports = Resources/qml\nQml2Imports = Resources/qml\n' > $C/Resources/qt.conf
# Sign and verify
xattr -cr dist/Deskflow.app
# Stable self-signed identity keeps the designated requirement (and TCC grants) the same across builds.
# Create it once with packaging/personal/create-signing-cert.sh. DESKFLOW_ADHOC_SIGN=1 falls back to ad-hoc.
# The bundle id (org.deskflow.deskflow) comes from Info.plist, so the code identifier stays stable.
SIGN_IDENTITY="${DESKFLOW_SIGN_IDENTITY:-Deskflow Self-Signed}"
if [ "${DESKFLOW_ADHOC_SIGN:-0}" = 1 ]; then
  codesign --force --deep --sign - dist/Deskflow.app
else
  if ! security find-identity -v -p codesigning | grep -qF "\"$SIGN_IDENTITY\""; then
    echo "ERROR: code-signing identity \"$SIGN_IDENTITY\" not found. Run packaging/personal/create-signing-cert.sh once (or set DESKFLOW_ADHOC_SIGN=1)." >&2
    exit 1
  fi
  codesign --force --deep --sign "$SIGN_IDENTITY" --timestamp=none dist/Deskflow.app
fi
codesign --verify --deep --strict --verbose=2 dist/Deskflow.app
codesign -d -r- dist/Deskflow.app
# Offending absolute refs (must be 0)
echo "offending refs: $("$REPO/packaging/personal/check-refs.sh" dist/Deskflow.app | wc -l | tr -d ' ')"
# Zip without AppleDouble ._ files: other unzip tools turn them into real files and break the code seal
ditto -c -k --norsrc --noextattr --noqtn --keepParent dist/Deskflow.app dist/Deskflow-arm64.zip
ls -l dist/Deskflow-arm64.zip; shasum -a 256 dist/Deskflow-arm64.zip
env -u DYLD_PRINT_LIBRARIES dist/Deskflow.app/Contents/MacOS/deskflow-core --version | head -1
