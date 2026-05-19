#!/usr/bin/env python3
#
#  Copyright (C) 2022 The Android Open Source Project
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

"""The script to invoke content_uploader binary to upload artifacts to CAS."""
import glob
import logging
import os
import subprocess
import sys

CONTENT_PLOADER_PREBUILT_PATH = 'tools/tradefederation/prebuilts/'
CONTENT_UPLOADER_BIN = 'content_uploader'
CONTENT_UPLOADER_TIMEOUT_SECS = 1800 # 30 minutes
LOG_PATH = 'logs/cas_uploader.log'

def _get_prebuilt_uploader() -> str:
    uploader = glob.glob(CONTENT_PLOADER_PREBUILT_PATH + '**/' + CONTENT_UPLOADER_BIN,
                         recursive=True)
    if not uploader:
        logging.error('%s not found in Tradefed prebuilt', CONTENT_UPLOADER_BIN)
        raise ValueError(f'Error: {CONTENT_UPLOADER_BIN} not found in Tradefed prebuilt')
    return uploader[0]

def _get_env_var(key: str, default=None, check=False):
    value = os.environ.get(key, default)
    if check and not value:
        logging.error('the environment variable %s is not set', key)
        raise ValueError(f'Error: the environment variable {key} is not set')
    return value

def _truncate_file(file_path: str):
    try:
        with open(file_path, 'w'):
            pass
    except Exception as e:
        print(f"Failed to trunacte file: {e}")


def _setup_logging() -> str:
    dist_dir = _get_env_var('DIST_DIR', check=True)
    log_file = os.path.join(dist_dir, LOG_PATH)
    _truncate_file(log_file)
    logging.basicConfig(
        level=logging.DEBUG,
        format='%(asctime)s %(levelname)s %(message)s',
        filename=log_file,
    )
    return log_file

def main():
    """Call content_uploader and pass all arguments as-is."""
    log_file = _setup_logging()

    uploader = _get_prebuilt_uploader()
    arguments = sys.argv[1:]

    try:
        result = subprocess.run([uploader] + arguments, capture_output=True, text=True,
                                check=True, timeout=CONTENT_UPLOADER_TIMEOUT_SECS)
        print(result.stdout)
    except FileNotFoundError:
        print(f'content_uploader.py will export logs to: {log_file}')
        logging.error('Uploader not found: %s', log_file)
    except subprocess.CalledProcessError as e:
        print(f'content_uploader.py will export logs to: {log_file}')
        logging.error(f"Error: Command failed with exit code {e.returncode}\nCommand: {e.cmd}\nStdout:\n{e.stdout}\nStderr:\n{e.stderr}")
    except ValueError as e:
        # Capture and log all errors - not to fail the build.
        print(f'content_uploader.py will export logs to: {log_file}')
        logging.exception("Unexpected error: %s", e)

if __name__ == '__main__':
    main()
