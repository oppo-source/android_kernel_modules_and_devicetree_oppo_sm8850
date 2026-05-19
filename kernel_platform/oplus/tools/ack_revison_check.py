import os
import csv
import re
import sys
import argparse
import subprocess
from pathlib import Path
from typing import Optional, Union
from common import get_boot_img_version

def check_commit_exists_from_HEAD(revision: str,
                                  repo_path: Union[str, Path, None] = None,
                                  max_commits: int = 500,
                                  min_hash_length: int = 7) -> bool:
    """检查Git仓库是否包含指定提交(支持短/长哈希)"""
    rev_len = len(revision)
    if not (min_hash_length <= rev_len <= 40):
        raise ValueError("哈希长度需{}-40字符(当前:{})".format(min_hash_length, rev_len))
    if not all(c in '0123456789abcdef' for c in revision.lower()):
         raise ValueError("哈希必须为十六进制字符")

    try:
        base_cmd = ['git', '-C', str(repo_path)] if repo_path else ['git']
        result = subprocess.run(base_cmd + ['rev-list', '--max-count', str(max_commits), 'HEAD'],
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                universal_newlines=True,
                                check=True)
        return revision in result.stdout.splitlines() if rev_len == 40 else \
               any(commit.startswith(revision) for commit in result.stdout.splitlines())
    except subprocess.CalledProcessError as e:
        err = e.stderr.strip()
        path = repo_path or os.getcwd()
        if "not a git repository" in err.lower():
             raise subprocess.SubprocessError("非Git仓库: {}".format(path)) from e
        raise subprocess.SubprocessError("Git错误: {}".format(err)) from e

def check_commit_exists_from_cherrypick(revision: str,
                                        repo_path: Union[str, Path, None] = None,
                                        min_hash_length: int = 7) -> bool:
    """检查Git仓库是否包含指定提交(支持短/长哈希)"""
    rev_len = len(revision)
    if not (min_hash_length <= rev_len <= 40):
        raise ValueError("哈希长度需{}-40字符(当前:{})".format(min_hash_length, rev_len))
    if not all(c in '0123456789abcdef' for c in revision.lower()):
        raise ValueError("哈希必须为十六进制字符")

    try:
        prev_cmd = ["git", "-C", repo_path, "rev-parse", "FETCH_HEAD~1"]
        prev_parse = subprocess.run(prev_cmd,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE,
                                    universal_newlines=True,
                                    check=True)
        prev_hash = prev_parse.stdout.strip()

        base_cmd = ['git', '-C', str(repo_path)] if repo_path else ['git']
        result = subprocess.run(base_cmd + ['log', '--oneline', '--pretty=format:%H', '{}..FETCH_HEAD'.format(prev_hash)],
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                universal_newlines=True,
                                check=True)
        return revision in result.stdout.splitlines() if rev_len == 40 else \
               any(commit.startswith(revision) for commit in result.stdout.splitlines())
    except subprocess.CalledProcessError as e:
        err = e.stderr.strip()
        path = repo_path or os.getcwd()
        if "not a git repository" in err.lower():
            raise subprocess.SubprocessError("非Git仓库: {}".format(path)) from e
        raise subprocess.SubprocessError("Git错误: {}".format(err)) from e

def is_cherrry_pick(repo_path: str)  -> bool:
    command = ['git', '-C', repo_path, 'reflog']
    try:
        result = subprocess.run(command,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                universal_newlines=True,
                                check=True)
        if 'cherry-pick' in result.stdout:
            return True
        else:
            return False
    except subprocess.CalledProcessError as e:
        err = e.stderr.strip()
        path = repo_path or os.getcwd()
        if "not a git repository" in err.lower():
            raise subprocess.SubprocessError("非Git仓库: {}".format(path)) from e
        raise subprocess.SubprocessError("Git错误: {}".format(err)) from e

