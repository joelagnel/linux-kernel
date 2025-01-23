#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Clone and build the stress-ng tool, placing the binary in the
# initrd directory. Ensure binary is up-to-date.
#
# Usage: ./bin/mkstress-ng.sh (run from any where).
#
# Copyright (C) Google LLC, 2024
# Author: Joel Fernandes (Google) <joel@joelfernandes.org>

# Get the directory where the script is located
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

REPO_URL="https://github.com/ColinIanKing/stress-ng.git"
SRC_DIR="${SCRIPT_DIR}/../res/stress-ng"
INITRD_DIR="${SCRIPT_DIR}/../initrd"
BIN_NAME="stress-ng"
export COMPILER="${CROSS_COMPILE}gcc"

is_statically_linked() {
    if file "$1" | grep -q "statically linked"; then
        return 0
    else
        return 1
    fi
}

needs_build() {
    if [ ! -f "$INITRD_DIR/$BIN_NAME" ]; then
        return 0
    fi
    if [ "$(find "$SRC_DIR" -newer "$INITRD_DIR/$BIN_NAME")" ]; then
        return 0
    fi
    if ! is_statically_linked "$INITRD_DIR/$BIN_NAME"; then
        return 0
    fi
    return 1
}

if [ ! -d "$INITRD_DIR" ]; then
    echo "Error: INITRD_DIR ($INITRD_DIR) does not exist"
    exit 1
fi

if ! which "$COMPILER" &> /dev/null; then
    echo "Error: Compiler $COMPILER not found."
    exit 1
fi

if [ ! -d "$SRC_DIR" ]; then
    echo "Cloning stress-ng repository..."
    if ! git clone "$REPO_URL" "$SRC_DIR"; then
        echo "Failed to clone stress-ng repository."
        rm -rf "$SRC_DIR"
        exit 1
    fi
else
    echo "Updating stress-ng repository..."
    cd "$SRC_DIR" || exit 1
    git pull || { echo "Failed to update stress-ng repository"; exit 1; }
    cd - > /dev/null || exit 1
fi

# Build stress-ng binary if needed
if needs_build; then
    echo "Building stress-ng binary..."
    cd "$SRC_DIR" || exit 1
    STATIC=1 make -j 8 || { echo "stress-ng build failed"; exit 1; }
    cd - > /dev/null || exit 1

    # Verify the stress-ng binary is static
    if ! is_statically_linked "$SRC_DIR/$BIN_NAME"; then
        echo "Error: The stress-ng binary is not statically linked."
        exit 1
    fi

    echo "Copying stress-ng binary to initrd directory..."
    cp "$SRC_DIR/$BIN_NAME" "$INITRD_DIR" || { echo "Failed to copy stress-ng binary"; exit 1; }
else
    echo "stress-ng binary is up-to-date, no build needed."
fi

echo "stress-ng build process completed successfully."
exit 0