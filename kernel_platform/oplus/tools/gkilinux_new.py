# !coding=utf-8
# -*- coding: utf-8 -*-
# @Time :  yungao.deng@all awnTech.SCM.codebase 2022/1/6 17:36
# @Author : 80398218
# @Email : dengyungao
# @File : get_same_project.py
# @Project : codebase_codes
import argparse
import json
import logging
import os
import re
import shutil
import subprocess
import sys
import time
import platform
from threading import Timer

from selenium import webdriver
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.common.by import By

logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(filename)s[line:%(lineno)d] - %(levelname)s: %(message)s')
logger = logging.getLogger(__name__)

class gki_codes:
    def __init__(self, args: argparse.Namespace):
        self.gkitag = args.gkitag.strip()
        self.env = args.env.strip()
        self.workspace = args.workspace
        self.kernelbuildid = args.kernelbuildid
        self.androidbuildid = args.androidbuildid
        self.project_name = args.project_name  # 需要提交的目标仓库
        self.branch_name = args.branch_name  # 需要提交的目标仓库上的目标分支名
        self.relative_path = args.relative_path  # 需要提交的目标仓库上的相对路径
        self.gerrit_server = args.gerrit_server
        self.alm_id = args.alm_id
        self.change_id = args.change_id
        self.chrome = None
        self.max_timeout = 1800

        if platform.system() == "Linux":
            self.driver_path = r"/usr/bin/chromedriver"
        else: #windows系统调用
            self.driver_path = r"F:\chromedriver-win64\chromedriver.exe" #根据实际情况修改
            self.chrome_path = r"C:\Program Files\Google\Chrome\Application\chrome.exe" #根据实际情况修改

        self.download_path = "download"  # 源码存放路径

        self.dom_if = "const root = arguments[0];root.shadowRoot.querySelectorAll('dom-if').forEach(dom => {dom.if = true;});"
        self.download_click = {
            "build_info_download": "const root = arguments[0];"
                                   "return root.shadowRoot.querySelector('artifact-viewer')"
                                   ".shadowRoot.querySelector('div.artifact-header > div.buttons-container > a > huckle-button')"
                                   ".shadowRoot.querySelector('button').click();",
            "vmlinux_symvers_download": "const root = arguments[0];"
                                        "return root.shadowRoot.querySelector('artifact-viewer')"
                                        ".shadowRoot.querySelector('div > div.buttons-container > a > huckle-button').click();"
        }

    def __del__(self):
        """析构函数确保资源释放"""
        self.close_chrome()

    def initialize_dirpath(self, dirpath):
        if os.path.exists(dirpath):
            print(f"重建路径：{dirpath}")
            shutil.rmtree(dirpath)
            os.mkdir(dirpath)
        else:
            os.mkdir(dirpath)

    def driver_initialize(self):
        options = webdriver.ChromeOptions()
        prefs = {
            "download.default_directory": os.getcwd(),
            "safebrowsing.enabled": False,
        }
        options.add_experimental_option("prefs", prefs)
        if hasattr(self, 'chrome_path'):
            options.binary_location = self.chrome_path
        else:
            pass
        if self.env == "release":
            options.add_argument("--proxy-server=wgw.myoas.com:9090")
            options.add_argument("--headless")
        else:
            options.add_argument("--remote-debugging-port=9222")
        options.add_argument('--no-sandbox')
        options.add_argument('--disable-dev-shm-usage')
        options.add_argument('--disable-gpu')
        options.add_argument(r'--user-data-dir=/home/zxw/gki_codebase/gki/UserData/Default')

        service = Service(executable_path=self.driver_path)

        try:
            self.chrome = webdriver.Chrome(service=service, options=options)
            print("Chrome 启动成功！")
            self.chrome.implicitly_wait(20)
            print(f"Chrome 浏览器版本: {self.chrome.capabilities['browserVersion']}")
            print(f"ChromeDriver 版本: {self.chrome.capabilities['chrome']['chromedriverVersion'].split(' ')[0]}")
        except Exception as e:
            print(f"启动失败: {e}")
            self.close_chrome()
            sys.exit()

    def close_chrome(self):
        if hasattr(self, 'chrome') and self.chrome is not None:
            try:
                self.chrome.quit()
                print("Chrome 已安全关闭")
            except Exception as e:
                print(f"关闭Chrome时出错: {e}")
            finally:
                self.chrome = None  # 确保引用被清除

    def execute(self, command, timeout):
        '''
        执行shell命令
        @param command: shell命令
        @param timeout: 超时时间
        @return:
        '''
        p = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
        timer = Timer(timeout, p.kill, [p])  # 设置定时器去终止这个命令
        try:
            timer.start()
            stdout, stderr = p.communicate()
            return_code = p.returncode
            print(return_code)
            print(stdout)
            if return_code == 0:
                return True
        except Exception as ex:
            print(ex)
        finally:
            timer.cancel()

    def check_download_file(self, f, load=1):
        '''
        @param f:
        @param load:
        @return:
        '''
        time.sleep(int(load))
        file_list = os.listdir(os.getcwd())
        if f in file_list:
            return True
        else:
            return False

    def download_file(self, file_name):
        count = 1
        while (not self.check_download_file(file_name)):
            time.sleep(1)
            count += 1
            if count > self.max_timeout:
                break

    def clear_download_file(self, file):
        '''
        @param file:
        @return:
        '''
        file_list = os.listdir(os.getcwd())
        for f in file_list:
            if re.search(file, f) and os.path.isfile(f):
                os.remove(f)

    def version_compare(self, target_version: int, base_version: int): #target_version >= base_version 返回True
        target_version_list = str(target_version).split('.')
        target_version = int(target_version_list[0])
        target_patchlevel = int(target_version_list[1])

        base_version_list = str(base_version).split('.')
        base_version = int(base_version_list[0])
        base_patchlevel = int(base_version_list[1])

        if target_version > base_version:
            return True
        elif target_version == base_version:
            if target_patchlevel >= base_patchlevel:
                return True
            else:
                return False
        else:
            return False

    def tranfer_gki_image(self):
        print("开始推送源码".center(100, "#"))
        # 清理环境
        if os.system("cd {} && rm -fr *.log".format(workspace)) == 0:
            print("删除上一次运行生成的文件 成功！")
        else:
            print("删除上一次运行生成的文件 失败！")
            sys.exit(1)

        oplus_project_code_path = os.path.join(self.workspace, "oplus_project_code_path")  # 下载目标仓库目标分支代码到本地
        self.initialize_dirpath(oplus_project_code_path)

        source_codes_root_path = os.path.join(self.workspace, "download")
        if os.path.exists(source_codes_root_path):
            paths = [str(path).strip() for path in os.listdir(source_codes_root_path) if len(str(path).strip()) > 0]
            if len(paths) == 1:  # 每次源码下载完成时只会生成一个目录
                img_code_path = os.path.join(source_codes_root_path, str("".join(paths)).strip())
                common_files = [os.path.join(img_code_path, str(file).strip()) for file in os.listdir(img_code_path) if len(str(file).strip()) > 0 and os.path.isfile(os.path.join(img_code_path, str(file).strip()))]
                print("\n需要上传的文件为：\n{}".format(common_files))
            else:
                print("本次升级生成的目标目录不止一个，属于异常情况，退出！")
                sys.exit(1)

            if os.system("git clone ssh://codebase@{}:29418/{} -b {} {} --depth 1 && scp -p -P 29418 codebase@{}:hooks/commit-msg {}/.git/hooks/".format(self.gerrit_server, self.project_name, self.branch_name, oplus_project_code_path, self.gerrit_server, oplus_project_code_path)) == 0:
                print("仓库{}分支{}代码下载成功，6.6已不再上传img等文件至gerrit中，将仅进行jfrog上传！".format(self.project_name, self.branch_name))
                # 创建jfrog上传文件目录
                jfrog_collect_dir = os.path.join(self.workspace, "img_collect", self.gkitag)
                os.system("rm -rf {} && mkdir -p {}".format(jfrog_collect_dir, jfrog_collect_dir))
                # 将相关文件存放到tag目录下，统一推送至jfrog
                for file in common_files:  # 复制公共文件
                    if os.system("cp -fr {} {}".format(file, jfrog_collect_dir)) == 0:
                        print("文件{}复制到{}成功".format(file, jfrog_collect_dir))
                    else:
                        print("文件{}复制到{}失败".format(file, jfrog_collect_dir))
                        sys.exit(1)

                print("jfrog推送目录文件如下:")
                print("*" * 170)
                print(os.listdir(jfrog_collect_dir))
                print("*" * 170)

                # 下载推送工具
                tool_dir = os.path.join(self.workspace, "jfrog_tool")
                jfrog_tool_cmd = "rm -rf {} && git clone ssh://gerrit_url:29418/oplus/kernel/build -b v/qcom/master --depth 1 {}".format(tool_dir, tool_dir)
                print("下载gki jfrog推送工具:", jfrog_tool_cmd)
                os.system(jfrog_tool_cmd)

                # 登录口令，本地存放的登录用户及口令
                with open("/home/segops/work/jfrog_login/login.txt", "r") as f:
                    login_data = json.load(f)
                jfrog_user = login_data.get("user")
                jfrog_passwd = login_data.get("passwd")

                gerrit_push_dir = os.path.join(oplus_project_code_path, self.relative_path)
                push_jfrog_cmd = "python3 -u {}/tools/ogki_gki_artifactory.py -u {} -p {} -t upload -k GKI -i {} -o {}".format(tool_dir, jfrog_user, jfrog_passwd, jfrog_collect_dir, gerrit_push_dir)
                os.system(push_jfrog_cmd)
            else:
                print("仓库{}分支{}代码下载失败！".format(self.project_name, self.branch_name))
                sys.exit(1)

            os.system("cd {} && git add .".format(oplus_project_code_path))
            commit_msg = "[AllawnTech.SCM.codebase][1/1]{{{}}}\n" \
                         "\n" \
                         "适用范围：{{无}}\n" \
                         "准入id：{{{}}}\n" \
                         "分析：{{无}}\n" \
                         "方案：{{\n仓库{}\n分支{}\n路径{}下的img\nTAG:{}\nGoogle链接：\nhttps://android.googlesource.com/kernel/common/+/refs/tags/{}\n}}\n" \
                         "风险及影响[快/稳/省/功能/安全隐私]：{{无}}\n" \
                         "测试建议：{{无}}\n" \
                         "跨组依赖(topic name)：{{无}}".format("更新到{}".format(self.gkitag), self.alm_id, self.project_name, self.branch_name, self.relative_path, self.gkitag, self.gkitag)
            if change_id:
                commit_msg = commit_msg + "\n\nChange-Id: {}".format(self.change_id if self.change_id.startswith("I") else "I" + self.change_id)
            commit_result = os.popen("cd {} && git commit -m {} && git commit --amend --no-edit ".format(oplus_project_code_path, '\'' + commit_msg + '\'')).read()
            if commit_result.find("nothing to commit") != -1:
                print("已更新过源码,不用重复更新")
                sys.exit(0)
            else:
                push_cmd = "cd {} && git push origin HEAD:refs/for/{} > /tmp/push_log.txt 2>&1".format(oplus_project_code_path, self.branch_name)
                if os.system(push_cmd) == 0:
                    with open("/tmp/push_log.txt", "r") as f:
                        push_result = f.read()
                    print("源码推送成功，推送内容为：\n{}".format(push_result))
                    push_url = "\n".join(re.compile("http\S+").findall(push_result))
                    if len(str(push_url).strip()) <= 0:
                        print("获取gerrit审核链接失败,退出！")
                        sys.exit(1)
                    else:
                        with open(os.path.join(self.workspace, "review_url.log"), mode="wt", encoding="utf-8") as fw:
                            fw.write(push_url)
                else:
                    print("源码推送失败")
                    sys.exit(1)
        print("源码推送完成".center(100, "#"))

    def main(self):
        print("开始下载源码".center(100, "#"))
        if self.gkitag:
            pattern = r'\d+\.\d+'  # 用于匹配一个或多个数字，后跟一个点，再后跟一个或多个数字
            result = re.search(pattern, str(self.gkitag).strip())
            if result:
                extracted_number = float(result.group())
                print("kernel version is {}".format(extracted_number))
            else:
                print("参数gkitag异常")
                sys.exit(1)

            if not self.version_compare(extracted_number, 6.6):
                print("该工具仅用于下载kernel版本6.6以上的GKI镜像")
                return

            self.initialize_dirpath(self.download_path)
            self.driver_initialize()

            self.initialize_dirpath(self.gkitag)
            tag_url = "https://android.googlesource.com/kernel/common/+/refs/tags/" + str(self.gkitag).strip()
            self.chrome.get(tag_url)
            self.chrome.maximize_window()
            self.chrome.refresh()
            herf = self.chrome.find_element(By.XPATH, "/html/body/div/div/pre[1]/a[1]").text

            vmlinux_url = herf + "/vmlinux"
            vmlinux_symvers_url = herf + "/vmlinux.symvers"
            BUILD_INFO_url = herf + "/BUILD_INFO"

            ####下载BUILD_INFO#############
            self.chrome.get(BUILD_INFO_url)
            time.sleep(2)
            host = self.chrome.find_element(By.XPATH, "//*[@id='artifact_view_page']")
            self.chrome.execute_script(self.dom_if, host)
            self.chrome.execute_script(self.download_click["build_info_download"], host)


            ####下载vmlinux#############
            self.chrome.get(vmlinux_url)
            self.download_file("vmlinux")

            ####下载vmlinux.symvers#############
            self.chrome.get(vmlinux_symvers_url)
            time.sleep(2)
            host = self.chrome.find_element(By.XPATH, "//*[@id='artifact_view_page']")
            self.chrome.execute_script(self.dom_if, host)
            self.chrome.execute_script(self.download_click["vmlinux_symvers_download"], host)
            self.download_file("vmlinux.txt")
            if os.path.exists("vmlinux.txt"):
                os.rename("vmlinux.txt", "vmlinux.symvers")

            ####下载vmlinux.symvers#############
            img_list = ["system_dlkm.flatten.erofs.img", "system_dlkm.flatten.ext4.img"]
            for img in img_list:
                img_url = herf + '/' +  img # herf + "/" + img
                self.chrome.get(img_url)
                self.download_file(img)
                if os.path.exists(img):
                    shutil.move(img, self.gkitag)
                    os.rename(os.path.join(self.gkitag, img), os.path.join(self.gkitag, "system_dlkm.img"))
                    break

            ####下载认证了的boot-img##########
            _build_id = re.search("submitted/(\d+)", herf).group(1)
            img_url = herf + "/signed%2Fcertified-boot-img-{}.tar.gz".format(_build_id)
            self.chrome.get(img_url)
            boot_file_name = "certified-boot-img-{}.tar.gz".format(_build_id)
            self.download_file(boot_file_name)
            mtk_cp_cmd = "tar -zxf {} --overwrite -C ./{}".format(boot_file_name, self.gkitag)
            self.execute(mtk_cp_cmd, self.max_timeout)
            self.clear_download_file(boot_file_name)

            shutil.move("BUILD_INFO.txt", self.gkitag)
            self.clear_download_file("BUILD_INFO.txt")
            shutil.move("vmlinux.symvers", self.gkitag)
            self.clear_download_file("vmlinux.symvers")
            shutil.move("vmlinux", self.gkitag)
            self.clear_download_file("vmlinux")
            shutil.move(self.gkitag, self.download_path)  # driver.quit()

            self.close_chrome()
        print("下载源码完成".center(100, "#"))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='manual to this script')
    parser.add_argument('--gkitag', type=str, help='android kernel common refs tag android for mtk', default=None)
    parser.add_argument('--kernelbuildid', type=str, help='kernel id for qualcomm', default=os.environ.get("kernelbuildid"))
    parser.add_argument('--androidbuildid', type=str, help='boot.img id for qualcomm', default=os.environ.get("androidbuildid"))
    parser.add_argument('--env', type=str, help='tool running type', default='release')
    parser.add_argument('--workspace', type=str, help='Import_GKI_Codes WarkSpace', os.environ.get("WORKSPACE"))
    parser.add_argument('--project_name', type=str, help='aosp_gki project_name', default=os.environ.get("project_name"))
    parser.add_argument('--branch_name', type=str, help='aosp_gki branch', default=os.environ.get("branch_name"))
    parser.add_argument('--relative_path', type=str, help='aosp gki relative_path', default=str(os.environ.get("relative_path")).strip().rstrip("/").lstrip("/"))
    parser.add_argument('--gerrit_server', type=str, default=os.environ.get("gerrit_server"))
    parser.add_argument('--alm_id', type=str, default=os.environ.get("alm_id", "6161412"))
    parser.add_argument('--change_id', type=str, default=os.environ.get("change_id", ""))
    args = parser.parse_args()

    import_gki_codes = gki_codes(args)
    import_gki_codes.main()
    import_gki_codes.close_chrome()
    import_gki_codes.tranfer_gki_image()
