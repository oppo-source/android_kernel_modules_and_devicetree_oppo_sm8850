import os
import subprocess
from datetime import datetime
import argparse
import common
import xml.etree.ElementTree as ET


def get_tools_path(relative_path):
    base_path = os.path.dirname(os.path.realpath(__file__))
    full_path = os.path.join(base_path, relative_path)
    return full_path


def parse_cmd_args():
    parser = argparse.ArgumentParser(description="auto merge script")
    parser.add_argument('--path', type=str, help='default path', default="kernel_platform")
    parser.add_argument('--branch', type=str, help='default branch', default="kernel_platform")
    parser.add_argument('--action', type=str, help='default action', default="checkout")
    parser.add_argument('-o', '--out', type=str, help='Kernel tmp out dir (default: tools/out)',
                        default=get_tools_path("out"))

    args = parser.parse_args()
    for key, value in vars(args).items():
        print(f"{key}: {value}")
    return args


def find_git_directories(base_dir, relative_paths_file, full_paths_file):
    git_paths = []

    for root, dirs, files in os.walk(base_dir):
        if '.git' in dirs:
            # 构建相对于 base_dir 的路径
            relative_path = os.path.relpath(root, base_dir)
            git_paths.append((relative_path, os.path.join(base_dir, relative_path)))

    # 确保输出文件的目录存在
    output_dir = os.path.dirname(relative_paths_file)
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # 同步写入文件
    with open(relative_paths_file, 'w') as f1, open(full_paths_file, 'w') as f2:
        for relative_path, full_path in git_paths:
            f1.write(relative_path + '\n')
            f2.write(full_path + '\n')


def get_upstream_from_manifest(manifest_path, project_path):
    try:
        # 解析 manifest.xml 文件
        tree = ET.parse(manifest_path)
        root = tree.getroot()

        # 遍历 <project> 元素
        for project in root.findall('project'):
            path = project.get('path')
            if path == project_path:
                name = project.get('name', "")
                upstream = project.get('upstream', "")
                return name, upstream

        # 如果没有找到匹配的路径
        print(f"No project found with path: {project_path}")
        return None, None
    except ET.ParseError as e:
        print(f"Error parsing XML: {e}")
        return None, None
    except FileNotFoundError:
        print(f"File {manifest_path} not found.")
        return None, None
    except Exception as e:
        print(f"An error occurred: {e}")
        return None, None


def get_revision_from_manifest(manifest_file):
    try:
        # 解析 XML 文件
        tree = ET.parse(manifest_file)
        root = tree.getroot()

        # 查找 <default> 标签
        default_tag = root.find('default')

        if default_tag is not None:
            # 获取 revision 属性的值
            revision = default_tag.get('revision')
            return revision
        else:
            return None
    except ET.ParseError as e:
        print(f"Error parsing XML: {e}")
        return None
    except FileNotFoundError:
        print(f"File not found: {manifest_file}")
        return None
    except Exception as e:
        print(f"Error: {e}")
        return None


def get_branch_name(full_branch_name):
    # 分割字符串
    parts = full_branch_name.split('/', 1)
    # 获取第一个 '/' 后面的所有内容
    if len(parts) > 1:
        return parts[1]
    else:
        return None


def check_local_branch_exists(git_dir, branch_name):
    try:
        # 使用 git branch 命令获取所有本地分支
        result = subprocess.run(['git', '-C', git_dir, 'branch', '--list', branch_name], capture_output=True, text=True,
                                check=True)
        # 检查输出是否包含分支名称
        if branch_name in result.stdout:
            print(f"Branch '{branch_name}' exists locally.")
            return branch_name
        else:
            print(f"Branch '{branch_name}' does not exist locally.")
            return None
    except subprocess.CalledProcessError as e:
        print(f"An error occurred: {e}")
        return None


