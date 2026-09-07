#!/usr/bin/env bash
# Copyright (C) 2026 Zach Podbielniak
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail

usage () {
	printf '%s\n' \
		'Usage: bash tests/run-backend-tests.sh [OPTIONS]' \
		'Run prebuilt GST tests on private Xvfb/Weston or headless gowl servers.' \
		'  --backend NAME     all (default), x11, wayland, or lrg' \
		'  --binary PATH      Visual test executable (default build/debug/test-backend-visual)' \
		'  --lrg-binary PATH  Optional prebuilt test-lrg-backend executable' \
		'  --require          Missing binaries/backends/servers fail instead of skipping' \
		'  --keep-logs        Retain server and test logs even on success' \
		'  -h, --help         Show help and examples' \
		'  --license          Show license' \
		'Examples:' \
		'  bash tests/run-backend-tests.sh --backend x11 --require' \
		'  bash tests/run-backend-tests.sh --binary build/release/test-backend-visual' \
		'  bash tests/run-backend-tests.sh --backend lrg --lrg-binary build/debug/test-lrg-backend --require' \
		'Exit: 0 passed/skipped; 1 failure or required prerequisite missing; 2 usage error.' \
		'--require requires execution of each selected backend, not unsupported clipboard subtests.' \
		'Existing-server startup failures, assertion failures and 60-second timeouts always fail.'
}

backend=all
binary=build/debug/test-backend-visual
lrg_binary=
require=0
keep_logs=0
while (($#))
do
	case "$1" in
		-h|--help) usage; exit 0 ;;
		--license)
			printf '%s\n' 'SPDX-License-Identifier: AGPL-3.0-or-later' \
				'GNU Affero General Public License version 3 or later.' \
				'This program comes with NO WARRANTY. See https://www.gnu.org/licenses/agpl-3.0.html'
			exit 0 ;;
		--require) require=1; shift ;;
		--keep-logs) keep_logs=1; shift ;;
		--backend|--binary|--lrg-binary)
			if (($# < 2)) || [[ -z $2 ]]
			then
				printf 'Missing value for %s\n' "$1" >&2
				exit 2
			fi
			case "$1" in
				--backend) backend=$2 ;;
				--binary) binary=$2 ;;
				--lrg-binary) lrg_binary=$2 ;;
			esac
			shift 2 ;;
		*) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
	esac
done
case "$backend" in
	all|x11|wayland|lrg) ;;
	*) printf 'Unknown backend: %s\n' "$backend" >&2; exit 2 ;;
esac

# Missing optional prerequisites are visible, never reported as a graphical pass.
missing () {
	if ((require))
	then
		printf 'FAIL [%s]: %s\n' "$1" "$2" >&2
		return 1
	fi
	printf 'SKIP [%s]: %s\n' "$1" "$2"
}

