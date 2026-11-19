#!/usr/bin/env bash
# Homebrew no longer keeps a local homebrew/core clone, so
# `brew extract --version=3.31.0 cmake` cannot see the formula.
# Install Kitware's CMake 3.31.0 instead. The macOS archive is a
# CMake.app bundle; there is no top-level bin/ directory.
# Prints the directory that contains the cmake executable.
set -euo pipefail

ver=3.31.0
sha256=50d5b9f370e71c8eee87c123b7fb9272caf2bf2b372ea7c9423f10f1cd52813b
prefix=/opt/cmake-3.31
bindir="${prefix}/CMake.app/Contents/bin"
url="https://github.com/Kitware/CMake/releases/download/v${ver}/cmake-${ver}-macos-universal.tar.gz"

if ! "${bindir}/cmake" --version 2>/dev/null | grep -q "cmake version ${ver}"; then
    tmp="$(mktemp)"
    curl -fsSL -o "${tmp}" "${url}"
    echo "${sha256}  ${tmp}" | shasum -a 256 -c - >&2
    sudo mkdir -p "${prefix}"
    sudo tar -xzf "${tmp}" -C "${prefix}" --strip-components=1
    rm -f "${tmp}"
fi

brew unlink cmake >/dev/null 2>&1 || true
if [[ -n "${GITHUB_PATH:-}" ]]; then
    echo "${bindir}" >> "${GITHUB_PATH}"
fi
echo "${bindir}"