def check_out_git_directory(git_dir, branch_name):
    try:
        command = ['git', '-C', git_dir, 'reset', '--hard', branch_name]
        print(f"Executing command: {' '.join(command)}")
        subprocess.run(command, check=True)

        # 切换到指定分支

        branch = get_branch_name(branch_name)
        print(branch)  # 输
        tmp_branch = "tmp_branch"
        existing_branch = check_local_branch_exists(git_dir, branch)
        if existing_branch:
            print(f"Existing branch: {existing_branch}")
            command = ['git', '-C', git_dir, 'checkout', branch_name, '-b', tmp_branch]
            print(f"Executing command: {' '.join(command)}")
            subprocess.run(command, check=True)

            command = ['git', '-C', git_dir, 'reset', '--hard', branch_name]
            print(f"Executing command: {' '.join(command)}")
            subprocess.run(command, check=True)

            command = ['git', '-C', git_dir, 'branch', '-D', existing_branch]
            print(f"Executing command: {' '.join(command)}")
            subprocess.run(command, check=True)

        command = ['git', '-C', git_dir, 'checkout', '-t', branch_name]
        print(f"Executing command: {' '.join(command)}")
        subprocess.run(command, check=True)

        existing_branch = check_local_branch_exists(git_dir, tmp_branch)
        if existing_branch:
            print(f"Existing branch: {existing_branch}")
            command = ['git', '-C', git_dir, 'branch', '-D', existing_branch]
            print(f"Executing command: {' '.join(command)}")
            subprocess.run(command, check=True)

        print(f"All branches and tags in {git_dir} have been updated.")
    except subprocess.CalledProcessError as e:
        print(f"An error occurred while updating {git_dir}: {e}")


def is_on_branch(repo_path):
    try:
        # 执行 git rev-parse --abbrev-ref HEAD 命令
        result = subprocess.run(
            ['git', '-C', repo_path, 'rev-parse', '--abbrev-ref', 'HEAD'],
            capture_output=True,
            text=True,
            check=True
        )

        # 获取命令输出
        output = result.stdout.strip()

        # 如果输出是 'HEAD'，则表示不在任何分支上
        if output == 'HEAD':
            return False
        else:
            return True
    except subprocess.CalledProcessError as e:
        # 如果命令执行失败，打印错误信息并返回 False
        print(f"Error: {e.stderr}")
        return False


def git_pull_git_directory(git_dir):
    try:

        # Fetch all branches
        command = ['git', '-C', git_dir, 'fetch', '--all']
        print(f"Executing command: {' '.join(command)}")
        subprocess.run(command, check=True)

        # Fetch all tags
        command = ['git', '-C', git_dir, 'fetch', '--tags']
        print(f"Executing command: {' '.join(command)}")
        subprocess.run(command, check=True)

        if is_on_branch(git_dir):
            # Fetch all tags
            command = ['git', '-C', git_dir, 'pull']
            print(f"Executing command: {' '.join(command)}")
            subprocess.run(command, check=True)

        print(f"All branches and tags in {git_dir} have been updated.\n")
    except subprocess.CalledProcessError as e:
        print(f"An error occurred while updating {git_dir}: {e}")


def get_remote_ssh_url(git_dir):
    try:
        # 获取 Gerrit 用户名
        result = subprocess.run(
            ['git', '-C', git_dir, 'config', 'remote.origin.url'],
            capture_output=True,
            text=True,
            check=True
        )

        # 解析 URL
        url = result.stdout.strip()
        if url.startswith("ssh://"):
            return url

        print("Username not found in remote URL.")
        return None
    except subprocess.CalledProcessError as e:
        print(f"An error occurred while getting remote URL: {e}")
        return None
    except Exception as e:
        print(f"An error occurred: {e}")
        return None


def get_git_user_name():
    try:
        # 执行 git config --list 命令
        result = subprocess.run(['git', 'config', '--list'], capture_output=True, text=True, check=True)

        # 解析输出以获取 user.name 的值
        for line in result.stdout.splitlines():
            if line.startswith('user.name='):
                return line.split('=', 1)[1]

        return "user.name not found"
    except subprocess.CalledProcessError as e:
        return f"Error: {e}"


