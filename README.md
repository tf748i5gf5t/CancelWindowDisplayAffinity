# CancelWindowDisplayAffinity

移除 Windows 窗口的 `SetWindowDisplayAffinity` 屏幕捕获保护。

## 原理

通过 DLL 注入 + [MinHook](https://github.com/TsudaKageyu/minhook) Inline Hook，直接拦截目标进程对 `SetWindowDisplayAffinity` 和 `GetWindowDisplayAffinity` 的调用：

| Hook 函数 | 行为 |
|-----------|------|
| `SetWindowDisplayAffinity` | 拦截调用 → 强制参数为 `WDA_NONE` → App 无法设置保护 |
| `GetWindowDisplayAffinity` | 拦截调用 → 返回伪造值 → App 以为保护仍生效 |

- **零 CPU 轮询**：事件驱动，仅在 API 被调用时触发
- **零响应延迟**：调用时即拦截，无竞态窗口
- **高隐蔽性**：双函数 Hook，App 无感知

## 架构

```
┌─────────────────────────────────────────────────────────────────┐
│  CancelWindowDisplayAffinity.exe（单文件）                       │
│  ├─ 嵌入 HookAffinity32.dll（Win32 资源）                       │
│  └─ 嵌入 HookAffinity64.dll（Win32 资源）                       │
│                                                                 │
│  运行时：                                                        │
│  1. 管理员提权                                                   │
│  2. 枚举所有设置了 Display Affinity 的窗口                        │
│  3. 释放 DLL 到 %TEMP%                                          │
│  4. 注入目标进程（64位直接注入 / 32位跨架构 PE 导出表解析注入）     │
│  5. Hook DLL 安装 Inline Hook + 一次性重置                       │
└─────────────────────────────────────────────────────────────────┘
```

## 构建

双击运行 `build.bat`，自动完成全部步骤：

1. 从 GitHub 下载 MinHook 源码
2. 编译 32 位 Hook DLL（`HookAffinity32.dll`）
3. 编译 64 位 Hook DLL（`HookAffinity64.dll`）
4. 编译资源文件，将两个 DLL 嵌入
5. 编译 64 位 EXE，输出单文件 `CancelWindowDisplayAffinity.exe`

### 环境要求

- Windows 10/11 x64
- Visual Studio 2022（需安装 C++ 桌面开发工作负载）

### 手动构建

从 **x86 Native Tools Command Prompt**：
```bat
cl /nologo /LD /I minhook/include /Fe:HookAffinity32.dll hook_dll.cpp minhook/src/hook.c minhook/src/buffer.c minhook/src/trampoline.c minhook/src/hde/hde32.c minhook/src/hde/hde64.c user32.lib
```

从 **x64 Native Tools Command Prompt**：
```bat
cl /nologo /LD /I minhook/include /Fe:HookAffinity64.dll hook_dll.cpp minhook/src/hook.c minhook/src/buffer.c minhook/src/trampoline.c minhook/src/hde/hde32.c minhook/src/hde/hde64.c user32.lib
rc /nologo payload.rc
cl /nologo /EHsc /Fe:CancelWindowDisplayAffinity.exe main.cpp payload.res user32.lib psapi.lib advapi32.lib shell32.lib
```

## 使用

运行 `CancelWindowDisplayAffinity.exe`（自动请求管理员权限），程序将自动发现并处理所有受保护窗口。

## 文件说明

| 文件 | 用途 |
|------|------|
| `hook_dll.cpp` | Hook DLL 源码（MinHook inline hook） |
| `main.cpp` | 扫描器 + 注入器（含跨架构注入） |
| `resource.h` | 资源 ID 定义 |
| `payload.rc` | 资源脚本（嵌入 DLL） |
| `build.bat` | 一键构建脚本 |

## 注意事项

- 本工具仅用于合法的屏幕捕获用途
- 部分应用可能检测并阻止 DLL 注入
- 使用直接 syscall 或内核驱动设置保护的应用无法被 Ring 3 Hook 拦截
- 请遵守相关法律法规和服务条款

## License

This project is provided as-is without warranty. Use at your own risk.
