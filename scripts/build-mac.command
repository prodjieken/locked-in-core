#!/bin/bash
# Double-click in Finder (or run in Terminal) to build locked-in-core + hello-character on this Mac.
# Installs Hello Character into ~/Library/Audio/Plug-Ins (VST3 + AU) and validates the AU.
# Everything is also written to mac-build.log next to this repo's README.

cd "$(dirname "$0")/.." || exit 1
LOG="$PWD/mac-build.log"
: > "$LOG"
exec > >(tee -a "$LOG") 2>&1

step() { echo; echo "==== $*"; }
fail() { echo; echo "BUILD FAILED: $*"; echo "(full log: $LOG)"; echo; read -r -p "Press Return to close"; exit 1; }

step "Tools"
sw_vers 2>/dev/null | tr '\n' ' '; echo; uname -m
xcode-select -p >/dev/null 2>&1 || fail "Xcode Command Line Tools missing. Run:  xcode-select --install   then double-click this again."
xcode-select -p
CMAKE=$(command -v cmake || ls /opt/homebrew/bin/cmake /usr/local/bin/cmake /Applications/CMake.app/Contents/bin/cmake 2>/dev/null | head -1)
[ -n "$CMAKE" ] || fail "CMake missing. Install it with:  brew install cmake   (or from cmake.org), then double-click this again."
"$CMAKE" --version | head -1
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)

step "Configure"
"$CMAKE" -S . -B build-mac "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release || fail "configure"

step "Build (first time takes a few minutes: JUCE is compiled for arm64 + x86_64)"
"$CMAKE" --build build-mac --config Release --parallel "$(sysctl -n hw.ncpu)" || fail "build"

step "Unit tests"
(cd build-mac && ctest -C Release --output-on-failure) || fail "unit tests"

step "Installed plugins"
for p in "$HOME/Library/Audio/Plug-Ins/VST3/Hello Character.vst3" "$HOME/Library/Audio/Plug-Ins/Components/Hello Character.component"; do
    [ -d "$p" ] || fail "not installed: $p"
    echo "$p"
    lipo -archs "$p/Contents/MacOS/Hello Character"
done

step "AU validation"
killall -9 AudioComponentRegistrar 2>/dev/null
auval -v aufx Hlch Lkin > build-mac/auval.txt 2>&1
tail -4 build-mac/auval.txt
grep -q "AU VALIDATION SUCCEEDED" build-mac/auval.txt || fail "auval (see build-mac/auval.txt)"

echo
echo "ALL DONE. Rescan plugins in FL Studio (Options > Manage plugins > Find installed plugins)."
echo "Look for Locked In > Hello Character (VST3 and AU)."
echo
read -r -p "Press Return to close"
