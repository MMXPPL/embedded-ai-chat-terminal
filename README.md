# 嵌入式 AI 聊天终端

基于 LVGL v8.2.0 与嵌入式 Linux（GEC6818 / S5P6818）的本地聊天终端，集成 DeepSeek API 实现智能对话。

## 功能
- LVGL 图形界面：登录、聊天、背景图与界面素材
- 通过 libcurl 调用 DeepSeek chat/completions 接口
- 人脸识别/摄像头相关外设驱动（见源码目录）

## 目录结构
- `文档/` — 项目文档、总结与验收报告（docx）
- `源码/` — LVGL 工程源码（已剔除 build 构建产物）
- `演示资料/` — 演示视频与截图
- `素材图片/` — UI 背景图资源

## 运行前配置（重要）
源码中的 API Key 已脱敏为占位符。运行前请将 `源码/lv_port_linux_v8.2/my_scr/my_ai_project.c`
中的 `YOUR_DEEPSEEK_API_KEY` 替换为你自己的密钥，或改为从环境变量读取。

**切勿将真实 API Key 提交到任何公开仓库。**

## 构建
在 Ubuntu 环境下使用 CMake 构建，交叉编译时替换为 ARM 工具链。