def generate_git_remote_add_command(path, remote_name, url, user_name):
    try:
        # 提取主机部分和项目部分
        if url.startswith("ssh://"):
            url = url[len("ssh://"):]

        # 分割主机名和端口号部分
        host_part, project_part = url.split('/', 1)

        # 构建完整的主机 URL
        host_url = f"ssh://{user_name}@{host_part}"

        # 构建完整的 URL
        full_url = f"{host_url}/{project_part}"

        # 构建 git remote add 命令
        command = f"git -C {path} remote add {remote_name} {full_url}"

        return command
    except Exception as e:
        return f"Error: {e}"


def execute_git_command(command):
    try:
        # 执行命令
        print(f"Executing command: {' '.join(command)}")
        result = subprocess.run(command, shell=True, check=True, capture_output=True, text=True)
        return result.stdout
    except subprocess.CalledProcessError as e:
        return f"Error: {e.stderr}"
    except Exception as e:
        return f"Error: {e}"


def extract_project_name_from_ssh_url(url):
    try:
        # 去掉 URL 的协议部分（ssh://）
        if url.startswith("ssh://"):
            url = url[len("ssh://"):]

        # 分割主机名和端口号部分
        host_part, project_part = url.split('/', 1)

        # 构建完整的主机 URL
        host_url = f"ssh://{host_part}"

        # 提取项目名称部分
        project_name = project_part

        return host_url, project_name
    except Exception as e:
        return f"Error: {e}", None


def run_git_command(repo_path, command):
    try:
        command = ['git', '-C', repo_path] + command
        print(f"Executing command: {' '.join(command)}")

        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )
        return result.stdout.strip().split('\n')
    except subprocess.CalledProcessError as e:
        raise Exception(f"Git command failed: {e.stderr}")


def check_branch_or_tag(repo_path, branch_or_tag_name, branch_type="remote"):
    if branch_type == "remote":
        branches = run_git_command(repo_path, ['branch', '--list', '-r', f'*{branch_or_tag_name}'])
    else:
        branches = run_git_command(repo_path, ['branch', '--list', '-a', f'*{branch_or_tag_name}'])

    tags = run_git_command(repo_path, ['tag', '--list', f'*{branch_or_tag_name}'])

    branch_match = [branch.strip() for branch in branches if branch.strip().endswith(branch_or_tag_name)]
    tag_match = [tag.strip() for tag in tags if tag.strip().endswith(branch_or_tag_name)]
    if len(branch_match) > 1 or len(tag_match) > 1:
        raise ValueError(f"Multiple branches or tags found ending with '{branch_or_tag_name}'")

    if branch_match and tag_match:
        raise ValueError(f"Both branch and tag found ending with '{branch_or_tag_name}'")

    if branch_match:
        return f"{branch_match[0]}"

    if tag_match:
        return f"{tag_match[0]}"

    return None


def git_merge(repo_path, merge_branch, commit_message=None):
    if not os.path.exists(repo_path):
        raise FileNotFoundError(f"Repository path '{repo_path}' does not exist.")

    try:
        # Check if the path is a valid git repository
        subprocess.run(['git', '-C', repo_path, 'rev-parse', '--is-inside-work-tree'], check=True)
    except subprocess.CalledProcessError:
        raise ValueError(f"Path '{repo_path}' is not a valid git repository.")

    try:
        # Fetch the latest changes
        # subprocess.run(['git', '-C', repo_path, 'fetch', '--all'], check=True)

        # Construct the merge command
        merge_command = ['git', '-C', repo_path, 'merge', '--no-edit', merge_branch]
        print(f"Executing command: {' '.join(merge_command)}")

        # Merge the specified branch with --no-edit to avoid manual commit message editing
        merge_result = subprocess.run(
            merge_command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )
        # If a commit message is provided, commit the merge
        if commit_message:
            subprocess.run(
                ['git', '-C', repo_path, 'commit', '-m', commit_message],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=True
            )

        return merge_result.stdout.strip()

    except subprocess.CalledProcessError as e:
        return f"Git merge failed: {e.stderr.strip()}"


