#!/usr/bin/env bash
exec "$(dirname "$0")/wrappers/sh/gen_certs.sh" "$@"
