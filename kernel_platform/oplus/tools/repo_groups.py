import argparse
import subprocess
import sys
from typing import List, Optional

# 默认配置参数
DEFAULT_CONFIG = {
    'repo_url': "ssh://gerrit_url:29418/oplus/platform/manifest",
    'branch': "b/master",
    #'manifest': "VND/vnd_mt6993.xml",
    'manifest': "VND/vnd_sm8850.xml",
    'repo_branch': "update",
    'reference_dir': "/work/oplus_mirror",
    'sync_jobs': 4,
    #'local_groups': "mtk-kernel-module,open-kernel-codebase"
    'local_groups': "open-kernel-codebase,qcom-kernel-module"
}

def parse_args():
    """解析命令行参数"""
    parser = argparse.ArgumentParser(
        description='初始化并同步代码仓库',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )

    parser.add_argument('--repo-url',
                       default=DEFAULT_CONFIG['repo_url'],
                       help='仓库URL')
    parser.add_argument('-b', '--branch',
                       default=DEFAULT_CONFIG['branch'],
                       help='分支名称')
    parser.add_argument('-m', '--manifest',
                       default=DEFAULT_CONFIG['manifest'],
                       help='manifest文件')
    parser.add_argument('--repo-branch',
                       default=DEFAULT_CONFIG['repo_branch'],
                       help='repo工具分支')
    parser.add_argument('--reference-dir',
                       default=DEFAULT_CONFIG['reference_dir'],
                       help='参考目录路径')
    parser.add_argument('-j', '--sync-jobs',
                       type=int,
                       default=DEFAULT_CONFIG['sync_jobs'],
                       help='同步任务数')
    parser.add_argument('-g', '--local-groups',
                       default=DEFAULT_CONFIG['local_groups'],
                       help='本地组名')

    return parser.parse_args()

def run_command(command_args: List[str]) -> bool:
    """
    打印并执行命令

    Args:
        command_args: 命令参数列表

    Returns:
        bool: 命令是否执行成功
    """
    # 先打印将要执行的命令
    print("[执行命令]", " ".join(command_args))

    # 然后执行命令
    try:
        result = subprocess.run(command_args, check=True)
        return result.returncode == 0
    except subprocess.CalledProcessError as e:
        print(f"[错误] 命令执行失败，状态码: {e.returncode}")
        return False
    except Exception as e:
        print(f"[错误] 执行命令时发生异常: {str(e)}")
        return False

def init_repo(args: argparse.Namespace) -> None:
    """
    初始化repo仓库

    Args:
        args: 命令行参数对象
    """
    print("== 开始初始化repo ==")

    command = [
        "repo", "init",
        "-u", args.repo_url,
        "-b", args.branch,
        "-m", args.manifest,
        f"--repo-branch={args.repo_branch}",
        f"--reference={args.reference_dir}",
        "--no-repo-verify",
        "-g", args.local_groups
    ]

    if not run_command(command):
        print("[错误] repo初始化失败", file=sys.stderr)
        sys.exit(1)

    print("[成功] repo初始化完成")


def sync_repo(args: argparse.Namespace) -> None:
    """
    同步代码

    Args:
        args: 命令行参数对象
    """
    print("== 开始同步代码 ==")

    command = [
        "repo", "sync",
        "-fcq",
        f"-j{args.sync_jobs}",
        "--no-tags",
        "--prune",
        "--no-repo-verify"
    ]

    if not run_command(command):
        print("[错误] 代码同步失败", file=sys.stderr)
        sys.exit(1)

    print("[成功] 代码同步完成")

def main():
    """主函数"""
    args = parse_args()
    init_repo(args)
    sync_repo(args)

if __name__ == "__main__":
    main()