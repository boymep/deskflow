#!/bin/bash
# One-time setup: create a self-signed code-signing identity in the login keychain.
# Signing every build with the same identity keeps the designated requirement stable, so macOS
# keeps the TCC grants (Accessibility / Input Monitoring) across auto-updates.
#
# usage: create-signing-cert.sh        (identity name: $DESKFLOW_SIGN_IDENTITY, default "Deskflow Self-Signed")
# Idempotent: exits 0 if the identity already exists.
# Note: the trust step (add-trusted-cert) asks for your macOS password.
set -euo pipefail
NAME="${DESKFLOW_SIGN_IDENTITY:-Deskflow Self-Signed}"
KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"

if security find-identity -v -p codesigning | grep -qF "\"$NAME\""; then
  echo "Identity \"$NAME\" already exists, nothing to do."
  exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PASS="deskflow-$$-$RANDOM"

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

openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 3650 \
  -config "$TMP/openssl.cnf" -keyout "$TMP/key.pem" -out "$TMP/cert.pem" 2>/dev/null

# OpenSSL 3 needs -legacy so that `security import` can read the p12; LibreSSL has no such flag.
if ! openssl pkcs12 -export -legacy -inkey "$TMP/key.pem" -in "$TMP/cert.pem" \
     -name "$NAME" -passout "pass:$PASS" -out "$TMP/id.p12" 2>/dev/null; then
  openssl pkcs12 -export -inkey "$TMP/key.pem" -in "$TMP/cert.pem" \
    -name "$NAME" -passout "pass:$PASS" -out "$TMP/id.p12"
fi

security import "$TMP/id.p12" -k "$KEYCHAIN" -P "$PASS" -T /usr/bin/codesign

echo "Marking the certificate as trusted for code signing: macOS will ask for your password."
security add-trusted-cert -r trustRoot -p codeSign -k "$KEYCHAIN" "$TMP/cert.pem"

echo "Result:"
security find-identity -v -p codesigning | grep -F "\"$NAME\"" \
  || { echo "ERROR: identity \"$NAME\" not found as valid after import" >&2; exit 1; }
