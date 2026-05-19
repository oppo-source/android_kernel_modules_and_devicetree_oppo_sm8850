#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Android System File Manager Tool
Batch management tool for Android system files (delete, rename, restore)
Requires root permission and writable system partition
"""

import os
import sys
import csv
import time
import argparse
import subprocess
import re
from datetime import datetime
from pathlib import Path
from typing import List, Dict, Tuple, Optional

# Protected files list (critical system files that should not be modified)
PROTECTED_FILES = [
    '/system/bin/sh',
    '/system/bin/su',
    '/system/bin/adbd',
    '/system/bin/init',
]

# Log levels
LOG_LEVELS = {
    'INFO': 'INFO',
    'WARNING': 'WARNING',
    'ERROR': 'ERROR',
    'SUCCESS': 'SUCCESS'
}


class Logger:
    """Logger class for recording operations"""

    def __init__(self, action: str, log_dir: str = 'logs'):
        self.action = action
        self.log_dir = Path(log_dir)
        self.log_dir.mkdir(exist_ok=True)

        # Create log file with timestamp
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        self.log_file = self.log_dir / f"{action}_{timestamp}.log"

        # Statistics
        self.stats = {
            'total': 0,
            'success': 0,
            'failed': 0,
            'skipped': 0
        }

    def log(self, level: str, message: str = '', file_path: str = '',
            result: str = '', error: str = ''):
        """Log a message with timestamp and details"""
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        log_entry = f"[{timestamp}] [{level}] [{self.action.upper()}]"

        if file_path:
            log_entry += f" {file_path}"
        if result:
            log_entry += f": {result}"
        if error:
            log_entry += f" - {error}"
        if message:
            log_entry += f" - {message}"

        # Write to log file
        with open(self.log_file, 'a', encoding='utf-8') as f:
            f.write(log_entry + '\n')

        # Print to console
        print(log_entry)

    def get_summary(self) -> str:
        """Get operation summary"""
        summary = f"""
