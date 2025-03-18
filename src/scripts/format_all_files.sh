#!/bin/bash

# Find the VS Code C++ extension path
VSCODE_EXT_DIR="$HOME/.vscode/extensions"
CLANG_FORMAT_PATH=$(find "$VSCODE_EXT_DIR" -type f -path "*/LLVM/bin/clang-format" | sort -V | tail -n 1)

# Check if clang-format was found
if [[ -z "$CLANG_FORMAT_PATH" ]]; then
    echo -e "\e[31mclang-format not found in VS Code extensions. Please install the C/C++ extension.\e[0m"
    exit 1
fi

echo -e "\e[36mUsing clang-format: $CLANG_FORMAT_PATH\e[0m"

# Process each file and check if changes were made
find src/vario src/variants -type f \( -name "*.cpp" -o -name "*.h" \) | while read -r file; do
    original_hash=$(sha256sum "$file" | awk '{print $1}')
    "$CLANG_FORMAT_PATH" -i "$file"
    new_hash=$(sha256sum "$file" | awk '{print $1}')

    if [[ "$original_hash" != "$new_hash" ]]; then
        echo -e "\e[32mFormatted: $file\e[0m"
    fi
done

echo -e "\e[36mFormatting complete.\e[0m"
