#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p logs
exec >> logs/colima-build-launch.log 2>&1
printf '%s Waiting for the colima Docker context (up to 30 minutes).\n' "$(date -u +%FT%TZ)"
deadline=$((SECONDS + 1800))
while (( SECONDS < deadline )); do
  if docker context inspect colima >/dev/null 2>&1 && \
     docker --context colima info >/dev/null 2>&1; then
    printf '%s Colima is ready; starting the Docker build.\n' "$(date -u +%FT%TZ)"
    export DOCKER_CONTEXT=colima
    # A pre-existing DOCKER_HOST must not redirect the selected context.
    unset DOCKER_HOST
    if bash scripts/build.sh; then
      printf '%s Build and gate tests succeeded. Hardware remains untested.\n' "$(date -u +%FT%TZ)"
      exit 0
    else
      rc=$?
      printf '%s Build failed with exit %s; inspect docker-build.log and compile.log.\n' "$(date -u +%FT%TZ)" "$rc"
      exit "$rc"
    fi
  fi
  sleep 5
done
printf '%s Timed out waiting for Colima. No build was started.\n' "$(date -u +%FT%TZ)"
exit 1
