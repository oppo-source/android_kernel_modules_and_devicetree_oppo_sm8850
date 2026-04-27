import requests
import argparse
import hashlib
import json
import time
import random
import hmac
from hashlib import sha1
import base64
import api

env = "sandbox"
# env = "release"

def get_environment_config(args, env_type, system, path=''):
    config = api.CONFIG.get(env_type, api.CONFIG["default"])
    mtp_paths = api.MTP_PATHS.get("common", api.MTP_PATHS["default"])

    operation_config = config.get(system, config.get("mtp", {}))

    args.host = operation_config.get("host", "default-host")
    args.app_id = operation_config.get("app_id", "default-app-id")
    args.app_secret = operation_config.get("app_secret", "default-app-secret")
    args.secret = operation_config.get("secret", "default-secret")
    args.pub = operation_config.get("pub", "default-pub")
    args.method = "POST"
    args.path = mtp_paths.get(path, 'default-path')
    args.url_send = "https://{}{}".format(args.host, args.path)
    return {
        "mtp": config.get("mtp", {}),
        "ipd-ppm": config.get("ipd-ppm", {})
    }


def hash_hmac(secret, plain_text):
    hmac_code = hmac.new(secret.encode(), plain_text.encode(), sha1).digest()
    return base64.b64encode(hmac_code).decode()


def create_to_content(receive_id, receive_type):
    return {
        "receiveId": receive_id,
        "receiveType": receive_type
    }


def create_request(msg_content, from_content, to_content, msg_type):
    return {
        "msg": msg_content,
        "from": from_content,
        "to": to_content,
        "type": msg_type
    }


def generate_message(user_ids=None, text_content="hello world"):
    # Base message content
    base_message = {
        "tag": "text",
        "text": text_content
    }
    if user_ids and user_ids != []:
        # Dynamically generate @ user parts
        at_users = [
            {
                "tag": "at",
                "userId": user_id
            }
            for user_id in user_ids
        ]
        # Combine message content
        message_content = {
            "content": [
                [base_message] + at_users
            ]
        }
    else:
        # If user_ids is empty or not specified, generate base message content
        message_content = base_message
    return message_content


def get_headers_content(args):
    timestamp = int(time.time())
    nonce = str(random.randint(100000, 999999))
    sign_string = "{}&{}&{}&{}&{}&{}".format(args.method, args.host, args.path, args.app_id, timestamp, nonce)
    sign = hash_hmac(args.app_secret, sign_string)
    timestr = str(int(round(time.time() * 1000)))
    list_data = []
    list_data.append(timestr)
    list_data.append(str(nonce))
    list_data.append(str(args.secret))
    list_data.append(str(args.pub))
    list_data.sort()
    str_data = ''.join(list_data)
    pubtoken = hashlib.sha256(str_data.encode('utf-8')).hexdigest()

    str_timestamp = str(timestamp)
    headers = {
        'Content-type': 'application/json',
        'appId': args.app_id,
        'sign': sign,
        'timestamp': str_timestamp,
        'signversion': '2.0.0',
        'host': args.host,
        'nonce': nonce
    }

    fromcontent = {
        "pub": args.pub,
        "time": timestr,
        "nonce": nonce,
        "pubtoken": pubtoken
    }
    return fromcontent, headers


def create_group_request(robot):
    req = {
        "operator": robot
    }
    return req


def create_projectnumber_request(project_number):
    req = {
        "projectNumber": "{}".format(project_number)
    }
    return req


def create_version_plan_request(project_code, project_name):
    req = {
        "current": 1,
        "projectName": "{}".format(project_name),
        "projectCode": "{}".format(project_code),
        "size": 1
    }
    return req


def create_pm_cmo_request(project_code):
    req = {
        "roleCodeList": ["SOFTWARE_PM", "SETTINGS_ENGINEERING"],
        "projectCode": "{}".format(project_code),
        "size": 1000
    }
    return req


