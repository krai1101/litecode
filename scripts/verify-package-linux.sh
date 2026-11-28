#!/usr/bin/env bash
# Run inside a graphical X11 session (or xvfb-run). This is a startup check,
# not a substitute for the extension workflow tests or visual review.
set -euo pipefail
if [ "$#" -ne 1 ]; then
    printf 'Usage: bash %s <installed-package-root>\n' "$0" >&2
    exit 2
fi
package=$(cd -- "$1" && pwd -P)
test -x "$package/bin/LiteCode"
test -f "$package/extensions/python-debugger/liblitecode_python_debugger_extension.so"
test -f "$package/extensions/python-debugger/bundled/libs/debugpy/adapter/__main__.py"
profile=$(mktemp -d "${TMPDIR:-/tmp}/litecode-package-desktop.XXXXXX")
export XDG_CONFIG_HOME="$profile/config"
export XDG_DATA_HOME="$profile/data"
export XDG_CACHE_HOME="$profile/cache"
export LITECODE_SETTINGS_FILE="$profile/settings.ini"
export HISTFILE=/dev/null
export QT_QPA_PLATFORM=xcb
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
unset LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$profile/workspace"
printf 'Isolated package startup log: %s/startup.log\n' "$profile"
set +e
timeout --signal=TERM --kill-after=5s 12s "$package/bin/LiteCode" \
    --new-window --folder "$profile/workspace" > "$profile/startup.log" 2>&1
status=$?
set -e
cat "$profile/startup.log"
if [ "$status" -ne 124 ]; then
    printf 'Package exited before the startup observation ended (status %s).\n' "$status" >&2
    exit 1
fi
if ! grep -q 'startup.first_frame_ms=' "$profile/startup.log"; then
    printf 'Package did not report its first frame.\n' >&2
    exit 1
fi
