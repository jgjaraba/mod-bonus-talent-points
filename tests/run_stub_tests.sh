#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
for file in tests/stubs/*.h.stub; do
    cp "$file" "$TMP_DIR/$(basename "$file" .stub)"
done
cp tests/test_module.cpp.stub "$TMP_DIR/test_module.cpp"
g++ -std=c++17 -Wall -Wextra -Werror \
  -I"$TMP_DIR" src/BonusTalentPoints.cpp src/GlyphUnlocks.cpp \
  src/DualSpecCustomization.cpp src/mod_bonus_talent_points_loader.cpp \
  "$TMP_DIR/test_module.cpp" -o "$TMP_DIR/test_module"
"$TMP_DIR/test_module"
