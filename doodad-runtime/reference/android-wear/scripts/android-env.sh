#!/usr/bin/env bash

set -euo pipefail

if [[ -z "${ANDROID_HOME:-}" ]]; then
    export ANDROID_HOME="${HOME}/Library/Android/sdk"
fi

if [[ -z "${JAVA_HOME:-}" ]]; then
    studio_jdk="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
    if [[ -d "${studio_jdk}" ]]; then
        export JAVA_HOME="${studio_jdk}"
    fi
fi

if [[ ! -x "${JAVA_HOME:-}/bin/java" ]]; then
    echo "A JDK 17 or newer is required; Android Studio's bundled JDK is supported." >&2
    return 1 2>/dev/null || exit 1
fi

if [[ ! -d "${ANDROID_HOME}" ]]; then
    echo "Android SDK not found at ${ANDROID_HOME}" >&2
    return 1 2>/dev/null || exit 1
fi

export PATH="${ANDROID_HOME}/platform-tools:${ANDROID_HOME}/emulator:${PATH}"
