#!/usr/bin/env bash
exec "$(dirname "$0")/wrappers/sh/operator_kind_smoke.sh" "$@"
