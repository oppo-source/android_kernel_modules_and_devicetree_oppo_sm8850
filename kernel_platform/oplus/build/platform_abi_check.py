#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import subprocess

def main():
    """Check and execute abi_gki_oki_check.py file"""
    # File list, sorted by priority
    target_files = [
        "kernel_platform/oplus/tools/abi_gki_oki_check.py",
        "kernel/oplus/tools/abi_gki_oki_check.py",
    ]


    # Traverse file list, find the first existing file and execute
    for target_file in target_files:
        if os.path.exists(target_file) and os.path.isfile(target_file):
            print("Found file: {}".format(target_file))
            print("Starting to execute abi_gki_oki_check.py...")

            try:
                # Build execution command
                command = [
                    "python3",
                    target_file,
                    #"--exit_on_error", "false"
                ]

                result = subprocess.run(command, check=False)

                # Return the status code of execution result
                sys.exit(result.returncode)
            except Exception as e:
                print("Error executing file: {}".format(e), file=sys.stderr)
                sys.exit(1)

    # If all files do not exist
    print("No target file found")
    print("File list searched:")
    for file_path in target_files:
        print("  - {}".format(file_path))
    sys.exit(0)

if __name__ == "__main__":
    main()

