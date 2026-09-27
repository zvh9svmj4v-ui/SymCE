#!/bin/sh
# Fetch fasmg (flat assembler g, flatassembler.net) into tools/bin/fasmg.
# Nothing is installed system-wide. On Apple Silicon the macOS build is x86-64
# and runs under Rosetta.
set -e
cd "$(dirname "$0")"
tmp=bin/.fasmg-src
rm -rf "$tmp" && mkdir -p "$tmp"
zip=$(curl -fsSL https://flatassembler.net/download.php | grep -o 'fasmg\.[a-z0-9]*\.zip' | head -1)
curl -fsSL "https://flatassembler.net/$zip" -o "$tmp/fasmg.zip"
unzip -q "$tmp/fasmg.zip" -d "$tmp"
case "$(uname -s)" in
    Darwin) cp "$tmp/source/macos/x64/fasmg" bin/fasmg ;;
    *)      cp "$tmp/fasmg.x64" bin/fasmg ;;
esac
chmod +x bin/fasmg
rm -rf "$tmp"
bin/fasmg 2>&1 | head -1
