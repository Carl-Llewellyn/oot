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

CLEAN=0

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
docker compose run --rm oot make -j"$JOBS" VERSION="$VERSION" REGION="$REGION" COMPARE="$COMPARE" RELOCATE="$RELOCATE" compress

if [[ "$RELOCATE" != "0" ]]; then
    docker compose run --rm oot bash -lc "cd /oot && \
        DMADATA_START=\$(./tools/dmadata_start.sh mips-linux-gnu-nm build/${VERSION}/oot-${VERSION}.elf) && \
        python3 tools/check_relocate_cutoff.py \
            --rom build/${VERSION}/oot-${VERSION}-compressed.z64 \
            --spec build/${VERSION}/dmadata_table_spec.h \
            --indices build/${VERSION}/relocate_priority_indices.txt \
            --dmadata-start \"\$DMADATA_START\" \
            --cutoff ${CUTOFF}"
fi