def reset_to_remote_branch(repo_path, remote_branch):
    try:
        # Reset to the specified remote branch
        reset_command = ['reset', '--mixed', remote_branch]
        print(f"Executing command: git -C {repo_path} {' '.join(reset_command)}")
        run_git_command(repo_path, reset_command)

        return "Reset successful."

    except Exception as e:
        return f"Git reset failed: {e}"


def create_gerrit_commit(repo_path, vendor_branch_or_tag, ack_branch_or_tag, commit_message=None):
    try:

        # Add all changes to the staging area
        add_command = ['add', '.']
        print(f"Executing command: git -C {repo_path} {' '.join(add_command)}")
        run_git_command(repo_path, add_command)

        # Commit the changes with a message
        if not commit_message:
            commit_message = f"""
[AllawnTech.SCM.codebase][1/1]{{更新到{ack_branch_or_tag}}}

适用范围：{{无}}
准入id：{{无}}
分析：{{自动化升级}}
方案：{{
路径 {repo_path}
分支 {vendor_branch_or_tag}
更新 {ack_branch_or_tag}
}}
风险及影响[快/稳/省/功能/安全隐私]：{{无}}
测试建议：{{无}}
跨组依赖(topic name)：{{无}}
"""
        commit_command = ['commit', '-m', commit_message]
        print(f"Executing command: git -C {repo_path} {' '.join(commit_command)}")
        run_git_command(repo_path, commit_command)

        # Push the new branch to Gerrit
        push_command = ['push', 'origin', f'HEAD:refs/for/{vendor_branch_or_tag}']
        print(f"Executing command: git -C {repo_path} {' '.join(push_command)}")
        push_result = ""
        # push_result = run_git_command(repo_path, push_command)

        return push_result
    except Exception as e:
        return f"Git operation failed: {e}"


def check_remote_exists(vendor_git_path, remote_name):
    try:
        # 使用 git remote 命令获取所有远程名称
        result = subprocess.run(['git', '-C', vendor_git_path, 'remote'], capture_output=True, text=True, check=True)
        # 检查输出是否包含远程名称
        if remote_name in result.stdout.splitlines():
            print(f"Remote '{remote_name}' already exists.")
            return True
        else:
            print(f"Remote '{remote_name}' does not exist.")
            return False
    except subprocess.CalledProcessError as e:
        print(f"An error occurred: {e}")
        return False


def remote_add_command(args, host_url, ack_project_name, vendor_git_path, username):
    vendor_ack_ssh_url = host_url + "/" + ack_project_name
    print(f"vendor_ack_ssh_url: {vendor_ack_ssh_url}")

    command = generate_git_remote_add_command(vendor_git_path, "ack", vendor_ack_ssh_url, username)
    print(command)
    commands_file = os.path.join(args.out, 'git_remote_add_commands.txt')

    if command:
        # 将命令写入文件
        with open(commands_file, 'a') as f:
            f.write(f"{command}\n")
    else:
        print("Failed to generate git remote add command.")

    if not check_remote_exists(vendor_git_path, "ack"):
        # 执行命令
        output = execute_git_command(command)
        #print(f"Command Output: {output}")
    else:
        print(f"remote ack exist")


