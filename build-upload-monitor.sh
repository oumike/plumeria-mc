#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SELECTED_ENV_NAME="tlora-pager-tft"
TDECK_ENV_NAME="tdeck"
CARDPUTER_ENV_NAME="cardputer-cap"
HELTEC_ENV_NAME="heltec-v4"
HELTEC_VERTICAL_ENV_NAME="heltec-v4-vertical"
TLORA_PAGER_ENV_NAME="tlora-pager-tft"
ENV_SELECTED_EXPLICITLY=false
SHOULD_ERASE_FIRST=false

has_env() {
	local env_name="$1"
	grep -q "^\[env:${env_name}\]" platformio.ini
}

select_env_or_exit() {
	local env_name="$1"
	local not_found_msg="$2"
	local extra_msg="${3:-}"

	if has_env "$env_name"; then
		SELECTED_ENV_NAME="$env_name"
		ENV_SELECTED_EXPLICITLY=true
		return
	fi

	echo "$not_found_msg"
	if [ -n "$extra_msg" ]; then
		echo "$extra_msg"
	fi
	exit 1
}

prompt_for_device() {
	local options=()
	local labels=()

	if has_env "$TDECK_ENV_NAME"; then
		options+=("$TDECK_ENV_NAME")
		labels+=("LilyGo T-Deck")
	fi
	if has_env "$CARDPUTER_ENV_NAME"; then
		options+=("$CARDPUTER_ENV_NAME")
		labels+=("M5Stack Cardputer + Cap LoRa/GPS")
	fi
	if has_env "$TLORA_PAGER_ENV_NAME"; then
		options+=("$TLORA_PAGER_ENV_NAME")
		labels+=("LilyGo T-Lora Pager TFT")
	fi
	if has_env "$HELTEC_ENV_NAME"; then
		options+=("$HELTEC_ENV_NAME")
		labels+=("Heltec V4 Expansion Kit")
	fi
	if has_env "$HELTEC_VERTICAL_ENV_NAME"; then
		options+=("$HELTEC_VERTICAL_ENV_NAME")
		labels+=("Heltec V4 Expansion Kit (Vertical UI)")
	fi

	if [ "${#options[@]}" -eq 0 ]; then
		echo "No supported device environments found in platformio.ini"
		exit 1
	fi

	if [ ! -t 0 ]; then
		SELECTED_ENV_NAME="${options[0]}"
		echo "[PIO] Non-interactive shell detected, using device env: $SELECTED_ENV_NAME"
		return
	fi

	echo "Select device to build for:"
	for i in "${!options[@]}"; do
		printf "  %d) %s (%s)\n" "$((i + 1))" "${labels[$i]}" "${options[$i]}"
	done

	while true; do
		read -r -p "Enter choice [1-${#options[@]}]: " choice
		if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#options[@]}" ]; then
			SELECTED_ENV_NAME="${options[$((choice - 1))]}"
			echo "[PIO] Selected device env: $SELECTED_ENV_NAME"
			return
		fi
		echo "Invalid selection. Please choose a number between 1 and ${#options[@]}."
	done
}

show_usage() {
	echo "Usage: $0 [--tdeck|-t] [--cardputer|-C] [--pager|-P] [--heltec|-H] [--heltec-vertical|--vertical|-V] [--erase|-E]"
	echo "  --tdeck, -t  Use T-Deck environment ($TDECK_ENV_NAME)"
	echo "  --cardputer, -C  Use Cardputer + Cap LoRa/GPS environment ($CARDPUTER_ENV_NAME)"
	echo "  --pager, -P   Use T-Lora Pager TFT environment ($TLORA_PAGER_ENV_NAME)"
	echo "  --heltec, -H  Use Heltec V4 expansion environment ($HELTEC_ENV_NAME)"
	echo "  --heltec-vertical, --vertical, -V  Use vertical Heltec env ($HELTEC_VERTICAL_ENV_NAME)"
	echo "                If neither is provided, you'll be prompted to choose a device."
	echo "  --erase, -E   Erase flash before clean build/upload"
}

run_pio_target() {
	local target="$1"
	local label="$2"
	echo "[PIO] $label ($SELECTED_ENV_NAME)..."
	pio run -e "$SELECTED_ENV_NAME" -t "$target"
}

if ! command -v pio >/dev/null 2>&1; then
	echo "PlatformIO CLI not found: install it or run from an environment that provides 'pio'."
	exit 1
fi

for arg in "$@"; do
	case "$arg" in
		--tdeck|-t)
			select_env_or_exit "$TDECK_ENV_NAME" "Environment '$TDECK_ENV_NAME' not found in platformio.ini"
			;;
		--erase|-E)
			SHOULD_ERASE_FIRST=true
			;;
		--cardputer|-C)
			select_env_or_exit "$CARDPUTER_ENV_NAME" "Environment '$CARDPUTER_ENV_NAME' not found in platformio.ini"
			;;
		--pager|-P)
			select_env_or_exit "$TLORA_PAGER_ENV_NAME" "Environment '$TLORA_PAGER_ENV_NAME' not found in platformio.ini"
			;;
		--heltec|-H)
			select_env_or_exit "$HELTEC_ENV_NAME" "Environment '$HELTEC_ENV_NAME' not found in platformio.ini"
			;;
		--heltec-vertical|--vertical|-V)
			select_env_or_exit "$HELTEC_VERTICAL_ENV_NAME" "Environment '$HELTEC_VERTICAL_ENV_NAME' not found in platformio.ini"
			;;
		--help|-h)
			show_usage
			exit 0
			;;
		*)
			echo "Unknown argument: $arg"
			show_usage
			exit 1
			;;
	esac
done

if [ "$ENV_SELECTED_EXPLICITLY" = false ]; then
	prompt_for_device
fi

if [ "$SHOULD_ERASE_FIRST" = true ]; then
	run_pio_target "erase" "Erasing device flash"
fi

run_pio_target "fullclean" "Full clean"
run_pio_target "upload" "Upload"
run_pio_target "monitor" "Monitor"
