#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if ! command -v pio >/dev/null 2>&1; then
  echo "PlatformIO CLI not found. Install it or run this from an environment with 'pio'."
  exit 1
fi

# Test order (included if env exists in platformio.ini):
# 1) tdeck
# 2) cardputer
# 3) pager
TARGET_LABELS=(
  "tdeck"
  "cardputer"
  "pager"
)

TARGET_ENVS=(
  "tdeck"
  "cardputer-cap"
  "tlora-pager-tft"
)

has_env() {
  local env_name="$1"
  grep -q "^\[env:${env_name}\]" platformio.ini
}

ACTIVE_LABELS=()
ACTIVE_ENVS=()

echo "[TEST] Checking target environments in platformio.ini..."
for i in "${!TARGET_ENVS[@]}"; do
  label="${TARGET_LABELS[$i]}"
  env_name="${TARGET_ENVS[$i]}"
  if has_env "$env_name"; then
    ACTIVE_LABELS+=("$label")
    ACTIVE_ENVS+=("$env_name")
    echo "[TEST] Found target: ${label} (${env_name})"
  else
    echo "[TEST] Skipping missing target: ${label} (${env_name})"
  fi
done

if [ "${#ACTIVE_ENVS[@]}" -eq 0 ]; then
  echo "[TEST] No target environments found in platformio.ini"
  exit 1
fi

echo "[TEST] Build phase: compiling all targets (no uploads)..."
for i in "${!ACTIVE_ENVS[@]}"; do
  label="${ACTIVE_LABELS[$i]}"
  env_name="${ACTIVE_ENVS[$i]}"
  echo ""
  echo "[BUILD] ${label} (${env_name})"
  pio run -e "$env_name"
done

echo ""
echo "[TEST] Build phase complete. Starting upload test loop."

for i in "${!ACTIVE_ENVS[@]}"; do
  label="${ACTIVE_LABELS[$i]}"
  env_name="${ACTIVE_ENVS[$i]}"

  echo ""
  echo "[UPLOAD] Next target: ${label} (${env_name})"
  while true; do
    echo "  1) Upload"
    echo "  2) Skip"
    read -r -p "Choose [1-2]: " choice
    # Accept typical terminal input variants (spaces / CR) so 2 reliably skips.
    choice="${choice//$'\r'/}"
    choice="${choice#${choice%%[![:space:]]*}}"
    choice="${choice%${choice##*[![:space:]]}}"
    case "$choice" in
      1)
        pio run -e "$env_name" -t upload
        break
        ;;
      2)
        echo "[UPLOAD] Skipped ${label} (${env_name})."
        break
        ;;
      *)
        echo "Invalid choice. Enter 1 to upload or 2 to skip."
        ;;
    esac
  done
done

echo ""
echo "[TEST] Completed upload testing for all targets."