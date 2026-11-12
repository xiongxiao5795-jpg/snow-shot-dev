# Snow Shot · macOS 快速标注版

[English](README.en.md) · 简体中文

本仓库 fork 自 [Snow Shot](https://github.com/mg-chao/snow-shot)，在原有截图、录屏等功能上增加 **快速标注**：无需截图，直接在当前屏幕上画矩形、箭头或自由笔迹，用于会议、教学和演示。

## 为什么维护这个版本

我曾就快速标注与原作者讨论。在我的感受中，对方缺少耐心理解使用场景和已经高度凝练的需求描述，对我用AI调研、总结、包括我个人回答的内容嗤之以鼻，沟通效果不佳；完整沟通过程见 [原讨论 #2494](https://github.com/mg-chao/snow-shot/issues/2494)。
因此我选择自己维护一个 macOS 自用版本。感谢原项目及其贡献者提供的基础工作。

本仓库**只维护 macOS 上与快速标注相关的功能**。我没有其他平台的设备，无法验证或满足 Windows、其他平台及其他功能需求，敬请见谅。有同样需求的朋友欢迎自取。

## 功能演示

以下演示按录制时间排列，依次展示截图标注使用问题、快速标注的基础操作和常驻模式。

### 截图标注使用问题

![截图标注使用问题演示](docs/quick-annotation-demos/screenshot-annotation-issue.gif)

### 快速标注基础演示

![快速标注基础演示](docs/quick-annotation-demos/quick-annotation-basics.gif)

### 快速标注常驻演示

![快速标注常驻演示](docs/quick-annotation-demos/quick-annotation-persistent.gif)

## 下载和使用

在本仓库的 [Releases](https://github.com/xiongxiao5795-jpg/snow-shot-dev/releases) 下载 macOS Apple Silicon 完整版 DMG。原版及其他平台的发行包请前往 [Snow Shot 上游 Releases](https://github.com/mg-chao/snow-shot/releases)；原版不包含本 fork 的快速标注功能。

- 按 `Control+4` 进入快速标注，或按住 `Fn` 临时标注；可在「设置 → 全局快捷键 → 快速标注」修改。
- 默认工具为圆角矩形，也可在工具栏选择矩形、箭头或画笔并调整样式。
- 默认标注短暂停留后淡出；工具栏可选择持续绘制并自动淡出，或保留标注并使用撤销、重做。
- 颜色、粗细、停留和淡出时间以及工具栏布局可在「设置 → 快速标注」调整。

按住 `Fn` 是否与输入法或系统功能冲突，取决于 macOS 设置和键盘。首次使用时请按系统提示授予所需权限。安装与签名注意事项见 [fork 维护说明](docs/fork-quick-annotation.md)。

## 维护和许可

快速标注默认不参与普通构建，本仓库的 macOS Full DMG 构建显式启用它。构建要求及同步上游的方式见 [维护说明](docs/fork-quick-annotation.md)；原项目使用说明见 [Snow Shot 上游](https://github.com/mg-chao/snow-shot)。

本仓库保留上游各组件的许可证与版权说明；Snow Shot 应用遵循 [GPL-3.0-or-later](snow_shot/COPYRIGHT)。
