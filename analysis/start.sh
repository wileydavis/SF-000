#!/bin/bash
# SF-000 Analysis: makes its own Python (once, in its data folder), then runs. Anything given is passed on
# (./start.sh --once, ./start.sh --help).
set -e
here="$(cd "$(dirname "$0")" && pwd)"
if [ "$(uname)" = "Darwin" ]; then data="$HOME/Library/Application Support/SF-000 Analysis"; else data="${XDG_DATA_HOME:-$HOME/.local/share}/sf000-analysis"; fi
mkdir -p "$data"
venv="$data/python"
py="$(command -v python3 || true)"
if [ -z "$py" ]; then
  echo "SF-000 Analysis needs Python 3. On a Mac: run 'xcode-select --install' once (or install Python from python.org), then start this again."
  exit 1
fi
if [ ! -x "$venv/bin/python" ]; then
  echo "Making SF-000 Analysis its own Python in $venv (once)..."
  "$py" -m venv "$venv"
fi
stamp="$venv/.requirements"
if ! cmp -s "$here/requirements.txt" "$stamp" 2>/dev/null; then
  echo "Fetching what it is made with (numpy, scipy, soundfile)..."
  "$venv/bin/python" -m pip install --quiet --upgrade pip
  "$venv/bin/python" -m pip install --quiet -r "$here/requirements.txt"
  cp "$here/requirements.txt" "$stamp"
fi
cd "$here"
exec "$venv/bin/python" -m sfa "$@"
