#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Git分支节点差异比较工具

功能：查找分支1上Initialize提交之后的提交中，哪些在分支2上不存在
用法：python3 compare_branches.py <节点1> <节点2> [仓库路径]

例如：查找 android16-6.12-2025-09_r19 上 Initialize 之后的提交，
      检查哪些在 android16-6.12-2025-12_r4 分支上不存在
"""

import sys
import re
import subprocess
import argparse
from ack_revison_check import get_resource_path

# 检查Python版本
if sys.version_info < (3, 0):
    print("错误: 此脚本需要Python 3.0或更高版本", file=sys.stderr)
    print("当前Python版本: {}".format(sys.version), file=sys.stderr)
    print("请使用 'python3' 命令运行此脚本", file=sys.stderr)
    sys.exit(1)

from pathlib import Path
from typing import Optional, Tuple, List

class BranchComparator:
    """分支比较器"""

    def __init__(self, repo_path: str = "."):
        """
        初始化比较器

        Args:
            repo_path: Git仓库路径，默认为当前目录
        """
        self.repo_path = Path(repo_path).resolve()
        if not self.repo_path.exists():
            raise ValueError(f"仓库路径不存在: {repo_path}")

        # 检查是否为Git仓库
        git_dir = self.repo_path / ".git"
        if not git_dir.exists():
            raise ValueError(f"不是有效的Git仓库: {repo_path}")

    def _run_git(self, *args) -> str:
        """执行git命令并返回输出"""
        cmd = ["git", "-C", str(self.repo_path)] + list(args)
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                check=True,
                encoding='utf-8'
            )
            return result.stdout.strip()
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"Git命令执行失败: {' '.join(cmd)}\n错误: {e.stderr}")

    def check_tag_exists(self, tag_name: str) -> bool:
        """
        检查 tag 是否存在

        Args:
            tag_name: tag 名称

        Returns:
            如果 tag 存在返回 True，否则返回 False
        """
        try:
            # 尝试解析 tag，如果成功则存在
            subprocess.run(
                ["git", "-C", str(self.repo_path), "rev-parse", "--verify", "--quiet", tag_name],
                capture_output=True,
                check=True
            )
            return True
        except subprocess.CalledProcessError:
            return False
        except Exception:
            return False

    def extract_version(self, branch_name: str) -> Optional[str]:
        """
        从分支名称中提取版本号

        例如: android16-6.12-2025-12_r4 -> android16-6.12-2025-12

        Args:
            branch_name: 分支名称

        Returns:
            版本号字符串，如果无法提取则返回None
        """
        # 匹配格式: android16-6.12-2025-12_r4 或类似格式
        # 提取到最后一个下划线之前的部分
        match = re.match(r'^(.+?)_r\d+$', branch_name)
        if match:
            return match.group(1)

        # 如果没有_r后缀，尝试直接匹配版本格式
        match = re.match(r'^(android\d+-\d+\.\d+-\d{4}-\d{2})', branch_name)
        if match:
            return match.group(1)

        return None

    def resolve_branch_ref(self, branch_name: str) -> str:
        """
        解析分支引用，尝试多种可能的格式

        Args:
            branch_name: 分支名称

        Returns:
            可用的分支引用
        """
        # 尝试的顺序：直接名称 -> origin/名称 -> remotes/origin/名称
        candidates = [
            branch_name,
            f"origin/{branch_name}",
            f"remotes/origin/{branch_name}",
        ]

        for ref in candidates:
            try:
                # 尝试检查引用是否存在（使用 rev-parse，如果不存在会抛出异常）
                result = subprocess.run(
                    ["git", "-C", str(self.repo_path), "rev-parse", "--verify", "--quiet", ref],
                    capture_output=True,
                    check=True
                )
                # 如果成功，返回这个引用
                return ref
            except subprocess.CalledProcessError:
                # 引用不存在，继续尝试下一个
                continue
            except Exception:
                continue

        # 如果都失败，尝试查找所有远程分支和标签
        try:
            # 获取所有分支和标签
            all_refs = self._run_git("show-ref")
            for line in all_refs.split('\n'):
                if not line:
                    continue
                parts = line.split()
                if len(parts) >= 2:
                    ref_path = parts[1]
                    # 检查是否匹配
                    if ref_path.endswith(branch_name) or ref_path.endswith(f"/{branch_name}"):
                        # 提取引用名称（去掉 refs/heads/ 或 refs/remotes/ 前缀）
                        if ref_path.startswith("refs/remotes/"):
                            return ref_path.replace("refs/remotes/", "")
                        elif ref_path.startswith("refs/heads/"):
                            return ref_path.replace("refs/heads/", "")
                        elif ref_path.startswith("refs/tags/"):
                            return ref_path.replace("refs/tags/", "")
        except Exception:
            pass

        # 如果都找不到，返回原始名称（让后续操作报错，但会提供更详细的错误信息）
        print(f"警告: 无法解析分支引用 '{branch_name}'，将尝试直接使用")
        return branch_name

    def find_initialize_commit(self, branch_name: str, version: str) -> Optional[str]:
        """
        在指定分支上查找Initialize提交

        Args:
            branch_name: 分支名称
            version: 版本号（用于匹配Initialize提交）

        Returns:
            Initialize提交的哈希值，如果未找到则返回None
        """
        # 解析分支引用
        branch_ref = self.resolve_branch_ref(branch_name)

        # 构建Initialize提交的匹配模式
        pattern = f"Initialize {version}"

        # 获取分支上的所有提交
        try:
            commits = self._run_git("log", "--oneline", "--grep", pattern, branch_ref)
            if not commits:
                return None

            # 获取第一个匹配的提交的完整哈希
            first_line = commits.split('\n')[0]
            commit_hash = first_line.split()[0]

            # 验证提交信息是否包含Initialize
            commit_msg = self._run_git("log", "-1", "--format=%s", commit_hash)
            if pattern in commit_msg:
                return commit_hash

            return None
        except Exception as e:
            print(f"查找Initialize提交时出错 (分支: {branch_ref}): {e}")
            return None

    def get_commits_after_initialize(self, branch_name: str, initialize_commit: str) -> List[str]:
        """
        获取Initialize提交之后的所有提交

        Args:
            branch_name: 分支名称
            initialize_commit: Initialize提交的哈希值

        Returns:
            提交哈希值列表
        """
        # 解析分支引用
        branch_ref = self.resolve_branch_ref(branch_name)

        try:
            # 获取从Initialize提交到分支顶部的所有提交（不包括Initialize提交本身）
            commits = self._run_git(
                "log",
                "--format=%H",
                f"{initialize_commit}..{branch_ref}"
            )

            if not commits:
                return []

            return commits.split('\n')
        except Exception as e:
            print(f"获取提交列表时出错 (分支: {branch_ref}): {e}")
            return []

    def get_change_id(self, commit_hash: str) -> Optional[str]:
        """
        获取提交的 Change-Id

        Args:
            commit_hash: 提交哈希值

        Returns:
            Change-Id 字符串，如果不存在则返回None
        """
        try:
            # 从提交信息中提取 Change-Id
            commit_msg = self._run_git("log", "-1", "--format=%B", commit_hash)
            for line in commit_msg.split('\n'):
                line = line.strip()
                if line.startswith("Change-Id:"):
                    return line.replace("Change-Id:", "").strip()
            return None
        except Exception:
            return None

    def find_commit_by_change_id(self, change_id: str, branch_name: str, base_commit: Optional[str] = None) -> Optional[str]:
        """
        在指定分支上根据 Change-Id 查找提交

        Args:
            change_id: Change-Id
            branch_name: 分支名称
            base_commit: 基准提交，只在此提交之后查找（可选）

        Returns:
            提交哈希值，如果未找到则返回None
        """
        if not change_id:
            return None

        # 解析分支引用
        branch_ref = self.resolve_branch_ref(branch_name)

        try:
            # 构建搜索范围
            if base_commit:
                # 只在基准提交之后查找
                search_range = f"{base_commit}..{branch_ref}"
            else:
                search_range = branch_ref

            # 在分支上搜索包含该 Change-Id 的提交
            commits = self._run_git("log", "--format=%H", "--grep", change_id, search_range)
            if commits:
                # 返回第一个匹配的提交
                return commits.split('\n')[0]
            return None
        except Exception:
            return None

    def get_parent_commit(self, commit_hash: str) -> Optional[str]:
        """
        获取提交的父提交（第一个父提交）

        Args:
            commit_hash: 提交哈希值

        Returns:
            父提交哈希值，如果没有父提交则返回None
        """
        try:
            parent = self._run_git("rev-parse", f"{commit_hash}^")
            return parent
        except Exception:
            return None

    def find_base_commit_in_branch(self, base_commit: str, branch_name: str) -> Optional[str]:
        """
        在指定分支上查找基准提交（通过 commit hash 或 Change-Id）

        Args:
            base_commit: 基准提交哈希值
            branch_name: 分支名称

        Returns:
            在分支上找到的对应提交哈希值，如果未找到则返回None
        """
        # 解析分支引用
        branch_ref = self.resolve_branch_ref(branch_name)

        # 先尝试直接查找（通过 commit hash）
        try:
            subprocess.run(
                ["git", "-C", str(self.repo_path), "merge-base", "--is-ancestor", base_commit, branch_ref],
                capture_output=True,
                check=True
            )
            # 如果提交是分支的祖先，返回基准提交本身
            return base_commit
        except subprocess.CalledProcessError:
            pass

        # 如果直接查找失败，尝试通过 Change-Id 查找
        change_id = self.get_change_id(base_commit)
        if change_id:
            found = self.find_commit_by_change_id(change_id, branch_name)
            if found:
                return found

        return None

    def is_commit_in_branch(self, commit_hash: str, branch_name: str, use_change_id: bool = True) -> bool:
        """
        检查提交是否在指定分支上（优先使用 Change-Id 匹配）

        Args:
            commit_hash: 提交哈希值
            branch_name: 分支名称
            use_change_id: 是否使用 Change-Id 进行匹配（默认True）

        Returns:
            如果提交在分支上返回True，否则返回False
        """
        # 解析分支引用
        branch_ref = self.resolve_branch_ref(branch_name)

        # 优先使用 Change-Id 匹配
        if use_change_id:
            change_id = self.get_change_id(commit_hash)
            if change_id:
                found_commit = self.find_commit_by_change_id(change_id, branch_name)
                if found_commit:
                    return True

        # 如果 Change-Id 匹配失败，回退到使用 commit hash
        try:
            # 使用 git merge-base --is-ancestor 检查提交是否是分支的祖先
            # 如果提交是分支的祖先，说明提交在分支上
            subprocess.run(
                ["git", "-C", str(self.repo_path), "merge-base", "--is-ancestor", commit_hash, branch_ref],
                capture_output=True,
                check=True
            )
            return True
        except subprocess.CalledProcessError:
            # 命令返回非0，说明提交不是分支的祖先
            return False
        except Exception as e:
            # 其他错误，尝试备用方法
            try:
                # 使用 git branch --contains 检查
                result = self._run_git("branch", "-a", "--contains", commit_hash)
                if branch_name in result or branch_ref in result:
                    return True
                return False
            except:
                return False

    def compare_branches(self, branch1: str, branch2: str, output_file: Optional[str] = None):
        """
        找出分支1上Initialize之后的提交中，哪些在分支2上不存在

        Args:
            branch1: 第一个分支名称（要查找其上的提交）
            branch2: 第二个分支名称（用于检查提交是否存在）
            output_file: 输出文件路径（可选）
        """
        print(f"查找分支 {branch1} 上 Initialize 之后的提交，检查哪些不在分支 {branch2} 上")
        print(f"仓库路径: {self.repo_path}\n")

        # 检查两个 tag 是否存在
        print("正在检查 tag 是否存在...")
        tag1_exists = self.check_tag_exists(branch1)
        tag2_exists = self.check_tag_exists(branch2)

        if not tag1_exists or not tag2_exists:
            missing_tags = []
            if not tag1_exists:
                missing_tags.append(branch1)
            if not tag2_exists:
                missing_tags.append(branch2)

            print(f"以下 tag 不存在: {', '.join(missing_tags)}")
            print("正在执行 git pull 以更新所有 tag 和分支信息...")
            try:
                self._run_git("pull", "--tags")
                print("git pull 完成\n")
            except Exception as e:
                print(f"警告: git pull 失败: {e}\n")

            # 再次检查 tag
            print("再次检查 tag 是否存在...")
            tag1_exists_after = self.check_tag_exists(branch1)
            tag2_exists_after = self.check_tag_exists(branch2)

            if not tag1_exists_after or not tag2_exists_after:
                still_missing = []
                if not tag1_exists_after:
                    still_missing.append(branch1)
                if not tag2_exists_after:
                    still_missing.append(branch2)

                print(f"警告: 以下 tag 在 git pull 后仍然不存在: {', '.join(still_missing)}")
                print("脚本退出执行，无法正确找到这些分支/标签\n")
                sys.exit(1)
            else:
                print("所有 tag 现在都存在\n")
        else:
            print("所有 tag 都存在，无需执行 git pull\n")

        # 提取版本号
        version1 = self.extract_version(branch1)

        if not version1:
            raise ValueError(f"无法从分支名称提取版本号: {branch1}")

        print(f"分支1 ({branch1}) 版本: {version1}")
        print(f"分支2 ({branch2}) 用于检查提交是否存在\n")

        # 查找Initialize提交
        print(f"正在查找分支 {branch1} 上的 Initialize {version1} 提交...")
        init_commit1 = self.find_initialize_commit(branch1, version1)
        if not init_commit1:
            raise ValueError(f"在分支 {branch1} 上未找到 'Initialize {version1}' 提交")
        print(f"找到Initialize提交: {init_commit1}\n")

        # 获取Initialize提交的父提交（作为基准点）
        print(f"正在查找 Initialize 提交的父提交（基准点）...")
        base_commit = self.get_parent_commit(init_commit1)
        if not base_commit:
            raise ValueError(f"无法找到 Initialize 提交的父提交")
        print(f"找到基准提交: {base_commit[:8]}\n")

        # 在分支2上查找对应的基准提交
        print(f"正在在分支 {branch2} 上查找对应的基准提交...")
        base_commit_in_branch2 = self.find_base_commit_in_branch(base_commit, branch2)
        if not base_commit_in_branch2:
            print(f"警告: 在分支 {branch2} 上未找到基准提交，将搜索整个分支")
            base_commit_in_branch2 = None
        else:
            print(f"在分支 {branch2} 上找到基准提交: {base_commit_in_branch2[:8]}\n")

        # 获取分支1上Initialize提交之后的所有提交
        print(f"正在获取分支 {branch1} 上 Initialize 之后的提交...")
        commits1 = self.get_commits_after_initialize(branch1, init_commit1)
        print(f"找到 {len(commits1)} 个提交\n")

        if not commits1:
            print("分支1上Initialize之后没有提交，无需比较。")
            return

        # 检查每个提交是否在分支2上（使用 Change-Id 匹配，只在基准提交之后查找）
        print(f"正在检查这些提交是否在分支 {branch2} 上（使用 Change-Id 匹配，仅在基准提交之后查找）...")
        missing_commits = []
        found_commits = []
        found_by_change_id = {}  # 记录通过 Change-Id 找到的提交映射

        for i, commit in enumerate(commits1, 1):
            if i % 10 == 0:
                print(f"  已检查 {i}/{len(commits1)} 个提交...", end='\r')

            # 先尝试使用 Change-Id 匹配（只在基准提交之后查找）
            change_id = self.get_change_id(commit)
            if change_id:
                found_commit = self.find_commit_by_change_id(change_id, branch2, base_commit_in_branch2)
                if found_commit:
                    found_commits.append(commit)
                    found_by_change_id[commit] = found_commit
                    continue

            # 如果 Change-Id 匹配失败，使用 commit hash 检查（但只在基准提交之后）
            if base_commit_in_branch2:
                # 检查提交是否在基准提交之后的分支上
                try:
                    subprocess.run(
                        ["git", "-C", str(self.repo_path), "merge-base", "--is-ancestor", commit, branch2],
                        capture_output=True,
                        check=True
                    )
                    # 检查是否在基准提交之后
                    try:
                        subprocess.run(
                            ["git", "-C", str(self.repo_path), "merge-base", "--is-ancestor", base_commit_in_branch2, commit],
                            capture_output=True,
                            check=True
                        )
                        found_commits.append(commit)
                        continue
                    except:
                        pass
                except:
                    pass

            # 如果都不匹配，则标记为缺失
            missing_commits.append(commit)

        print(f"  检查完成: {len(commits1)} 个提交\n")

        # 显示结果
        print("=" * 80)
        print("结果统计:")
        print("=" * 80)
        print(f"分支 {branch1} 上 Initialize {version1} 之后的总提交数: {len(commits1)}")
        print(f"在分支 {branch2} 上存在的提交数: {len(found_commits)}")
        if found_by_change_id:
            print(f"  - 其中通过 Change-Id 匹配找到的: {len(found_by_change_id)} 个")
        print(f"在分支 {branch2} 上不存在的提交数: {len(missing_commits)}")
        print()

        if missing_commits:
            print("=" * 80)
            print(f"在分支 {branch1} 上但不在分支 {branch2} 上的提交:")
            print("=" * 80)

            output_lines = []
            for i, commit in enumerate(missing_commits, 1):
                try:
                    # 获取提交信息
                    commit_msg = self._run_git("log", "-1", "--format=%s", commit)
                    commit_author = self._run_git("log", "-1", "--format=%an", commit)
                    commit_date = self._run_git("log", "-1", "--format=%ad", commit)
                    change_id = self.get_change_id(commit)

                    info = f"{i}. {commit[:8]} - {commit_msg}"
                    info += f"\n   作者: {commit_author}, 日期: {commit_date}"
                    if change_id:
                        info += f"\n   Change-Id: {change_id}"

                    print(info)
                    output_lines.append(info)

                    # 显示文件变更
                    try:
                        files = self._run_git("diff-tree", "--no-commit-id", "--name-only", "-r", commit)
                        if files:
                            file_list = files.split('\n')
                            print(f"   修改文件数: {len(file_list)}")
                            if len(file_list) <= 5:
                                print(f"   文件: {', '.join(file_list)}")
                            else:
                                print(f"   文件: {', '.join(file_list[:5])} ... (共{len(file_list)}个)")
                    except:
                        pass

                    print()
                    output_lines.append("")

                except Exception as e:
                    print(f"{i}. {commit[:8]} - (获取提交信息失败: {e})")
                    output_lines.append(f"{i}. {commit[:8]} - (获取提交信息失败: {e})")

            # 保存到文件
            if output_file:
                output_path = Path(output_file)
                content = "\n".join([
                    f"分支 {branch1} 上但不在分支 {branch2} 上的提交",
                    f"总计: {len(missing_commits)} 个提交",
                    "=" * 80,
                    ""
                ] + output_lines)
                output_path.write_text(content, encoding='utf-8')
                print(f"\n结果已保存到: {output_file}")
        else:
            print(f"\n所有分支 {branch1} 上 Initialize 之后的提交都在分支 {branch2} 上。")


def main():
    """主函数"""
    parser = argparse.ArgumentParser(
        description="查找分支1上Initialize提交之后的提交中，哪些在分支2上不存在",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python3 compare_branches.py android16-6.12-2025-09_r19 android16-6.12-2025-12_r4
  python3 compare_branches.py android16-6.12-2025-09_r19 android16-6.12-2025-12_r4 /path/to/repo
  python3 compare_branches.py android16-6.12-2025-09_r19 android16-6.12-2025-12_r4 -o result.txt
  python3 compare_branches.py android16-6.12-2025-09_r19 android16-6.12-2025-12_r4 -r /path/to/repo

说明:
  查找分支1上Initialize之后的提交，检查哪些不在分支2上
        """
    )

    parser.add_argument(
        "branch1",
        help="第一个分支节点名称，查找此分支上Initialize之后的提交 (例如: android16-6.12-2025-09_r19)"
    )

    parser.add_argument(
        "branch2",
        help="第二个分支名称，用于检查提交是否存在 (例如: android16-6.12-2025-12_r4)"
    )

    parser.add_argument(
        "-r", "--repo",
        default=get_resource_path('../../common/'),
        help="Git仓库路径 (可选，默认: 当前目录，与位置参数互斥)"
    )

    parser.add_argument(
        "-o", "--output",
        help="将详细差异输出到文件"
    )

    args = parser.parse_args()

    # 确定仓库路径：优先使用 -r 选项，否则使用位置参数，最后默认为当前目录
    repo_path = args.repo or args.repo_path or "."

    try:
        comparator = BranchComparator(repo_path)
        comparator.compare_branches(args.branch1, args.branch2, args.output)
    except Exception as e:
        print(f"错误: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()

