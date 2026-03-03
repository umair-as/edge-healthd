#!/bin/bash
# SPDX-License-Identifier: MIT
#
# gen-tls-cert.sh — Generate a self-signed TLS certificate for edge-healthd-ui
#
# Usage:
#   gen-tls-cert.sh [cert-dir] [hostname] [days]
#
#   cert-dir  Directory to write cert.pem and key.pem  (default: /etc/edge/tls)
#   hostname  CN / SAN hostname                         (default: system hostname)
#   days      Certificate validity in days              (default: 3650, ~10 years)
#
# After running, start edge-healthd-ui with:
#   -tls-cert /etc/edge/tls/cert.pem -tls-key /etc/edge/tls/key.pem

set -euo pipefail

CERT_DIR="${1:-/etc/edge/tls}"
CN="${2:-$(hostname)}"
DAYS="${3:-3650}"

CERT_FILE="${CERT_DIR}/cert.pem"
KEY_FILE="${CERT_DIR}/key.pem"

if ! command -v openssl &>/dev/null; then
    echo "error: openssl not found" >&2
    exit 1
fi

# Skip if cert already exists and is still valid
if [[ -f "${CERT_FILE}" && -f "${KEY_FILE}" ]]; then
    if openssl x509 -checkend 86400 -noout -in "${CERT_FILE}" 2>/dev/null; then
        echo "Certificate already exists and is valid: ${CERT_FILE}"
        exit 0
    fi
    echo "Certificate expired or expiring soon — regenerating..."
fi

install -d -m 750 "${CERT_DIR}"

openssl req -x509 \
    -newkey rsa:4096 \
    -keyout "${KEY_FILE}" \
    -out "${CERT_FILE}" \
    -days "${DAYS}" \
    -nodes \
    -subj "/CN=${CN}" \
    -addext "subjectAltName=DNS:${CN},DNS:localhost,IP:127.0.0.1"

chmod 640 "${CERT_FILE}" "${KEY_FILE}"

# If running as root, set ownership to edgehealth if the user exists
if [[ ${EUID} -eq 0 ]] && id edgehealth &>/dev/null; then
    chown root:edgehealth "${CERT_DIR}" "${CERT_FILE}" "${KEY_FILE}"
fi

echo "Certificate written to ${CERT_FILE} (valid ${DAYS} days, CN=${CN})"