def extract_group_info(result_json):
    group_info = []
    if result_json.get("success") and result_json.get("data"):
        group_list = result_json["data"].get("groupList", [])
        for group in group_list:
            group_info.append({
                "groupName": group.get("groupName", "0"),
                "groupId": group.get("groupId", "0"),
                "headerUrl": group.get("headerUrl", "0"),
                "createDate": group.get("createDate", 0)
            })
    return group_info


def create_group_user_content(user, robot):
    req = {
        "groupId": user,
        "operator": robot
    }
    return req


def create_add_user_to_group_content(group, user_id, robot):
    req = {
        "groupId": group,
        "members": [
            user_id
        ],
        "robots": [
            robot
        ],
        "operator": robot
    }
    return req


def parse_project_data(json_data):
    """
    解析传入的 JSON 数据，提取 projectCode 和 projectName 的值。

    :param json_data: 传入的 JSON 数据
    :return: projectCode 和 projectName 的值
    """
    try:
        # 将 JSON 字符串解析为 Python 对象
        data = json.loads(json_data)

        # 检查 data 是否包含 'data' 键
        if 'data' in data and data['data']:
            # 获取第一个项目的 projectCode 和 projectName
            project = data['data'][0]
            project_code = project.get('projectCode')
            project_name = project.get('projectName')

            # 返回 projectCode 和 projectName
            return project_code, project_name
        else:
            return '0', '0'
    except json.JSONDecodeError as e:
        print(f"JSON 解析错误: {e}")
        return '0', '0'
    except Exception as e:
        print(f"发生错误: {e}")
        return '0', '0'


def parse_create_user_number(json_data):
    """
    解析传入的 JSON 数据，提取所有记录的 createUserNo 的值。

    :param json_data: 传入的 JSON 数据
    :return: 一个包含所有 createUserNo 的列表
    """
    try:
        # 将 JSON 字符串解析为 Python 对象
        data = json.loads(json_data)

        # 检查 data 是否包含 'data' 和 'records' 键
        if 'data' in data and 'records' in data['data']:
            records_list = data['data']['records']

            # 提取所有记录的 createUserNo
            create_user_no = [record.get('createUserNo') for record in records_list if record.get('createUserNo')]

            # 返回 un_list
            return create_user_no
        else:
            return []
    except json.JSONDecodeError as e:
        print(f"JSON 解析错误: {e}")
        return []
    except Exception as e:
        print(f"发生错误: {e}")
        return []


def parse_pm_cmo_data(json_data):
    """
    解析传入的 JSON 数据，提取 SOFTWARE_PM 和 SETTINGS_ENGINEERING 的 userNo 列表。

    :param json_data: 传入的 JSON 数据
    :return: (pm_list, cmo_list) 两个列表，分别包含 SOFTWARE_PM 和 SETTINGS_ENGINEERING 的 userNo
    """
    try:
        # 将 JSON 字符串解析为 Python 对象
        data = json.loads(json_data)

        # 初始化列表
        pm_list = []
        cmo_list = []

        # 检查 data 是否包含 'data' 和 'records' 键
        if 'data' in data and 'records' in data['data']:
            records = data['data']['records']

            # 遍历 records 列表
            for record in records:
                role_code = record.get('roleCode')
                user_list = record.get('userList', [])

                if role_code == "SOFTWARE_PM":
                    pm_list.extend([user.get('userNo') for user in user_list])
                elif role_code == "SETTINGS_ENGINEERING":
                    cmo_list.extend([user.get('userNo') for user in user_list])

        # 返回 pm_list 和 cmo_list
        return pm_list, cmo_list
    except json.JSONDecodeError as e:
        print(f"JSON 解析错误: {e}")
        return [], []
    except Exception as e:
        print(f"发生错误: {e}")
        return [], []


def extract_user_codes(json_data):
    """
    解析 JSON 数据并提取 groupUsers 中所有 userCode 的值。

    :param json_data: JSON 数据
    :return: userCode 列表
    """
    # 解析 JSON 数据
    data = json.loads(json_data)

    # 提取 groupUsers 中的 userCode
    user_codes = []
    for user in data.get('data', {}).get('groupUsers', []):
        user_codes.append(user.get('userCode'))

    return user_codes