def update_git_directory(args, ack_git_path, username):
    vendor_manifest_file = '.repo/manifest.xml'
    ack_manifest_file = get_tools_path("manifest.xml")
    vendor_git_path = os.path.join(args.path, ack_git_path)
    ack_unknown_project = os.path.join(args.out, "ack_unknown_project.txt")

    vendor_project_name, vendor_branch_or_tag = get_upstream_from_manifest(vendor_manifest_file, vendor_git_path)
    if vendor_project_name:
        print(f"project:{vendor_project_name} path:{vendor_git_path} branch:{vendor_branch_or_tag}")
    else:
        print(f"vendor project not found for {vendor_git_path}")

    ack_project_name, ack_branch_or_tag = get_upstream_from_manifest(ack_manifest_file, ack_git_path)
    if ack_project_name:
        print(f"project:{ack_project_name} path:{ack_git_path}  branch:{ack_branch_or_tag}\n")
    else:
        print(f"ack project not found for {ack_git_path}")
        # 将命令写入文件
        with open(ack_unknown_project, 'a') as f:
            f.write(f"{vendor_git_path}\n")
        return

    if not ack_branch_or_tag:
        ack_branch_or_tag = get_revision_from_manifest(ack_manifest_file)
        print("ack_branch_or_tag:" + ack_branch_or_tag)

    # 获取远程 Push URL
    vendor_ssh_url = get_remote_ssh_url(vendor_git_path)
    print(f"vendor_ssh_url {vendor_ssh_url}")

    host_url, project_name = extract_project_name_from_ssh_url(vendor_ssh_url)
    print(f"Host URL: {host_url}")
    print(f"Project Name: {project_name}\n")

    if ack_project_name != vendor_project_name:
        remote_add_command(args, host_url, ack_project_name, vendor_git_path, username)
    else:
        print(f"ack and vendor are the same project:{ack_project_name}\n")

    git_pull_git_directory(vendor_git_path)
    full_ack_branch_or_tag = check_branch_or_tag(vendor_git_path, ack_branch_or_tag)
    print("full_ack_branch_or_tag:" + full_ack_branch_or_tag)

    full_vendor_branch_or_tag = check_branch_or_tag(vendor_git_path, vendor_branch_or_tag)
    print("full_vendor_branch_or_tag:" + full_vendor_branch_or_tag)

    check_out_git_directory(vendor_git_path, full_vendor_branch_or_tag)
    print(f"vendor_git_path: {vendor_git_path}\n")

    if args.action == "checkout":
        check_out_git_directory(vendor_git_path, full_ack_branch_or_tag)
    elif args.action == "gerrit":
        print(f"start merge")
        # git_merge(vendor_git_path, full_ack_branch_or_tag)
        reset_result = reset_to_remote_branch(vendor_git_path, full_vendor_branch_or_tag)
        print(reset_result)
        # 创建新的 Gerrit 提交
        gerrit_result = create_gerrit_commit(vendor_git_path, vendor_branch_or_tag, ack_branch_or_tag)
        print(gerrit_result)
    elif args.action == "merge":
        print(f"start merge")
        # git_merge(vendor_git_path, full_ack_branch_or_tag)
        # 创建新的 Gerrit 提交
        # gerrit_result = create_gerrit_commit(vendor_git_path, vendor_branch_or_tag, ack_branch_or_tag)
        # print(gerrit_result)


def process_git_directory(args, git_paths):
    try:
        username = get_git_user_name()
        print(f"username {username}")
        with open(git_paths, 'r') as file:
            for line in file:
                line = line.strip()
                if line:
                    print("\n" + line)
                    update_git_directory(args, line, username)
    except FileNotFoundError:
        print(f"File {git_paths} not found.")
    except Exception as e:
        print(f"An error occurred while reading the file: {e}")


def main():
    args = parse_cmd_args()
    full_paths = os.path.join(args.out, 'full_paths.txt')
    relative_paths = os.path.join(args.out, 'relative_paths.txt')
    common.delete_all_exists_file(args.out)
    find_git_directories(args.path, relative_paths, full_paths)
    process_git_directory(args, relative_paths)


if __name__ == "__main__":
    start_time = datetime.now()
    print("Begin time:", start_time.strftime("%Y-%m-%d %H:%M:%S"))
    main()
    end_time = datetime.now()
    print("End time:", end_time.strftime("%Y-%m-%d %H:%M:%S"))
    elapsed_time = end_time - start_time
    print("Total time:", common.format_duration(elapsed_time))
