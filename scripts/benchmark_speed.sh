#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
ROM_PATH="${HOME}/pokefirered/pokefirered.gba"
FRAMES=240000
RUNS=3

usage() {
    cat <<'EOF'
Usage: benchmark_speed.sh [--frames N] [--runs N] [--rom path]
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --frames)
            FRAMES="$2"
            shift 2
            ;;
        --runs)
            RUNS="$2"
            shift 2
            ;;
        --rom)
            ROM_PATH="$2"
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ ! -x "${BUILD_DIR}/pfr_play" ]]; then
    echo "Missing ${BUILD_DIR}/pfr_play" >&2
    exit 1
fi

if [[ ! -x "${BUILD_DIR}/mgba_bench" ]]; then
    echo "Missing ${BUILD_DIR}/mgba_bench" >&2
    exit 1
fi

if [[ ! -f "${ROM_PATH}" ]]; then
    echo "Missing ROM: ${ROM_PATH}" >&2
    exit 1
fi

TMP_DIR="$(mktemp -d /tmp/pfr-bench.XXXXXX)"
RESULTS_TSV="${TMP_DIR}/results.tsv"
trap 'rm -rf "${TMP_DIR}"' EXIT

extract_field() {
    local line="$1"
    local key="$2"

    tr ' ' '\n' <<<"${line}" | awk -F= -v key="${key}" '$1 == key { print $2; exit }'
}

run_case() {
    local label="$1"
    shift
    local run

    for ((run = 1; run <= RUNS; run++)); do
        local log_path="${TMP_DIR}/${label// /_}_${run}.log"
        local result_line elapsed fps presents speed

        (
            cd "${BUILD_DIR}"
            "$@"
        ) >"${log_path}" 2>&1

        result_line="$(grep 'benchmark_result ' "${log_path}" | tail -n 1 || true)"
        if [[ -z "${result_line}" ]]; then
            echo "No benchmark_result line for ${label} run ${run}" >&2
            cat "${log_path}" >&2
            exit 1
        fi

        elapsed="$(extract_field "${result_line}" elapsed_seconds)"
        fps="$(extract_field "${result_line}" emulated_fps)"
        presents="$(extract_field "${result_line}" presents)"
        speed="$(extract_field "${result_line}" speedup_vs_realtime)"

        printf "%s\t%d\t%s\t%s\t%s\t%s\n" "${label}" "${run}" "${elapsed}" "${fps}" "${presents}" "${speed}" >>"${RESULTS_TSV}"
    done
}

run_case "native-fast-forward-4096" env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "${BUILD_DIR}/pfr_play" --fast-forward 4096 --benchmark-frames "${FRAMES}"
run_case "native-max-speed" "${BUILD_DIR}/pfr_play" --max-speed --benchmark-frames "${FRAMES}"
run_case "mgba-headless" "${BUILD_DIR}/mgba_bench" "${ROM_PATH}" --frames "${FRAMES}"

printf "Benchmark frames per run: %s\n" "${FRAMES}"
printf "Benchmark runs per case: %s\n\n" "${RUNS}"
printf "%-24s %4s %12s %14s %10s %12s\n" "case" "run" "elapsed_s" "emu_fps" "presents" "speedup"
awk -F'\t' '{
    printf "%-24s %4s %12.6f %14.2f %10s %11.2fx\n", $1, $2, $3 + 0, $4 + 0, $5, $6 + 0
}' "${RESULTS_TSV}"

printf "\n%-24s %12s %14s %12s\n" "case" "avg_s" "avg_emu_fps" "vs_mgba"
mgba_avg="$(awk -F'\t' '$1 == "mgba-headless" { sum += $4; count++ } END { if (count) printf "%.8f", sum / count; }' "${RESULTS_TSV}")"
awk -F'\t' -v mgba_avg="${mgba_avg}" '
{
    elapsed[$1] += $3;
    fps[$1] += $4;
    count[$1] += 1;
}
END {
    for (case_name in count) {
        avg_elapsed = elapsed[case_name] / count[case_name];
        avg_fps = fps[case_name] / count[case_name];
        ratio = (mgba_avg + 0 > 0) ? avg_fps / (mgba_avg + 0) : 0;
        printf "%-24s %12.6f %14.2f %11.2fx\n", case_name, avg_elapsed, avg_fps, ratio;
    }
}' "${RESULTS_TSV}" | sort
