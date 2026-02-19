#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

VERSION="${VERSION:-ntsc-1.2}"
REGION="${REGION:-US}"
COMPARE="${COMPARE:-0}"
JOBS="${JOBS:-$(nproc)}"

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
    docker compose run --rm oot make VERSION="$VERSION" REGION="$REGION" clean
fi

docker compose run --rm oot make -j"$JOBS" VERSION="$VERSION" REGION="$REGION" setup
docker compose run --rm oot make -j"$JOBS" VERSION="$VERSION" REGION="$REGION" COMPARE="$COMPARE"
docker compose run --rm oot make -j"$JOBS" VERSION="$VERSION" REGION="$REGION" COMPARE="$COMPARE" compress
