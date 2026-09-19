#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENV_FILE="$SCRIPT_DIR/.env"

if [[ ! -f "$ENV_FILE" ]]; then
    echo "ERROR: $ENV_FILE not found. Copy .env.example to .env and fill in values."
    exit 1
fi

set -a
# shellcheck source=/dev/null
source "$ENV_FILE"
: "${LEAF_PH_SENSOR_ENABLED:=1}"
: "${LEAF_EC_SENSOR_ENABLED:=1}"
: "${LEAF_WATER_LEVEL_SENSOR_ENABLED:=1}"
: "${LEAF_FLOW_SENSOR_ENABLED:=1}"
set +a

TARGET="${1:-}"
ACTION="${2:-}"

if [[ -n "$TARGET" ]]; then
    pio run -e "$TARGET" -d "$SCRIPT_DIR"
else
    pio run -d "$SCRIPT_DIR"
fi

if [[ "$ACTION" == "upload" ]]; then
    if [[ -n "$TARGET" ]]; then
        pio run -e "$TARGET" -d "$SCRIPT_DIR" --target upload
    else
        pio run -d "$SCRIPT_DIR" --target upload
    fi
fi
