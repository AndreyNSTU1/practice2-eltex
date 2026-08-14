#!/usr/bin/env bash
set -u

CC=${CC:-gcc}
CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -O2"
PROG=./filecopy

$CC $CFLAGS filecopy.c -o "$PROG" || exit 1

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

printf 'first file\nline 2\n' > "$TMP_DIR/one.txt"
printf 'file with spaces\n' > "$TMP_DIR/file two.txt"
: > "$TMP_DIR/empty.txt"
dd if=/dev/urandom of="$TMP_DIR/binary.bin" bs=1 count=10000 status=none

check_copy() {
    src=$1
    if ! cmp -s "$src" "$src.copy"; then
        echo "FAIL: copy differs: $src" >&2
        exit 1
    fi
}

echo '== Test 1: unnamed pipes =='
"$PROG" \
    "$TMP_DIR/one.txt" \
    "$TMP_DIR/file two.txt" \
    "$TMP_DIR/empty.txt" \
    "$TMP_DIR/binary.bin" || exit 1
check_copy "$TMP_DIR/one.txt"
check_copy "$TMP_DIR/file two.txt"
check_copy "$TMP_DIR/empty.txt"
check_copy "$TMP_DIR/binary.bin"

echo '== Test 2: named FIFOs =='
rm -f "$TMP_DIR"/*.copy
"$PROG" -p "$TMP_DIR/myfifo" \
    "$TMP_DIR/one.txt" \
    "$TMP_DIR/binary.bin" || exit 1
check_copy "$TMP_DIR/one.txt"
check_copy "$TMP_DIR/binary.bin"

if [[ -e "$TMP_DIR/myfifo" || -e "$TMP_DIR/myfifo.ack" ]]; then
    echo 'FAIL: FIFO files were not removed' >&2
    exit 1
fi

echo '== Test 3: nonexistent file; other files must still be copied =='
rm -f "$TMP_DIR/one.txt.copy"
set +e
"$PROG" "$TMP_DIR/missing.txt" "$TMP_DIR/one.txt"
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
    echo 'FAIL: expected non-zero status because one input file is missing' >&2
    exit 1
fi
check_copy "$TMP_DIR/one.txt"

echo '== Test 4: invalid -p option =='
set +e
"$PROG" -p >/dev/null 2>&1
rc=$?
set -e
if [[ $rc -eq 0 ]]; then
    echo 'FAIL: "-p" without a FIFO name must fail' >&2
    exit 1
fi

echo 'ALL TESTS PASSED'
