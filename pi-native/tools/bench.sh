#!/usr/bin/env bash
# Benchmark every mode with the Pi preset and report PASS/FAIL against p99 <= 33.3 ms.
#
#   tools/bench.sh http://192.168.1.10/ [seconds]
#
# The kiosk service holds the display, so it is stopped for the run and restarted afterwards.
set -uo pipefail

SOURCE="${1:?usage: $0 <source> [seconds]}"
SECONDS_PER_MODE="${2:-120}"
BIN="${IMAGETUNNEL_BIN:-/opt/imagetunnel/bin/imagetunnel}"
# Benchmark with the same device arguments the service uses (e.g. --rotate 90 --frame-cap 30).
IMAGETUNNEL_ARGS=""
[[ -r /etc/default/imagetunnel ]] && source /etc/default/imagetunnel
OUT="${BENCH_OUT:-bench-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"

restart_service=0
if systemctl is-active --quiet imagetunnel; then
    sudo systemctl stop imagetunnel
    restart_service=1
fi

status=0
for mode in floating tunnel grid maze; do
    echo "==> $mode (${SECONDS_PER_MODE}s)"
    # A throwaway config dir so a saved local override does not skew results.
    # shellcheck disable=SC2086
    SDL_VIDEODRIVER=kmsdrm "$BIN" --source "$SOURCE" --config-dir "$OUT/cfg" $IMAGETUNNEL_ARGS \
        --bench "$mode" --seconds "$SECONDS_PER_MODE" --csv "$OUT/$mode.csv" | tee -a "$OUT/summary.txt" | grep '^BENCH'
    [[ ${PIPESTATUS[0]} -eq 0 ]] || status=1
done

[[ $restart_service -eq 1 ]] && sudo systemctl start imagetunnel
echo "Results in $OUT/ (summary.txt, per-mode CSV)"
exit $status
