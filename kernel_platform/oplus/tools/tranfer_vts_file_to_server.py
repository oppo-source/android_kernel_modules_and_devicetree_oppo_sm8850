import os
import sys
import json
import common
import hashlib
import requests
import subprocess
import secure_upload
import ack_lts_spl_check

def calculate_file_md5(file_path):
    if not os.path.isfile(file_path):
        return None

    try:
        md5 = hashlib.md5()
        with open(file_path, 'rb') as f:
            for chunk in iter(lambda: f.read(4096), b""):
                md5.update(chunk)
        return md5.hexdigest()
    except Exception as e:
        print("Error reading file {}: {}".format(file_path, e))
        return None

def get_local_path(path):
    base_path = os.path.dirname(os.path.realpath(__file__))
    full_path = os.path.join(base_path, "{}".format(path))
    return full_path

def get_files_list():
    files_list = []
    file_path = get_local_path('vts_file_list.txt')
    with open(file_path, 'r') as js:
        file_lines = js.readlines()
        for file_line in file_lines:
            files_list.append(file_line.strip())

    return files_list

def get_json_to_dict(json_file):
    if  not os.path.exists(json_file):
        return

    with open(json_file, 'r', encoding='utf-8') as file:
        remote_brach_dict = json.load(file)

    return remote_brach_dict

def save_dict_to_json(data, json_file):
    if os.path.exists(json_file):
        os.remove(json_file)

    with open(json_file, 'w', encoding='utf-8') as file:
        json.dump(data, file, separators=(",", ": "), indent=4)

def clone_kernel_configs(branch, destination):
    repo_url = "ssh://gerrit_url:29418/kernel/configs"
    try:
        # Check if the destination directory exists, and delete it if it does
        if os.path.exists(destination):
            print("{} already exists".format(destination))
            subprocess.run(["rm", "-rf", destination], check=True)

        # Use shallow clone to clone a single branch and only include the latest commit
        subprocess.run([
            "git", "clone", "--depth", "1", "--branch", branch, repo_url, destination
        ], check=True)

        print("gerrit {} branch {} cloned to {}".format(repo_url, branch, destination))
    except subprocess.CalledProcessError as e:
        print("clone error: {}".format(e))

def download_file_to_local(remote_url, local_file):
    if os.path.exists(local_file):
        os.remove(local_file)

    try:
        response = requests.get(remote_url)
        with open(local_file, 'wb') as f:
            f.write(response.content)
    except  Exception as e:
        print("Download {} failed\n{}".format(remote_url, e))

def upload_file_to_remote(local_path, files_list, remote_branches):
    last_remote_branch = None

    for remote_path in remote_branches.keys():
        remote_branch = remote_branches[remote_path]

        if remote_branch != last_remote_branch:
            clone_kernel_configs(remote_branch, local_path)
            last_remote_branch = remote_branch

        kernel_configs_remote_url = "xxx/aosp-gki-image-local/android-vts/kernel_configs/{}".format(remote_path)

        local_md5_file = os.path.join(local_path, "md5_sums.json")
        remote_md5_file = os.path.join(kernel_configs_remote_url, "md5_sums.json")

        download_file_to_local(remote_md5_file, local_md5_file)

        remote_md5_sums = get_json_to_dict(local_md5_file)
        local_md5_sums = {}
        md5_flag = False

        for file_name in files_list:
            remote_file = os.path.join(kernel_configs_remote_url, file_name)
            local_file = os.path.join(local_path, file_name)

            md5_data = calculate_file_md5(local_file)
            local_md5_sums[file_name] = md5_data

            try:
                if md5_data != remote_md5_sums[file_name]:
                    common.upload_file(local_file, remote_file)
                    md5_flag = True
                else:
                    print("{} don't change. No file upload required.".format(file_name))
            except  Exception as e:
                print("An error occurred: {}".format(e))

        if md5_flag:
            save_dict_to_json(local_md5_sums, local_md5_file)
            common.upload_file(local_md5_file, remote_md5_file)

def main():
    kernel_configs_local_path = get_local_path("out/kernel_configs")
    files_list = get_files_list()
    kernel_configs_remote_branch_file = get_local_path("kernel_configs_remote_branch.json")
    remote_branches = get_json_to_dict(kernel_configs_remote_branch_file)
    upload_file_to_remote(kernel_configs_local_path, files_list, remote_branches)

if __name__ == "__main__":
    main()
