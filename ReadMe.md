# NanaZip Rebuild · 重制版

面向 Windows 日常文件整理的 NanaZip 个人重制版。在 NanaZip 压缩与解压功能的基础上，增强资源管理器右键菜单，加入批量重命名、文件归类和批量新建文件夹等工具。

本项目由 [leaping365](https://github.com/leaping365) 维护，基于 [M2Team/NanaZip](https://github.com/M2Team/NanaZip) 修改，是独立衍生项目。原项目介绍与相关信息保留在 [上游 README](ReadMe.Upstream.md)。

## 重制版功能

### 批量重命名

在资源管理器中选中文件或文件夹，通过 NanaZip 右键菜单进入重命名工具。

- **按规则改名**：添加前后缀、更换前缀、替换或删除文字、自动编号、截取首尾字符、添加日期、整理空白与调整英文大小写。
- **按名单改名**：粘贴或导入 TXT 名单，一行对应一个项目；支持调整预览顺序。
- **常用名称与规则**：保存常用配置，支持搜索、编辑、排序，并可显示在右键快捷菜单中。
- **先预览再执行**：显示新旧名称，校验非法名称、重复目标和已有文件冲突；保留最近一次成功操作的撤销记录。
- **保留文件扩展名**：文件仅修改主名称，文件夹按完整名称处理。

详细操作、名单编码、校验规则与限制见 [批量重命名说明](Documents/Renaming.md)。

### 文件整理与右键菜单

- 增加“文件归入文件夹”工具。
- 增加“批量新建文件夹”和“以日期新建文件夹”入口。
- 调整重命名相关菜单的分组与顺序，便于从资源管理器直接操作。
- 改进自定义窗口的字体、DPI 缩放、尺寸调整与前台激活行为。

### 压缩与解压

沿用 NanaZip 的压缩、解压、文件管理及资源管理器集成能力；本仓库的主要定制集中在右键菜单和文件整理体验。

## 使用方式

1. 安装此重制版构建产物。
2. 在资源管理器中选中文件或文件夹，打开 NanaZip 右键子菜单。
3. 选择重命名或文件整理工具，按窗口提示操作；重命名前检查预览并确认。

本仓库上传了源代码与安装脚本，源码 ZIP 不能直接作为安装程序运行。是否有可下载的成品，请查看本仓库的 [Releases](https://github.com/leaping365/nanazip-rebuild/releases)；上游 NanaZip 的安装包不包含本项目的定制功能。

## 开发与打包

- 这是 Windows 原生 C++ 项目，解决方案入口为 `NanaZip.slnx`，需要配套的 Visual Studio C++ 构建环境、Windows SDK 及项目依赖。
- `Installer/` 包含中文安装程序、证书信任与右键菜单注册脚本，当前安装脚本面向 x64。
- `Installer/BuildInstaller.ps1` 用于基于现有 MSIX、更新后的二进制文件和本机签名证书生成安装产物。它还依赖 Windows SDK 工具和 Inno Setup 6；仅克隆源码不能直接完成签名打包，仓库中的 `.cer` 公钥证书不包含签名私钥。
- 批量重命名的开发验证入口为 `Tests/BuildRenameTests.cmd`，用法见 [开发验证说明](Documents/Renaming.md#开发验证)。

## 文档与反馈

- [批量重命名说明](Documents/Renaming.md)
- [上游项目介绍](ReadMe.Upstream.md)
- [许可证](License.md)
- [上游同步记录](Documents/UpstreamSynchronization.md)
- [提交本重制版的问题与建议](https://github.com/leaping365/nanazip-rebuild/issues)

## 致谢与许可

感谢 NanaZip、7-Zip 及相关开源项目的作者和贡献者。本重制版保留上游版权与许可信息；使用、修改和分发请遵守 [License.md](License.md) 以及各组件的许可证。
