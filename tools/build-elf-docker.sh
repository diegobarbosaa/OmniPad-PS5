#!/usr/bin/env bash
# OmniPad PS5 — Docker Build Script for Linux / macOS
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "[*] Building Docker image omnipad-build..."
docker build -t omnipad-build "${PROJ_DIR}"

if [ -z "$1" ]; then
    echo "[*] Compiling ELF with built-in PS5 Payload SDK..."
    docker run --rm -v "${PROJ_DIR}":/work omnipad-build make ps5
else
    echo "[*] Compiling ELF with local SDK at $1..."
    docker run --rm -v "$1":/opt/ps5-payload-sdk -v "${PROJ_DIR}":/work -e PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk omnipad-build make ps5
fi

echo "[+] Done! Check dist/OmniPad-PS5.elf"
