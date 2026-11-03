#!/bin/bash
# One-time setup of a stable self-signed code-signing identity in a dedicated keychain.
# Every release is signed with it, so the designated requirement (certificate leaf hash) stays the same
# and macOS keeps TCC grants (Accessibility, Input Monitoring, Screen Recording) across updates.
# Idempotent. Does not touch the login keychain or system trust settings, never prompts.
# Prints the certificate SHA-1 on the last line ("identity sha1: ...").
set -euo pipefail
NAME="Deskflow Personal Signing"
KC="$HOME/Library/Keychains/deskflow-signing.keychain-db"
CFG="$HOME/.config/deskflow-signing"
PWFILE="$CFG/keychain-password"

mkdir -p "$CFG"; chmod 700 "$CFG"
if [ ! -f "$PWFILE" ]; then
  (umask 077; openssl rand -hex 24 > "$PWFILE")
fi
chmod 600 "$PWFILE"
PW=$(cat "$PWFILE")

[ -f "$KC" ] || security create-keychain -p "$PW" "$KC"
security set-keychain-settings "$KC"            # no auto-lock timeout
security unlock-keychain -p "$PW" "$KC"

# Search list: add the keychain, keep existing entries
LIST=()
while IFS= read -r l; do l=${l//\"/}; l=$(echo "$l" | xargs); [ -n "$l" ] && LIST+=("$l"); done < <(security list-keychains -d user)
case " ${LIST[*]} " in *"$KC"*) ;; *) security list-keychains -d user -s "${LIST[@]}" "$KC" ;; esac

if ! security find-certificate -c "$NAME" "$KC" >/dev/null 2>&1; then
  TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
  P12PASS=$(openssl rand -hex 12)
  cat > "$TMP/openssl.cnf" <<CNF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $NAME
[ext]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = critical,codeSigning
CNF
  openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 7300 -config "$TMP/openssl.cnf" \
    -keyout "$TMP/key.pem" -out "$TMP/cert.pem" 2>/dev/null
  # OpenSSL 3 needs -legacy so that `security import` can read the p12; LibreSSL has no such flag.
  openssl pkcs12 -export -legacy -inkey "$TMP/key.pem" -in "$TMP/cert.pem" -name "$NAME" \
    -passout "pass:$P12PASS" -out "$TMP/id.p12" 2>/dev/null ||
  openssl pkcs12 -export -inkey "$TMP/key.pem" -in "$TMP/cert.pem" -name "$NAME" \
    -passout "pass:$P12PASS" -out "$TMP/id.p12"
  security import "$TMP/id.p12" -k "$KC" -P "$P12PASS" -T /usr/bin/codesign >/dev/null
  security set-key-partition-list -S apple-tool:,apple: -s -k "$PW" "$KC" >/dev/null
  rm -rf "$TMP"
fi

SHA1=$(security find-certificate -c "$NAME" -Z "$KC" | awk '/^SHA-1 hash:/{print $3; exit}')
echo "keychain: $KC"
echo "identity sha1: $SHA1"
