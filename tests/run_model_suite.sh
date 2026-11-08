#!/bin/sh
# Runs the `model` doctest suite. Without REVISA_TEST_MODEL it warns and exits 77, which ctest
# reports as Skipped (SKIP_RETURN_CODE), so a missing model is never a silent pass.
#   usage: run_model_suite.sh <path to revisa_tests>
set -eu
if [ -z "${REVISA_TEST_MODEL:-}" ]; then
    echo "AVISO: REVISA_TEST_MODEL no está definido: se salta la suite model." >&2
    echo "       REVISA_TEST_MODEL=/ruta/al/modelo.gguf ctest --test-dir build -L model" >&2
    exit 77
fi
if [ ! -f "$REVISA_TEST_MODEL" ]; then
    echo "ERROR: REVISA_TEST_MODEL=$REVISA_TEST_MODEL no existe." >&2
    exit 1
fi
exec "$1" -ts=model
