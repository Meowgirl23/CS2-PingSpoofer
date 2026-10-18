# 构建教程

## 环境要求

- Windows 10 / 11，x64。
- [Zig 0.15.2](https://ziglang.org/download/0.15.2/zig-x86_64-windows-0.15.2.zip)，选择 Windows x86_64 版本并完整解压。此版本已用于本项目的构建测试。
- Git；也可以通过仓库页面的 **Code → Download ZIP** 下载源码。

使用 Zig 自带的 C/C++ 编译工具链，不需要额外安装 Visual Studio 或 .NET。请保留 Zig 解压目录中的 `lib` 等文件，不要只复制 `zig.exe`。

## 1. 获取源码

打开 PowerShell，执行：

```powershell
git clone https://github.com/Meowgirl23/CS2-PingSpoofer.git
Set-Location .\CS2-PingSpoofer
```

如果下载的是 ZIP，先完整解压，再在包含 `build.ps1` 的目录中打开 PowerShell。

## 2. 指定 Zig 路径

下面假设你将 Zig 解压到了 `C:\Tools\zig`，请按实际位置修改：

```powershell
$Zig = 'C:\Tools\zig\zig.exe'
& $Zig version
```

预期输出 `0.15.2`。后续命令在同一个 PowerShell 窗口中执行。

## 3. 构建 DLL

在仓库根目录执行：

```powershell
& .\build.ps1 -Zig $Zig
```

完成后，仓库根目录会生成：

```text
fakelatency.dll
```

中间目标文件位于 `build` 目录。构建过程不会启动 CS2，也不会执行注入。

如果 `zig` 已加入 PATH，也可以直接执行：

```powershell
& .\build.ps1
```

## 4. 运行本地测试

在仓库根目录执行：

```powershell
& .\tests\run.ps1 -Zig $Zig
```

脚本会重新构建 DLL，并在 `tests\bin` 中生成测试程序和模拟模块。测试覆盖指令解析、ACK 字段处理、调用参数转发、开关恢复、并发卸载和版本不匹配处理。

当前测试通过时输出：

```text
PASS 6401; real_game_tested=false
```

这些测试不需要启动 CS2，也不会向真实游戏进程注入。`tests\bin` 中的 DLL 仅供测试，请勿把它们复制进游戏目录。测试通过不代表真实服务器会采纳修改后的延迟，也不代表其他玩家看到的 Ping 已验证。

## 常见问题

- **找不到 Zig**：检查 `$Zig` 是否指向实际的 `zig.exe`，并确认整个解压目录完整。
- **找不到 build.ps1**：确认当前目录是仓库根目录，而不是 `src` 或 `tests`。
- **脚本运行受限**：检查下载文件是否带有来源标记，并遵循设备的 PowerShell 执行策略；受组织管理的设备请联系管理员。
- **输出文件被占用**：先关闭占用该文件的程序或卸载已加载的 DLL，再重新构建。
- **构建成功但加载后退出**：正式 DLL 校验 `steamnetworkingsockets.dll` 的 SHA-256。版本不匹配时会静默退出，重新构建本身不会更新该校验值。

## 文件说明

| 路径 | 内容 |
| --- | --- |
| `src/fakelatency.cpp` | DLL 初始化、网络处理和控制指令 |
| `src/logic.h` | 指令解析和 ACK 字段处理 |
| `build.ps1` | DLL 构建脚本 |
| `tests` | 本地测试及模拟模块 |
| `third_party/minhook` | MinHook 依赖及其许可证 |

第三方依赖已随源码提供，无需另行下载。分发源码或二进制时，请保留对应的第三方许可证。构建产物已通过 `.gitignore` 排除。