def get_project_code(args, project_number):
    get_environment_config(args, args.type, "mtp", 'getProjectDhInfo')
    fromcontent, headers = get_headers_content(args)
    req = create_projectnumber_request(project_number)
    payload = json.dumps(req)
    result = requests.post(args.url_send, headers=headers, data=payload)
    result = result.content.decode('utf8')
    project_code, project_name = parse_project_data(result)
    return project_code, project_name


def get_version_plan_list(args, project_code, project_name):
    get_environment_config(args, args.type, "mtp", 'getVersionPlanList')
    fromcontent, headers = get_headers_content(args)
    req = create_version_plan_request(project_code, project_name)
    payload = json.dumps(req)
    result = requests.post(args.url_send, headers=headers, data=payload)
    result = result.content.decode('utf8')
    create_user_no = parse_create_user_number(result)
    return create_user_no


def get_version_pm_cmo_list(args, project_code):
    get_environment_config(args, args.type, "mtp", 'getProjectTeamPage')
    fromcontent, headers = get_headers_content(args)
    req = create_pm_cmo_request(project_code)
    payload = json.dumps(req)
    result = requests.post(args.url_send, headers=headers, data=payload)
    result = result.content.decode('utf8')
    pm_list, cmo_list = parse_pm_cmo_data(result)
    return pm_list, cmo_list


def get_user_id_info_from_sever(args, project_number):
    project_code, project_name = get_project_code(args, project_number)
    #print("project_code {} project_name {}".format(project_code, project_name))
    create_user_no = get_version_plan_list(args, project_code, project_name)
    pm_list, cmo_list = get_version_pm_cmo_list(args, project_code)
    data_dict = {
                 "PROJECT_NAME": project_name,
                 "createUserNo": create_user_no,
                 "SOFTWARE_PM": pm_list,
                 "CMO": cmo_list
                }
    print(json.dumps(data_dict))
    return data_dict


def get_tt_group_id(args):
    get_environment_config(args, args.type, "mtp", 'getInfoByRobotId')
    fromcontent, headers = get_headers_content(args)
    req = create_group_request(args.pub)
    payload = json.dumps(req)
    result = requests.post(args.url_send, headers=headers, data=payload)
    result = result.content.decode('utf8')
    result_json = json.loads(result)
    group_info = extract_group_info(result_json)
    return group_info


def send_tt_group_at_message(args, user_ids, user):
    get_environment_config(args, args.type, "mtp", 'send')
    fromcontent, headers = get_headers_content(args)
    msgcontent = generate_message(user_ids, "文本测试CC")
    tocontent = create_to_content(user, 2)  # send to group 2
    req = create_request(msgcontent, fromcontent, tocontent, 23)  # other messsage is 23
    payload = json.dumps(req)
    print(payload)
    result = requests.post(args.url_send, headers=headers, data=payload)


def add_user_to_tt_group(args, user_id, group):
    get_environment_config(args, args.type, "mtp", 'addUser')
    fromcontent, headers = get_headers_content(args)
    req = create_add_user_to_group_content(group, user_id, args.pub)
    payload = json.dumps(req)
    result = requests.post(args.url_send, headers=headers, data=payload)


def get_tt_group_user(args, user_ids, user):
    get_environment_config(args, args.type, "mtp", 'info')
    fromcontent, headers = get_headers_content(args)
    req = create_group_user_content(user, args.pub)
    payload = json.dumps(req)
    result = requests.post(args.url_send, headers=headers, data=payload)
    result = result.content.decode('utf8')
    # 调用函数提取 userCode
    user_codes = extract_user_codes(result)
    return user_codes


