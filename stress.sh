#!/bin/bash

# Check if the correct number of arguments is provided
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <binary_path> <number_of_times>"
    exit 1
fi

BINARY_PATH=$1
NUMBER_OF_TIMES=$2

# Check if the binary file exists and is executable
if [ ! -x "$BINARY_PATH" ]; then
    echo "Error: $BINARY_PATH is not an executable file."
    exit 1
fi

# Loop to execute the binary N times
for ((i=1; i<=NUMBER_OF_TIMES; i++)); do
    # Execute the binary
    $BINARY_PATH
    EXIT_STATUS=$?

    # Check if the exit status is -1
    if [ "$EXIT_STATUS" -eq 255 ]; then
        # Print in red using ANSI escape codes
        echo -e "\e[31mExecution $i: Exited with status -1\e[0m"
    else
        echo "Execution $i: Exited with status $EXIT_STATUS"
    fi
done
