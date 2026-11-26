#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "${1:-}" = "--check" ]; then
    find include src tests -type f \( -name '*.hpp' -o -name '*.cpp' -o -name '*.mm' \) -exec clang-format --style=file --dry-run --Werror {} +
else
    find include src tests -type f \( -name '*.hpp' -o -name '*.cpp' -o -name '*.mm' \) -exec clang-format --style=file -i {} +
fi
