#!/bin/bash
# Double-click in Finder: SF-000 Analysis in a Terminal window. Close the window (or press Ctrl-C) to stop it.
exec "$(dirname "$0")/start.sh" "$@"
