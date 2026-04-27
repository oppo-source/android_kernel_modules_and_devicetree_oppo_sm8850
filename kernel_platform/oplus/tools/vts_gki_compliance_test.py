#!/usr/bin/env python
# -*- coding: utf-8 -*-
# coding=utf-8

# ------------------------------------------------------------
# Copyright (C), 2023-2024, OPLUS Mobile Comm Corp., Ltd.
# ------------------------------------------------------------
# Author: 80398218(zhangxiaowei4)
# ------------------------------------------------------------
import os
import requests
import zipfile
import shutil
import logging
import argparse
import subprocess

class Bsp_Vts_Gki_Compliance_Test:
    def __init__(self, device_id):
        self.device_id = device_id
        self.logger = logging.getLogger(self.__class__.__name__)
        self.logger.setLevel(logging.DEBUG)  # 设置日志级别

        if not self.logger.handlers:
            handler = logging.StreamHandler()
            formatter = logging.Formatter('%(asctime)s: %(name)s <%(levelname)s>: %(message)s')
            handler.setFormatter(formatter)
            self.logger.addHandler(handler)

    def adb_shell(self, command):
        command_list = [
            "adb", "-s", self.device_id, "shell", command
        ]
        try:
            process = subprocess.Popen(command_list, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
            stdout, stderr = process.communicate()
            if process.returncode == 0:
                return stdout.strip()
        except Exception as e:
            self.logger.error("An error occurred in adb shell:", e)

    def command_run(self, command):
        try:
            subprocess.run(command, check=True)
        except Exception as e:
            self.logger.error("An error occurred in command run:", e)

    def initialize_testcase(self):
        kernel_version_info = self.adb_shell("uname -r")
        self.logger.info(kernel_version_info)
        if "abogki" in kernel_version_info:
            kernel_type = "ogki"
        elif "ab" in kernel_version_info:
            kernel_type = "gki"
        elif "-o-" in kernel_version_info:
            kernel_type = "oki"

        if kernel_type == "oki":
            raise Exception("此用例仅测试kernel类型为GKI或者OGKI的项目")

    def cleanup_testcase(self):
        self.logger.info(f"开始删除测试文件")
        self.adb_shell("rm -rf /data/local/tmp/vts_gki_compliance_test")
        try:
            shutil.rmtree("vts_gki_compliance_test")
            os.remove("vts_gki_compliance_test.zip")
        except Exception as e:
            self.logger.error(f"Deleting test package exception: {e}")

    def download_zip(self, url, save_path="."):
        os.makedirs(save_path, exist_ok=True)

        file_name = url.split("/")[-1]
        if not file_name.endswith(".zip"):
            file_name = "vts_gki_compliance_test.zip"

        response = requests.get(url, stream=True)
        response.raise_for_status()  # 检查请求是否成功

        zip_path = os.path.join(save_path, file_name)
        with open(zip_path, "wb") as f:
            for chunk in response.iter_content(chunk_size=8192):
                f.write(chunk)

        self.zip_path = os.path.abspath(zip_path)
        self.logger.info(f"Dwonload zip file to: {self.zip_path}")

    def unzip_file(self, extract_to="."):
        with zipfile.ZipFile(self.zip_path, 'r') as zip_ref:
            zip_ref.extractall(extract_to)
        self.unzip_file_path = self.zip_path.split('.')[0]
        self.logger.info(f"Unzip file to: {self.unzip_file_path}")

    def download_xml(self, url):
        response = requests.get(url)
        response.raise_for_status()  # 检查请求是否成功
        save_path = self.unzip_file_path + r'\approved-ogki-builds.xml'
        with open(save_path, "wb") as f:
            f.write(response.content)
        self.logger.info(f"Dwonload xml file to: {save_path}")

    def main(self):
        zip_url = "xxx/aosp-gki-image-local/android-vts/default/vts_gki_compliance_test.zip"
        xml_url = "xxx/aosp-gki-image-local/android-vts/kernel_configs/default/approved-ogki-builds.xml"

        self.download_zip(zip_url)
        self.unzip_file()
        self.download_xml(xml_url)

        self.command_run(f"adb push {self.unzip_file_path} /data/local/tmp/vts_gki_compliance_test")

        dir_path = self.adb_shell("ls /data/local/tmp/vts_gki_compliance_test")
        if "No such file or directory" in dir_path:
            raise PreconditionsException("测试项不适用")

        file_path = self.adb_shell("ls /data/local/tmp/vts_gki_compliance_test/approved-ogki-builds.xml")
        if "No such file or directory" in file_path:
            raise PreconditionsException("测试项不适用")

        self.adb_shell("chmod +x /data/local/tmp/vts_gki_compliance_test/arm64/vts_gki_compliance_test")
        test_log = self.adb_shell("/data/local/tmp/vts_gki_compliance_test/arm64/vts_gki_compliance_test")
        self.logger.info(f"\n{test_log}")
        if "[  FAILED  ]" in test_log:
            log_lines = test_log.splitlines()
            error_log = []
            for log_line in log_lines:
                if log_line.startswith("[  FAILED  ]"):
                    self.logger.error(log_line)
                    if not log_line.endswith("ms)"):
                        error_log.append(log_line.strip("[  FAILED  ]"))
            error_res = '\n'.join(error_log)
            raise Exception(f"vts_gki_compliance_test failed!\n{error_res}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Test for fast vts_gki_compliance_test.")
    parser.add_argument('-d', '--device', default=None, help="Input device id")

    args = parser.parse_args()

    Test_instance = Bsp_Vts_Gki_Compliance_Test(args.device)
    Test_instance.initialize_testcase()
    try:
        Test_instance.main()
    except Exception as e:
        raise Exception(f"vts_gki_compliance_test failed!\n{e}")
    finally:
        Test_instance.cleanup_testcase()

