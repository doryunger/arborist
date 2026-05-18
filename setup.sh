#!/usr/bin/env bash
set -e

MISSING=()

check() {
    local name=$1
    local cmd=$2
    if ! command -v "$cmd" &>/dev/null; then
        echo "  [ ] $name"
        MISSING+=("$name")
    else
        echo "  [x] $name ($($cmd --version 2>&1 | head -1))"
    fi
}

check_pkg() {
    local name=$1
    local pkg=$2
    if ! dpkg -s "$pkg" &>/dev/null; then
        echo "  [ ] $name"
        MISSING+=("$pkg")
    else
        echo "  [x] $name"
    fi
}

echo "Checking dependencies..."
check      "cmake"      cmake
check      "make"       make
check      "g++"        g++
check_pkg  "GTest"      libgtest-dev
check_pkg  "GMock"      libgmock-dev
check_pkg  "yaml-cpp"   libyaml-cpp-dev

if [ ${#MISSING[@]} -eq 0 ]; then
    echo ""
    echo "All dependencies satisfied."
    exit 0
fi

echo ""
echo "Missing: ${MISSING[*]}"
echo "Installing..."
sudo apt-get update -qq
sudo apt-get install -y cmake make g++ libgtest-dev libgmock-dev libyaml-cpp-dev
echo ""
echo "Done. All dependencies installed."