# Each subshell owns its server, runtime directory and cleanup trap.
run_backend () (
	local name=$1 executable=$binary capabilities temp server_pid='' display_number=''
	local status=0 ready=0 i compositor_pid='' use_gowl=0 socket
	local -a args=()
	if [[ $name == lrg ]]
	then
		executable=$lrg_binary
	fi
	if [[ -z $executable || ! -x $executable ]]
	then
		missing "$name" "prebuilt executable unavailable: ${executable:-not specified}"
		return $?
	fi
	if [[ $executable != /* ]]
	then
		executable=$PWD/$executable
	fi
	if ! command -v timeout >/dev/null
	then
		missing "$name" 'timeout missing (coreutils)'
		return $?
	fi
	if [[ $name == lrg ]]
	then
		capabilities=$(timeout 10s "$executable" -l) || return 1
		if [[ $capabilities != *'/lrg/render-context/image-upload'* ]]
		then
			missing "$name" 'binary lacks optional LRG graphics test'
			return $?
		fi
		args=(-p /lrg/render-context/image-upload)
	else
		capabilities=$(timeout 10s "$executable" --list-backends) || return 1
		if [[ $'\n'$capabilities$'\n' != *$'\n'"$name"$'\n'* ]]
		then
			missing "$name" 'backend not compiled into executable'
			return $?
		fi
	fi
	if [[ $name == wayland ]]
	then
		if ! command -v weston >/dev/null
		then
			use_gowl=1
		fi
	else
		if ! command -v Xvfb >/dev/null
		then
			use_gowl=1
		fi
	fi
	if ((use_gowl)) && { ! command -v gowl >/dev/null ||
		{ [[ $name != wayland ]] && ! command -v Xwayland >/dev/null; }; }
	then
		missing "$name" 'need Weston/Xvfb or gowl (plus Xwayland for X11/LRG)'
		return $?
	fi
	temp=$(mktemp -d "${TMPDIR:-/tmp}/gst-backend.XXXXXXXX") || return 1
	cleanup () {
		if [[ -n $server_pid ]]
		then
			kill "$server_pid" 2>/dev/null || true
			wait "$server_pid" 2>/dev/null || true
		fi
		if [[ -n $compositor_pid ]]
		then
			kill "$compositor_pid" 2>/dev/null || true
			wait "$compositor_pid" 2>/dev/null || true
		fi
		if ((status != 0 || keep_logs))
		then
			printf 'Logs retained: %s\n' "$temp" >&2
		else
			rm -rf -- "$temp"
		fi
	}
	trap cleanup EXIT
	trap 'status=1; exit 1' HUP INT TERM
	# Assertion failures should not invoke the host's core-dump collector.
	ulimit -c 0
	# Disconnect from all inherited graphical/session endpoints before launch.
	unset DISPLAY WAYLAND_DISPLAY WAYLAND_SOCKET XAUTHORITY DBUS_SESSION_BUS_ADDRESS
	export XDG_RUNTIME_DIR=$temp
	export HOME=$temp XDG_CONFIG_HOME=$temp/config XDG_CACHE_HOME=$temp/cache
	export XDG_DATA_HOME=$temp/data XDG_STATE_HOME=$temp/state
	export DBUS_SESSION_BUS_ADDRESS=unix:path=$temp/no-session-bus
	export GST_TEST_BACKEND_VISUAL=1 GST_TEST_NESTED_DISPLAY=1 GST_TEST_BACKEND=$name
	export LIBGL_ALWAYS_SOFTWARE=1
	# No YAML means an empty module registry; no C config means no compiled hooks.
	# The systemd opt-out is essential even with a private runtime directory.
	if ((use_gowl))
	then
		export GOWL_DISABLE_SYSTEMD=1 WLR_BACKENDS=headless WLR_RENDERER=pixman
		export WLR_HEADLESS_OUTPUTS=1 WLR_LIBINPUT_NO_DEVICES=1
		printf 'SERVER [%s]: gowl headless/pixman, configs and systemd disabled\n' "$name"
		gowl --no-c-config --no-yaml-config >"$temp/gowl.log" 2>&1 &
		compositor_pid=$!
		for ((i = 0; i < 100; i++))
		do
			kill -0 "$compositor_pid" 2>/dev/null || break
			for socket in "$temp"/wayland-*
			do
				if [[ -S $socket ]]
				then
					export WAYLAND_DISPLAY=${socket##*/}
					ready=1; break
				fi
			done
			((ready)) && break
			sleep 0.1
		done
		if ((ready == 0))
		then
			printf 'FAIL [%s]: headless gowl did not become ready\n' "$name" >&2
			status=1; return 1
		fi
		if [[ $name == wayland ]]
		then
			server_pid=$compositor_pid
			compositor_pid=''
		else
			# Rootful Xwayland avoids compositor tiling of individual X test windows.
			Xwayland -displayfd 3 -geometry 1280x1024 -shm -nolisten tcp -ac \
				3>"$temp/display" >"$temp/server.log" 2>&1 &
			server_pid=$!
		fi
	elif [[ $name == wayland ]]
	then
		export WAYLAND_DISPLAY=gst-test
		weston --backend=headless-backend.so --renderer=pixman --scale=1 \
			--width=1280 --height=1024 --socket="$WAYLAND_DISPLAY" --idle-time=0 \
			--no-config >"$temp/server.log" 2>&1 &
		server_pid=$!
	else
		# -displayfd atomically chooses a free display, unlike probing :99.
		Xvfb -displayfd 3 -screen 0 1280x1024x24 -nolisten tcp -ac \
			3>"$temp/display" >"$temp/server.log" 2>&1 &
		server_pid=$!
	fi
	ready=0
	for ((i = 0; i < 100; i++))
	do
		if ! kill -0 "$server_pid" 2>/dev/null
		then
			break
		fi
		if [[ $name == wayland ]]
		then
			if [[ -S $temp/$WAYLAND_DISPLAY ]]
			then
				ready=1; break
			fi
		elif [[ -s $temp/display ]] && IFS= read -r display_number <"$temp/display"
		then
			if [[ $display_number =~ ^[0-9]+$ ]]
			then
				export DISPLAY=:$display_number
				ready=1; break
			fi
		fi
		sleep 0.1
	done
	if ((ready == 0))
	then
		printf 'FAIL [%s]: nested server did not become ready\n' "$name" >&2
		status=1
		return 1
	fi
	if [[ $name == lrg ]]
	then
		export GST_TEST_LRG_GRAPHICS=1
	fi
	printf 'RUN [%s]: %s\n' "$name" "$executable"
	timeout --kill-after=5s 60s "$executable" "${args[@]}" 2>&1 | tee "$temp/test.log" || status=$?
	if ((status != 0))
	then
		printf 'FAIL [%s]: test exit %s\n' "$name" "$status" >&2
		return 1
	fi
	cleanup
	trap - EXIT
	printf 'PASS [%s]: selected backend tests completed (see GTest subtest skips above)\n' "$name"
)

result=0
if [[ $backend == all ]]
then
	for selected in x11 wayland
	do
		run_backend "$selected" || result=1
	done
	if [[ -n $lrg_binary ]]
	then
		run_backend lrg || result=1
	fi
else
	run_backend "$backend" || result=1
fi
exit "$result"
