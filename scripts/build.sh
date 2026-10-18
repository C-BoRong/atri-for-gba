#!/usr/bin/env bash
set -euo pipefail
export DEVKITPRO="$(cygpath -u "$DEVKITPRO")"
export DEVKITARM="$(cygpath -u "$DEVKITARM")"
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:/usr/bin:$PATH"
if [ -n "${GBA_PYTHON:-}" ]; then
    export PYTHON="$(cygpath -u "$GBA_PYTHON")"
fi
make "$@"
