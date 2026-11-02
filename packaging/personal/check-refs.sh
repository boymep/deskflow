#!/bin/bash
# usage: check-refs.sh App.app  -> prints offending refs and unresolved @rpath refs
APP="$1"
find "$APP" -type f \( -perm -111 -o -name "*.dylib" \) | while read f; do
  file "$f" | grep -q Mach-O || continue
  otool -L "$f" | tail -n +2 | awk '{print $1}' | grep -E "^/opt/homebrew|^/usr/local" | sed "s|^|$f -> |"
done
