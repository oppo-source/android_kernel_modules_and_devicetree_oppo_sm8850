# -*- coding: utf-8 -*-
import json

def clean_string(s):
    """清理字符串两端的引号和逗号"""
    s = s.strip()
    if s.startswith('"'):
        s = s[1:]
    if s.endswith('",'):
        s = s[:-2]
    elif s.endswith('"'):
        s = s[:-1]
    return s

def handle_json_special_chars(text, escape=True):
    """
    处理JSON特殊字符
    :param text: 要处理的文本
    :param escape: True表示转义(适合JSON解析)，False表示反转义(还原可读格式)
    :return: 处理后的文本
    """
    char_pairs = [
        ('"', r'\"'),
        ('\n', r'\n'),
        ('\t', r'\t'),
        ('\r', r'\r')
    ]

    for orig, escaped in char_pairs:
        if escape:
            text = text.replace(orig, escaped)
        else:
            text = text.replace(escaped, orig)
    return text


def parse_json_safely(json_text):
    """安全解析JSON文本"""
    try:
        data = json.loads(json_text)
        #print("直接解析成功:")
        #print(json.dumps(data, indent=2, ensure_ascii=False))
        return data
    except json.JSONDecodeError as e:
        print(f"{json_text}\n直接解析失败")
        print(f"解析失败原因: {e}")
        print("尝试使用修复中:")
        return parse_json_fields(json_text)


def parse_json_field(json_text, field_name, next_field, pattern="split"):
    """解析JSON中的单个字段（增强容错版）

    Args:
        json_text: 要解析的JSON文本
        field_name: 要提取的字段名（需包含引号和冒号，如'"field":'）
        next_field: 下一个字段名或结束标记
        pattern:
            "split" - 从field_name到next_field之间的内容
            "rsplit" - 从field_name到最后一个next_field之间的内容（不存在则取到文本结束）

    Returns:
        解析成功返回字段内容字符串，失败返回"未知"
    """
    try:
        # 1. 安全获取field_name之后的内容
        field_parts = json_text.split(field_name, 1)
        if len(field_parts) < 2:
            print(f"警告：未找到字段 {field_name}")
            return "未知"

        remaining_text = field_parts[1]

        # 2. 根据模式提取内容（带容错）
        try:
            if pattern == "rsplit":
                # 安全处理rsplit：如果next_field不存在则取全部内容
                split_parts = remaining_text.rsplit(next_field, 1)
                content = split_parts[0] if len(split_parts) > 1 else remaining_text
            else:
                # 普通split模式
                split_parts = remaining_text.split(next_field, 1)
                content = split_parts[0] if len(split_parts) > 1 else remaining_text
        except Exception as e:
            print(f"内容分割出错: {e}")
            content = remaining_text  # 分割失败时使用剩余文本作为默认值

        # 3. 清理和转义内容
        cleaned_content = clean_string(content) if content else ""

        # 4. 尝试构建临时JSON验证（带容错）
        try:
            escaped_content = handle_json_special_chars(cleaned_content, escape=True)
            temp_json_str = f'{{{field_name}"{escaped_content}"}}'
            field_data = json.loads(temp_json_str)
            print(f"\n{field_name}解析成功:")
            print(json.dumps(field_data, indent=2, ensure_ascii=False))
            return escaped_content
        except json.JSONDecodeError as e:
            print(f"\n{field_name} JSON验证失败（仍返回内容）: {e}")
            return cleaned_content  # 验证失败仍返回清理后的内容

    except Exception as e:
        print(f"\n{field_name}处理过程中发生未预期错误: {e}")
        return "未知"  # 最终兜底


def parse_json_fields(json_text):
    """分段解析JSON字段并重组"""
    fields = {
        "risk_level": ('"risk_level":', '"module":'),
        "module": ('"module":', '"analysis":'),
        "analysis": ('"analysis":', '"summary":'),
        "summary": ('"summary":', '"test_recommendations":'),
        "test_recommendations": ('"test_recommendations":', '}', 'rsplit')
    }

    result = {}
    for module, params in fields.items():
        result[module] = parse_json_field(json_text, *params)

    print("\n重组后的JSON内容:")
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return result


def parse_json_demo():
    # 示例JSON文本
    json_text_local = '''{
    "risk_level": "低",
    "module": "内存管理",
    "analysis": "",
    "summary": "",
    "test_recommendations": ""
    }'''

    # 执行解析
    reconstructed_json_data = parse_json_safely(json_text_local)

    # 格式化输出
    print("\n=== 格式化输出 ===")
    for field, value in reconstructed_json_data.items():
        print(f"\n{field}:\n{handle_json_special_chars(value, escape=False)}")