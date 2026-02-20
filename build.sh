#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

VERSION="${VERSION:-ntsc-1.2}"
REGION="${REGION:-US}"
COMPARE="${COMPARE:-0}"
JOBS="${JOBS:-$(nproc)}"
RELOCATE="${RELOCATE:-1}"
CUTOFF="${CUTOFF:-0xFD0000}"
LOCK_RELOCATE_FILE="${LOCK_RELOCATE_FILE:-config/relocate_kokiri_ydan_names.txt}"
BOOT_RELOCATE_FILE="${BOOT_RELOCATE_FILE:-config/relocate_boot_names.txt}"
GUARD_RELOCATE_FILE="${GUARD_RELOCATE_FILE:-config/relocate_guard_names.txt}"
SEED_RELOCATE_FILE="${SEED_RELOCATE_FILE:-config/relocate_kokiri_ydan_seed.txt}"
AUTO_GENERATE_RELOCATE_NAMES="${AUTO_GENERATE_RELOCATE_NAMES:-1}"

CLEAN=0
MERGED_NAMES_FILE=".relocate_names_merged.csv"

cleanup() {
    if [[ -n "$MERGED_NAMES_FILE" && -f "$MERGED_NAMES_FILE" ]]; then
        rm -f "$MERGED_NAMES_FILE"
    fi
}
trap cleanup EXIT

usage() {
    cat <<'EOF'
Usage: ./build.sh [--clean] [--help]

Builds the ROM with Docker and also runs the compress target.

Environment overrides:
  VERSION   (default: ntsc-1.2)
  REGION    (default: US)
  COMPARE   (default: 0)
  JOBS      (default: nproc)
  RELOCATE  (default: 1)
  CUTOFF    (default: 0xFD0000)
  LOCK_RELOCATE_FILE (default: config/relocate_kokiri_ydan_names.txt)
  BOOT_RELOCATE_FILE (default: config/relocate_boot_names.txt)
  GUARD_RELOCATE_FILE (default: config/relocate_guard_names.txt)
  SEED_RELOCATE_FILE (default: config/relocate_kokiri_ydan_seed.txt)
  AUTO_GENERATE_RELOCATE_NAMES (default: 1)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)
            CLEAN=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ "$CLEAN" -eq 1 ]]; then
    docker compose run --rm oot make VERSION="$VERSION" REGION="$REGION" RELOCATE="$RELOCATE" clean
fi

if [[ "$RELOCATE" != "0" ]]; then
    docker compose run --rm oot bash -lc "cd /oot && rm -f build/${VERSION}/relocate_priority_indices.txt"
fi

docker compose run --rm oot make -j"$JOBS" VERSION="$VERSION" REGION="$REGION" RELOCATE="$RELOCATE" setup
docker compose run --rm oot make -j"$JOBS" VERSION="$VERSION" REGION="$REGION" COMPARE="$COMPARE" RELOCATE="$RELOCATE"

if [[ "$RELOCATE" != "0" && "$AUTO_GENERATE_RELOCATE_NAMES" != "0" && -f "$SEED_RELOCATE_FILE" ]]; then
    SEED_SEGMENTS="$(cat "$SEED_RELOCATE_FILE")"
    if [[ -n "${SEED_SEGMENTS//[[:space:]]/}" ]]; then
        docker compose run --rm oot bash -lc "cd /oot && \
            DMADATA_START=\$(./tools/dmadata_start.sh mips-linux-gnu-nm build/${VERSION}/oot-${VERSION}.elf) && \
            PYTHONPATH=/oot/tools python3 tools/scan_area_dependencies.py \
                --version ${VERSION} \
                --dmadata-start \"\$DMADATA_START\" \
                --segments \"${SEED_SEGMENTS}\" \
                --emit-relocate-names ${LOCK_RELOCATE_FILE}"
    fi
fi

MAKE_RELOCATE_NAMES_ARG=()
if [[ "$RELOCATE" != "0" ]]; then
    BOOT_NAMES=""
    LOCK_NAMES=""
    GUARD_NAMES=""
    if [[ -f "$BOOT_RELOCATE_FILE" ]]; then
        BOOT_NAMES="$(cat "$BOOT_RELOCATE_FILE")"
    fi
    if [[ -f "$LOCK_RELOCATE_FILE" ]]; then
        LOCK_NAMES="$(cat "$LOCK_RELOCATE_FILE")"
    fi
    if [[ -f "$GUARD_RELOCATE_FILE" ]]; then
        GUARD_NAMES="$(cat "$GUARD_RELOCATE_FILE")"
    fi
    MERGED_NAMES="$(printf '%s,%s,%s' "$BOOT_NAMES" "$LOCK_NAMES" "$GUARD_NAMES" | awk -F',' '
        {
            for (i = 1; i <= NF; i++) {
                gsub(/^[ \t\r\n]+|[ \t\r\n]+$/, "", $i)
                if ($i != "" && !seen[$i]++) {
                    out[++n] = $i
                }
            }
        }
        END {
            for (i = 1; i <= n; i++) {
                printf "%s%s", out[i], (i < n ? "," : "")
            }
        }')"
    if [[ -n "$MERGED_NAMES" ]]; then
        printf '%s' "$MERGED_NAMES" > "$MERGED_NAMES_FILE"
        MAKE_RELOCATE_NAMES_ARG=(RELOCATE_PRIORITY_NAMES="$MERGED_NAMES")
    fi
fi

docker compose run --rm oot make -j"$JOBS" VERSION="$VERSION" REGION="$REGION" COMPARE="$COMPARE" RELOCATE="$RELOCATE" "${MAKE_RELOCATE_NAMES_ARG[@]}" compress

if [[ "$RELOCATE" != "0" ]]; then
    CHECK_NAMES_ARGS=("--names-file" "$MERGED_NAMES_FILE")
    if [[ ! -f "$MERGED_NAMES_FILE" ]]; then
        echo "Missing merged relocate names file; refusing to run cutoff check." >&2
        exit 1
    fi

    docker compose run --rm oot bash -lc "cd /oot && \
        DMADATA_START=\$(./tools/dmadata_start.sh mips-linux-gnu-nm build/${VERSION}/oot-${VERSION}.elf) && \
        python3 tools/check_relocate_cutoff.py \
            --rom build/${VERSION}/oot-${VERSION}-compressed.z64 \
            --spec build/${VERSION}/dmadata_table_spec.h \
            --indices build/${VERSION}/relocate_priority_indices.txt \
            ${CHECK_NAMES_ARGS[*]} \
            --dmadata-start \"\$DMADATA_START\" \
            --cutoff ${CUTOFF}"
fi