def send_tt_normal_message(args, user, msg_type=1, text_content="hello world"):
    if not user or user == '0':
        print("Error: Invalid user. User must be a non-empty string and not '0'.")
        return
    get_environment_config(args, args.type, "mtp", 'send')
    fromcontent, headers = get_headers_content(args)

    msgcontent = generate_message([], text_content)
    tocontent = create_to_content(user, msg_type)  # 推送给人员
    req = create_request(msgcontent, fromcontent, tocontent, 2)  # 文本消息
    payload = json.dumps(req)
    # print(args.url_send)
    # print(headers)
    # print(payload)
    result = requests.post(args.url_send, headers=headers, data=payload)
    result = result.content
    # print(result.decode('utf8'))


def send_tt_message_demo(args):
    user_ids = ["all"]
    new_user_code = "0"
    project_code = "0"
    group_info = get_tt_group_id(args)
    for group in group_info:
        group_id = group["groupId"]
        print("groupId:", group_id)

        user_codes = get_tt_group_user(args, user_ids, group_id)
        if new_user_code not in user_codes:
            print("add user_code:", new_user_code)
            add_user_to_tt_group(args, new_user_code, group_id)

        send_tt_group_at_message(args, user_ids, group_id)
        send_tt_normal_message(args, group_id, 2)
        print("headerUrl:", group["headerUrl"])
        print("createDate:", group["createDate"])

    get_user_id_info_from_sever(args, project_code)
    send_tt_normal_message(args, new_user_code, 1)


def send_tt_message_cmd(args):
    group_ids_lists = []
    group_info = get_tt_group_id(args)
    for group in group_info:
        group_id = group["groupId"]
        group_ids_lists.append(group_id)

    # 将逗号分隔的字符串转换为列表
    try:
        users_list = args.users.split(',')
    except Exception as e:
        print("Error splitting users: {}".format(e))
        return

    # 去除列表中每个元素的首尾空白字符
    users_list = [user.strip() for user in users_list]

    for user_id in users_list:
        send_tt_normal_message(args, user_id, 1, args.message)
        for group in group_info:
            group_id = group["groupId"]
            user_codes = get_tt_group_user(args, user_id, group_id)
            if user_id not in user_codes:
                add_user_to_tt_group(args, user_id, group_id)

    for group in group_info:
        group_id = group["groupId"]
        send_tt_normal_message(args, group_id, 2, args.message)


def parse_cmd_args():
    parser = argparse.ArgumentParser(description="ACK LTS Teamtalk message route")
    parser.add_argument('--type', type=str, help='Type of build to send or check (default: sandbox)', default="sandbox")
    parser.add_argument('--method', type=str, help='Method to use for the operation (default: None)', default="")
    parser.add_argument('--host', type=str, help='Host directory for unpacking (default: None)', default="")
    parser.add_argument('--path', type=str, help='Path to the kernel directory (default: None)', default="")
    parser.add_argument('--url_send', type=str, help='URL to send data (default: None)', default="")
    parser.add_argument('--app_id', type=str, help='Application ID (default: None)', default="")
    parser.add_argument('--app_secret', type=str, help='Application secret (default: None)', default="")
    parser.add_argument('--pub', type=str, help='Public key (default: None)', default="")
    parser.add_argument('--secret', type=str, help='Secret key (default: None)', default="")
    parser.add_argument('--message', type=str, help='Message to send (default: "hello world")', default="hello world")
    parser.add_argument('--cmd', type=str, help='Command to execute (default: "send")', default="send")
    parser.add_argument('--project', type=str, help='Project name (default: None)', default="")
    parser.add_argument('--users', type=str, help='Users to notify (default: None)', default="")

    args = parser.parse_args()
    if args.cmd != "get":
        for key, value in vars(args).items():
            print(f"{key}: {value}")
    return args


def main():
    args = parse_cmd_args()
    if args.cmd == "get":
        get_user_id_info_from_sever(args, args.project)
    elif args.cmd == "send":
        # send_tt_message_demo(args)
        send_tt_message_cmd(args)


if __name__ == "__main__":

    try:
        main()
    except SystemExit as e:
        SystemExit("system error force exit.")
    except Exception as e:
        print(f"Error {e}:")