def parse_version(version_str):
    """从kernel版本信息中获取commit信息"""
    if not version_str:
        print("Version string is empty.")
        return ""

    match = re.search(r'(\d+)\.(\d+)\.(\d+)', version_str)
    if not match:
        print("Invalid version format.")
        return ""

    current_kernel_version = match.group(0)
    current_version = int(match.group(1))
    current_patchlevel = int(match.group(2))
    current_sublevel = int(match.group(3))

    if "-android" in version_str:
        main_version, android_version = version_str.split("-android")
        android_version_parts = android_version.split("-")

        launchversion = int(android_version_parts[0])
        kmi_generation = int(android_version_parts[1])

        commit = 'UNKNOWN'
        match = re.search(r'-g([^\\-]*)(?:-|$)', android_version)
        if match:
            commit = match.group(1)
    else:
        launchversion = 0
        kmi_generation = 0
        commit = 'UNKNOWN'

    return {
        "CURRENT_KERNEL_VERSION": current_kernel_version,
        "CURRENT_VERSION": current_version,
        "CURRENT_PATCHLEVEL": current_patchlevel,
        "CURRENT_SUBLEVEL": current_sublevel,
        "LAUNCHVERSION": launchversion,
        "KMI_GENERATION": kmi_generation,
        "COMMIT": commit
    }

def get_commit_approvel_from_csv(input_file: str, vendor: str) -> None:
    # Check if input file exists
    if not os.path.exists(input_file):
        print("Input file {} does not exist. Creating an empty output file.".format(input_file))
        return []

    # Read content from CSV file
    column = []
    try:
        with open(input_file, "r", encoding='utf-8') as file:
            csv_reader = csv.reader(file, delimiter=',')
            column = [(row[0], row[1]) for row in csv_reader if row and row[0].strip() and row[1].strip()]
    except Exception as exc:
        print("Error reading input file {}: {}".format(input_file, exc))
        return []

    # Temporarily store the extracted content in a StringIO object
    output_buffer = []
    for (kernel_version, chip_company) in column:
        if chip_company == vendor:
            output_buffer.append(kernel_version)

    return output_buffer

def is_mtk_platform():
    base_path = os.path.dirname(os.path.realpath(__file__))
    mtk_path = os.path.abspath(os.path.join(base_path, '../../../device/mediatek'))
    if os.path.isdir(mtk_path):
        return True
    else:
        return False

def get_resource_path(relative_path):
    base_path = os.path.dirname(os.path.realpath(__file__))
    return os.path.join(base_path, relative_path)

def check_ack_commit_exist(kernel_version: str):
    print("\nStart ack commit check...")
    version_info = parse_version(kernel_version)
    if is_mtk_platform():
        vendor = 'mtk'
        relative_path = "../../kernel-{}.{}".format(version_info['CURRENT_VERSION'], version_info['CURRENT_PATCHLEVEL'])
    else:
        vendor = 'qcom'
        relative_path = "../../common"

    approval_path = os.path.abspath(get_resource_path("../config/ack_commit_tmp_approval.csv"))
    commit_approvel = get_commit_approvel_from_csv(approval_path, vendor)
    if kernel_version in commit_approvel:
        print("Skip check kernel {}...".format(kernel_version))
        return

    base_path = os.path.dirname(os.path.realpath(__file__))
    repo_path = os.path.abspath(get_resource_path(relative_path))

    commit_id = version_info['COMMIT']

    exists = check_commit_exists_from_HEAD(commit_id, repo_path)
    if is_cherrry_pick(repo_path):
        exists = check_commit_exists_from_cherrypick(commit_id, repo_path) or exists

    if not exists:
        print("ERROR 与kernel version {} 对应的common仓源码未合入...".format(kernel_version))
        print("ERROR 请参考文档")
        sys.exit(1)

    print("End ack commit check...\n")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='检查Git提交是否存在')
    parser.add_argument('-v','--version', help='kernel version', default=None)
    parser.add_argument('-b','--boot', help='boot img', default=(get_resource_path('../../../vendor/aosp_gki/aosp_gki/gki/boot.img')
                        if is_mtk_platform() else get_resource_path('../platform/aosp_gki/gki/boot.img')))
    args = parser.parse_args()

    if args.version != None:
        check_ack_commit_exist(args.version)
    elif args.boot != None:
        bootimg = args.boot
        if not bootimg.endswith('boot.img'):
            bootimg = os.path.join(args.boot, 'boot.img')
        unpack_bootimg = get_resource_path('unpack_bootimg')
        lz4 = get_resource_path('lz4')
        unpack = bootimg.replace('boot.img', 'unpack')
        kernel = os.path.join(unpack, 'kernel')
        kernel_version = get_boot_img_version(unpack_bootimg, bootimg, unpack, kernel, lz4)
        check_ack_commit_exist(kernel_version)
    else:
        print("\n请输入kernel版本(-v kernel_version)或者boot.img(-b boot_path)路径!!!\n")
