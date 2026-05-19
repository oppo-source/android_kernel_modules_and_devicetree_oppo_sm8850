---
name: GrabCodeDiff
description: 触发指令 @GrabCodeDiff 时，使用 "git diff origin/b/master HEAD > last_push_diff.patch" 命令提取最新的 git push 内容。
---

## 一、Skill 核心定位
使用 git diff 命令提取代码仓库最新一次 git push 内容，作为代码审查的内容。

## 二、触发条件
用户输入触发指令：@GrabCodeDiff

## 三、工作流程
使用 "git diff origin/master HEAD > last_push_diff.patch" 命令提取最新的 git push 内容。