[SUMMARY]
Total files: {self.stats['total']}
Success: {self.stats['success']}
Failed: {self.stats['failed']}
Skipped: {self.stats['skipped']}
Detailed log: {self.log_file}
"""
        return summary


class ADBManager:
    """ADB management class for device communication"""

    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.max_retry = 3

    def run_command(self, command: str, shell: bool = False,
                    timeout: int = 30) -> Tuple[bool, str, str]:
        """
        Execute a command and return result

        Args:
            command: Command to execute
            shell: Whether to run in shell mode
            timeout: Command timeout in seconds

        Returns:
            Tuple of (success, stdout, stderr)
        """
        try:
            if shell:
                result = subprocess.run(
                    command,
                    shell=True,
                    capture_output=True,
                    text=True,
                    timeout=timeout,
                    encoding='utf-8',
                    errors='ignore'
                )
            else:
                result = subprocess.run(
                    command.split(),
                    capture_output=True,
                    text=True,
                    timeout=timeout,
                    encoding='utf-8',
                    errors='ignore'
                )

            success = result.returncode == 0
            stdout = result.stdout.strip()
            stderr = result.stderr.strip()

            if self.verbose:
                print(f"[DEBUG] Command: {command}")
                print(f"[DEBUG] Return code: {result.returncode}")
                if stdout:
                    print(f"[DEBUG] Output: {stdout}")
                if stderr:
                    print(f"[DEBUG] Error: {stderr}")

            return success, stdout, stderr

        except subprocess.TimeoutExpired:
            return False, '', 'Command execution timeout'
        except Exception as e:
            return False, '', str(e)

    def check_adb_connection(self) -> bool:
        """Check if ADB device is connected"""
        success, stdout, _ = self.run_command('adb devices')
        if not success:
            return False

        # Check if any device is connected
        lines = stdout.split('\n')
        device_count = 0
        for line in lines[1:]:  # Skip first line "List of devices attached"
            if line.strip() and 'device' in line:
                device_count += 1

        return device_count > 0

    def wait_for_device(self, timeout: int = 120) -> bool:
        """Wait for device to connect"""
        print(f"[INFO] Waiting for device connection (max {timeout} seconds)...")
        start_time = time.time()

        while time.time() - start_time < timeout:
            if self.check_adb_connection():
                print("[INFO] Device connected")
                return True
            time.sleep(2)

        return False

    def check_root_permission(self) -> bool:
        """Check if device has root permission"""
        # Method 1: Check id command output
        success, stdout, _ = self.run_command('adb shell id')
        if success and 'uid=0' in stdout:
            return True

        # Method 2: Try su command
        success, stdout, _ = self.run_command('adb shell su -c "id"')
        if success and 'uid=0' in stdout:
            return True

        return False

    def enable_adb_root(self) -> bool:
        """Enable ADB root mode"""
        print("[INFO] Checking ADB root status...")
        success, stdout, _ = self.run_command('adb root')

        if not success:
            return False

        # Check output
        if 'already running as root' in stdout.lower() or \
           'restarting adbd as root' in stdout.lower():
            print("[INFO] ADB is already running as root")
            time.sleep(2)  # Wait for device response
            return True

        # If root was executed, wait for device response
        if 'restarting' in stdout.lower():
            print("[INFO] Restarting ADB as root mode...")
            if not self.wait_for_device(timeout=10):
                return False

        return self.check_root_permission()

    def check_remount_status(self) -> bool:
        """Check if system partition is mounted as read-write"""
        success, stdout, _ = self.run_command('adb shell mount | grep /system')
        if not success:
            return False

        # Check if it's rw mode
        return 'rw,' in stdout or 'rw ' in stdout

    def remount_system(self) -> bool:
        """Remount system partition as writable"""
        print("[INFO] Checking system partition mount status...")

        if self.check_remount_status():
            print("[INFO] System partition is already writable")
            return True

        print("[INFO] Remounting system partition...")
        success, stdout, stderr = self.run_command('adb remount')

        if not success:
            print(f"[ERROR] Remount failed: {stderr}")
            return False

        # Verify remount success
        time.sleep(1)
        if self.check_remount_status():
            print("[INFO] System partition successfully mounted as writable")
            return True
        else:
            print("[ERROR] Remount verification failed, system partition still read-only")
            return False

    def check_verity_status(self) -> Tuple[bool, bool]:
        """
        Check Verity status

        Returns:
            Tuple of (is_disabled, need_reboot)
        """
        # Check veritymode
        success, stdout, _ = self.run_command('adb shell getprop ro.boot.veritymode')
        if success:
            veritymode = stdout.strip().lower()
            if veritymode in ['disabled', '']:
                return True, False

        # Check partition.system.verified
        success, stdout, _ = self.run_command('adb shell getprop partition.system.verified')
        if success:
            verified = stdout.strip().lower()
            if verified in ['0', 'false', '']:
                return True, False

        return False, True

    def disable_verity(self) -> bool:
        """Disable Verity (may require device reboot)"""
        print("[INFO] Checking Verity status...")

        is_disabled, need_reboot = self.check_verity_status()
        if is_disabled:
            print("[INFO] Verity is already disabled")
            return True

        print("[INFO] Verity is not disabled, disabling...")
        success, stdout, stderr = self.run_command('adb disable-verity')

        if not success:
            print(f"[ERROR] Failed to disable Verity: {stderr}")
            return False

        print("[INFO] Verity disabled, device will reboot in 5 seconds")
        time.sleep(5)

        # Reboot device
        print("[INFO] Rebooting device...")
        self.run_command('adb reboot')

        # Wait for device to reboot
        if not self.wait_for_device(timeout=120):
            print("[ERROR] Device connection timeout after reboot")
            return False

        print("[INFO] Device rebooted, rechecking environment...")
        return True

    def file_exists(self, file_path: str) -> bool:
        """Check if file exists on device"""
        # Escape special characters
        escaped_path = file_path.replace('"', '\\"')
        command = f'adb shell "[ -f \\"{escaped_path}\\" ] && echo exists"'
        success, stdout, _ = self.run_command(command, shell=True)
        return success and 'exists' in stdout

    def delete_file(self, file_path: str) -> bool:
        """Delete file on device"""
        escaped_path = file_path.replace('"', '\\"')
        command = f'adb shell "rm -f \\"{escaped_path}\\""'
        success, stdout, stderr = self.run_command(command, shell=True)

        if success:
            # Verify file is deleted
            if not self.file_exists(file_path):
                return True

        return False

    def rename_file(self, file_path: str, backup_path: str) -> bool:
        """Rename file on device"""
        escaped_path = file_path.replace('"', '\\"')
        escaped_backup = backup_path.replace('"', '\\"')
        command = f'adb shell "mv \\"{escaped_path}\\" \\"{escaped_backup}\\""'
        success, stdout, stderr = self.run_command(command, shell=True)

        if success:
            # Verify file is renamed
            if not self.file_exists(file_path) and self.file_exists(backup_path):
                return True

        return False

    def restore_file(self, backup_path: str, file_path: str) -> bool:
        """Restore file from backup on device"""
        escaped_path = file_path.replace('"', '\\"')
        escaped_backup = backup_path.replace('"', '\\"')
        command = f'adb shell "mv \\"{escaped_backup}\\" \\"{escaped_path}\\""'
        success, stdout, stderr = self.run_command(command, shell=True)

        if success:
            # Verify file is restored
            if self.file_exists(file_path) and not self.file_exists(backup_path):
                return True

        return False


class FileManager:
    """Main file manager class"""

    def __init__(self, config_path: str, action: str, dry_run: bool = False,
                 verbose: bool = False, confirm: bool = False):
        self.config_path = config_path
        self.action = action
        self.dry_run = dry_run
        self.verbose = verbose
        self.confirm = confirm
        self.adb = ADBManager(verbose=verbose)
        self.logger = Logger(action, log_dir='logs')

    def validate_path(self, file_path: str) -> Tuple[bool, str]:
        """
        Validate file path

        Args:
            file_path: Path to validate

        Returns:
            Tuple of (is_valid, error_message)
        """
        # Check path format
        if not file_path.startswith('/'):
            return False, "Path must start with '/'"

        # Check for dangerous characters
        dangerous_chars = ['..', '*', '?', ';', '&', '|', '`', '$', '(', ')', '<', '>']
        for char in dangerous_chars:
            if char in file_path:
                return False, f"Path contains dangerous character: {char}"

        # Check if file is in protected list
        if file_path in PROTECTED_FILES:
            return False, "File is in protected list, operation not allowed"

        return True, ""

    def detect_encoding(self, file_path: Path) -> str:
        """
        Detect file encoding by trying multiple encodings

        Args:
            file_path: Path to the file

        Returns:
            Detected encoding name
        """
        # Check for UTF-8 BOM first
        try:
            with open(file_path, 'rb') as f:
                first_bytes = f.read(3)
                if first_bytes == b'\xef\xbb\xbf':
                    return 'utf-8-sig'
        except Exception:
            pass

        # Try common encodings in order of likelihood
        encodings = [
            'utf-8-sig',  # UTF-8 with BOM (Excel compatible)
            'utf-8',      # UTF-8 without BOM
            'gbk',        # Chinese Windows default
            'gb2312',     # Chinese simplified
            'cp936',      # Windows Chinese code page
            'latin1',     # Fallback
        ]

        for encoding in encodings:
            try:
                with open(file_path, 'r', encoding=encoding) as f:
                    # Try to read a small portion to test
                    f.read(1024)
                    f.seek(0)  # Reset to beginning
                return encoding
            except (UnicodeDecodeError, UnicodeError):
                continue
            except Exception:
                continue

        # Default fallback
        return 'utf-8-sig'

    def load_config(self) -> List[Dict[str, str]]:
        """
        Load configuration file

        Returns:
            List of file dictionaries with file_path, description, backup_path
        """
        config_file = Path(self.config_path)

        if not config_file.exists():
            print(f"[ERROR] Configuration file not found: {self.config_path}")
            sys.exit(1)

        # Detect file encoding
        detected_encoding = self.detect_encoding(config_file)
        if self.verbose:
            print(f"[DEBUG] Detected encoding: {detected_encoding}")

        files = []
        try:
            # Try to open with detected encoding
            with open(config_file, 'r', encoding=detected_encoding) as f:
                reader = csv.DictReader(f)

                # Check required column
                if 'file_path' not in reader.fieldnames:
                    print("[ERROR] CSV file missing required column: file_path")
                    sys.exit(1)

                for row_num, row in enumerate(reader, start=2):  # Start from row 2 (row 1 is header)
                    file_path = row.get('file_path', '').strip()

                    # Skip empty lines and comment lines
                    if not file_path or file_path.startswith('#'):
                        continue

                    # Auto-fix path: add leading '/' if missing
                    if not file_path.startswith('/'):
                        file_path = '/' + file_path
                        if self.verbose:
                            print(f"[INFO] Row {row_num} path auto-fixed: {file_path}")

                    # Validate path
                    is_valid, error_msg = self.validate_path(file_path)
                    if not is_valid:
                        print(f"[WARNING] Row {row_num} invalid path: {file_path} - {error_msg}")
                        continue

                    files.append({
                        'file_path': file_path,
                        'description': row.get('description', '').strip(),
                        'backup_path': row.get('backup_path', '').strip() or f"{file_path}_bak"
                    })

        except UnicodeDecodeError as e:
            print(f"[ERROR] Failed to decode configuration file with encoding '{detected_encoding}'")
            print(f"[ERROR] Error details: {e}")
            print("[INFO] Please ensure the CSV file is saved in UTF-8 or GBK encoding")
            print("[INFO] If using Excel, save as 'CSV UTF-8 (Comma delimited) (*.csv)'")
            sys.exit(1)
        except csv.Error as e:
            print(f"[ERROR] CSV file format error: {e}")
            sys.exit(1)
        except Exception as e:
            print(f"[ERROR] Failed to read configuration file: {e}")
            print(f"[INFO] Detected encoding: {detected_encoding}")
            sys.exit(1)

        return files

    def check_environment(self) -> bool:
        """
        Check and prepare environment

        Returns:
            True if environment is ready
        """
        print("[INFO] Starting environment check...")

        # 1. Check ADB connection
        if not self.adb.check_adb_connection():
            print("[ERROR] No ADB device detected")
            print("[INFO] Please ensure device is connected and USB debugging is enabled")
            sys.exit(1)

        # 2. Check and enable ADB root
        if not self.adb.enable_adb_root():
            print("[ERROR] Failed to enable ADB root mode")
            sys.exit(2)

        # 3. Check root permission
        if not self.adb.check_root_permission():
            print("[ERROR] Device does not have root permission, please root the device first")
            sys.exit(1)

        # 4. Check and disable Verity (may require reboot)
        verity_disabled, need_reboot = self.adb.check_verity_status()
        if not verity_disabled:
            if not self.adb.disable_verity():
                print("[ERROR] Failed to disable Verity")
                sys.exit(3)
            # Recheck after reboot
            if not self.adb.enable_adb_root():
                print("[ERROR] Failed to enable ADB root mode after reboot")
                sys.exit(2)
            if not self.adb.check_root_permission():
                print("[ERROR] Device does not have root permission after reboot")
                sys.exit(1)

        # 5. Check and execute remount
        if not self.adb.remount_system():
            print("[ERROR] Failed to mount system partition as writable")
            sys.exit(2)

        print("[INFO] Environment check completed")
        return True

    def execute_delete(self, file_path: str) -> Tuple[bool, str]:
        """Execute delete operation"""
        # Check if file exists
        if not self.adb.file_exists(file_path):
            return False, "File does not exist"

        if self.dry_run:
            return True, "Dry-run mode, file not actually deleted"

        # Execute delete
        if self.adb.delete_file(file_path):
            return True, "Delete successful"
        else:
            return False, "Delete failed"

    def execute_rename(self, file_path: str, backup_path: str) -> Tuple[bool, str]:
        """Execute rename operation"""
        # Check if original file exists
        if not self.adb.file_exists(file_path):
            return False, "Original file does not exist"

        # Check if backup file already exists
        if self.adb.file_exists(backup_path):
            return False, f"Backup file already exists: {backup_path}"

        if self.dry_run:
            return True, "Dry-run mode, file not actually renamed"

        # Execute rename
        if self.adb.rename_file(file_path, backup_path):
            return True, "Rename successful"
        else:
            return False, "Rename failed"

    def execute_restore(self, file_path: str, backup_path: str) -> Tuple[bool, str]:
        """Execute restore operation"""
        # Check if backup file exists
        if not self.adb.file_exists(backup_path):
            return False, "Backup file does not exist"

        # Check if original file already exists
        if self.adb.file_exists(file_path):
            # Backup existing file
            old_backup = f"{file_path}.old_{int(time.time())}"
            if not self.dry_run:
                self.adb.rename_file(file_path, old_backup)
            self.logger.log(
                LOG_LEVELS['WARNING'],
                f"Original file exists, backed up as: {old_backup}",
                file_path
            )

        if self.dry_run:
            return True, "Dry-run mode, file not actually restored"

        # Execute restore
        if self.adb.restore_file(backup_path, file_path):
            return True, "Restore successful"
        else:
            return False, "Restore failed"

    def run(self):
        """Run main program"""
        print("[INFO] Android System File Manager Tool")
        print(f"[INFO] Action: {self.action}")
        print(f"[INFO] Configuration file: {self.config_path}")
        if self.dry_run:
            print("[INFO] Dry-run mode: check only, no actual execution")
        print()

        # Environment check
        if not self.check_environment():
            return

        # Load configuration file
        print("[INFO] Loading configuration file...")
        files = self.load_config()

        if not files:
            print("[ERROR] No valid file paths in configuration file")
            sys.exit(1)

        print(f"[INFO] Found {len(files)} files")
        print()

        # Operation confirmation (for delete operation)
        if self.action == 'delete' and self.confirm and not self.dry_run:
            print(f"[WARNING] About to delete {len(files)} files")
            print("File list:")
            for file_info in files:
                print(f"  - {file_info['file_path']}")
            print()
            response = input("Confirm deletion? (yes/no): ").strip().lower()
            if response not in ['yes', 'y']:
                print("[INFO] Operation cancelled")
                sys.exit(0)
            print()

        # Execute operations
        print(f"[INFO] Starting {self.action} operation...")
        print()

        for idx, file_info in enumerate(files, 1):
            file_path = file_info['file_path']
            backup_path = file_info['backup_path']

            self.logger.stats['total'] += 1

            print(f"[{idx}/{len(files)}] Processing: {file_path}")

            try:
                if self.action == 'delete':
                    success, result = self.execute_delete(file_path)
                elif self.action == 'rename':
                    success, result = self.execute_rename(file_path, backup_path)
                elif self.action == 'restore':
                    success, result = self.execute_restore(file_path, backup_path)
                else:
                    success, result = False, f"Unknown action type: {self.action}"

                if success:
                    self.logger.stats['success'] += 1
                    self.logger.log(LOG_LEVELS['SUCCESS'], '', file_path, result)
                else:
                    self.logger.stats['failed'] += 1
                    self.logger.log(LOG_LEVELS['ERROR'], '', file_path, result)

            except Exception as e:
                self.logger.stats['failed'] += 1
                error_msg = f"Operation exception: {str(e)}"
                self.logger.log(LOG_LEVELS['ERROR'], '', file_path, '', error_msg)

            print()

        # Output summary
        print(self.logger.get_summary())


def main():
    """Main function"""
    parser = argparse.ArgumentParser(
        description='Android System File Manager Tool - Batch manage Android system files (delete, rename, restore)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Usage examples:
  # Delete files
  python file_manager.py --action delete --config file_delete_list.csv

  # Rename files (backup)
  python file_manager.py --action rename --config file_delete_list.csv

  # Restore files
  python file_manager.py --action restore --config file_delete_list.csv

  # Dry-run mode (check only, no execution)
  python file_manager.py --action delete --config file_delete_list.csv --dry-run

  # Confirm before delete
  python file_manager.py --action delete --config file_delete_list.csv --confirm
        """
    )

    parser.add_argument(
        '--config', '-c',
        type=str,
        default='file_delete_list.csv',
        help='Configuration file path (default: file_delete_list.csv)'
    )

    parser.add_argument(
        '--action', '-a',
        type=str,
        required=True,
        choices=['delete', 'rename', 'restore'],
        help='Action type: delete / rename / restore'
    )

    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='Dry-run mode, check only without execution'
    )

    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help='Verbose output mode'
    )

    parser.add_argument(
        '--confirm',
        action='store_true',
        help='Require confirmation before delete operation (only for delete action)'
    )

    args = parser.parse_args()

    # Create file manager and run
    manager = FileManager(
        config_path=args.config,
        action=args.action,
        dry_run=args.dry_run,
        verbose=args.verbose,
        confirm=args.confirm
    )

    try:
        manager.run()
    except KeyboardInterrupt:
        print("\n[INFO] Operation interrupted by user")
        sys.exit(130)
    except Exception as e:
        print(f"[ERROR] Program exception: {e}")
        if args.verbose:
            import traceback
            traceback.print_exc()
        sys.exit(1)


if __name__ == '__main__':
    main()
