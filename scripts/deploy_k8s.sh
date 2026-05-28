#!/usr/bin/env bash
exec "$(dirname "$0")/wrappers/sh/deploy_k8s.sh" "$@"
