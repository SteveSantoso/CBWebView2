# CBWebView2 开发者文档（中文）

> 基于微软 Edge **WebView2**（Chromium）的原生 Unreal Engine 集成插件。
> 在 UMG 与世界空间 3D 控件中嵌入完整网页，支持透明穿透点击、IME 输入与 JavaScript ⇄ 蓝图双向通信。
>
> English version: `Developer_Guide_en.md`

---

## 目录

1. [概述](#1-概述)
2. [环境要求与安装](#2-环境要求与安装)
3. [模块结构](#3-模块结构)
4. [快速上手](#4-快速上手)
5. [核心类与 API](#5-核心类与-api)
6. [JavaScript ⇄ 蓝图通信](#6-javascript--蓝图通信)
7. [透明穿透点击](#7-透明穿透点击)
8. [世界空间 3D 网页](#8-世界空间-3d-网页)
9. [IME 中文输入](#9-ime-中文输入)
10. [项目设置](#10-项目设置)
11. [下载、打印与开发者工具](#11-下载打印与开发者工具)
12. [常见问题](#12-常见问题)

---

## 1. 概述

CBWebView2 把微软 **Evergreen Edge WebView2** 运行时直接嵌入到 Unreal。与引擎自带的旧版 Web Browser 相比，它能完整渲染现代网页（WebGL、CSS 动画、视频、ECharts/Three.js 仪表盘等），并提供游戏与网页之间的双向通信。

**核心能力：**

- **UMG 控件** `UCBWebView2Widget` —— 把真实浏览器放进任意 Widget 蓝图。
- **世界空间控件** `UCBWebView2WorldWidget` —— 通过 `WidgetComponent` 将网页渲染到网格体/3D HUD 上。
- **无窗口合成（Visual WinComp）模式** —— 与 Slate 内容真正分层混合，支持透明，并提供透明区域穿透点击。
- **JavaScript 桥接** —— 蓝图调用 `ExecuteScript()`，页面通过 `window.chrome.webview.postMessage` 回传消息到 `OnMessageReceived`。
- **完整 IME** —— 中文/日文/韩文输入与正确的光标定位。
- **导航与历史** —— `LoadURL`/`GoBack`/`GoForward`/`Reload`/`StopLoading`，以及标题、源 URL、前进/后退可用性变化事件。
- **下载、打印 PDF、DevTools、运行时背景色控制**。
- **世界空间 GPU 输出自动选路**：D3D12 共享纹理 / D3D11 GPU 拷贝 / CPU 回读兜底，按当前 RHI 自动选择。

---

## 2. 环境要求与安装

| 项目 | 要求 |
| --- | --- |
| 平台 | **仅 Windows 64 位（Win64）** |
| 引擎 | UE 5.x（本插件在 UE 5.7 上开发验证） |
| 运行时依赖 | 终端机需安装 **Microsoft Edge WebView2 Runtime**（新版 Win10/11 已预装；否则微软免费提供） |

**安装步骤：**

1. 将 `CBWebView2` 文件夹放入项目的 `Plugins/` 目录（或引擎的 `Engine/Plugins/Marketplace/`）。
2. 启动项目，在 **编辑 → 插件** 中确认 `CBWebView2` 已启用（属于 **Web** 分类）。
3. C++ 项目：右键 `.uproject` 重新生成项目文件并编译。蓝图项目首次启动会提示编译插件模块。
4. `WebView2Loader.dll` 已随插件分发，无需额外配置。

---

## 3. 模块结构

插件包含两个 **Runtime** 模块（均 `LoadingPhase=Default`，仅 Win64）：

- **`CBWebView2`** —— 面向项目的 UMG / Slate 层。蓝图通常直接与本模块的类交互（`UCBWebView2Widget`、`UCBWebView2WorldWidget`）。
- **`WebView2Utils`** —— 原生集成层（Win32 / Windows.UI.Composition / WebView2 Runtime / D3D 互操作），包含引擎子系统与项目设置。

依赖第三方 `WebView2` 模块（`Source/ThirdParty/WebView2`），内置 WebView2 SDK 头文件、库与 `WebView2Loader.dll`。

---

## 4. 快速上手

### 4.1 在 UMG 中嵌入网页

1. 新建/打开一个 **Widget Blueprint**。
2. 在控件面板里搜索 **CB Web View 2**，拖入画布。
3. 在 **Details → CBWebView2** 设置 `Initial Url`（如 `https://example.com` 或本地 `file:///...`）。
4. 在 HUD/关卡蓝图中创建该 Widget 并 `Add to Viewport`。

运行时切换地址：

```
// 蓝图：对 UCBWebView2Widget 引用调用
LoadURL("https://www.unrealengine.com")
```

### 4.2 加载本地页面

把 HTML 放到项目里（参考插件 `Content/Web/index.html`），用 `file:///` 绝对路径加载，例如：

```
LoadURL("file:///D:/MyProject/Content/Web/index.html")
```

---

## 5. 核心类与 API

### 5.1 `UCBWebView2Widget`（UMG 浏览器）

最常用的项目侧入口。关键可编辑属性：

| 属性 | 说明 |
| --- | --- |
| `InitialUrl` | 初始加载的 URL |
| `BackgroundColor` | 默认背景色，Alpha=0 表示透明 |
| `bEnableTransparencyHitTest` | 是否注入透明穿透点击脚本 |
| `bAllowNonInteractiveElementPassthrough` | 非交互绘制区域（如纯白面板）是否允许穿透 |

**常用函数（均 BlueprintCallable）：**

| 函数 | 说明 |
| --- | --- |
| `LoadURL(Url)` | 导航到 URL |
| `GetCurrentURL()` / `GetCurrentTitle()` | 获取当前 URL / 标题 |
| `ExecuteScript(Script, Callback)` | 执行 JS，结果字符串经回调返回 |
| `GoForward()` / `GoBack()` / `Reload()` / `StopLoading()` | 导航控制 |
| `OpenDevToolsWindow()` | 打开 DevTools |
| `PrintToPdf(OutputPath, bLandscape)` | 导出当前页为 PDF |
| `SetBackgroundColorEx(Color)` | 运行时改背景色 |
| `SetWebViewVisibility(Visibility)` | 同步 UMG 与原生 WebView 可见性 |

**事件（BlueprintAssignable）：**

`OnMessageReceived`、`OnLoadStarted`、`OnLoadCompleted`、`OnNewWindowRequested`、`OnCursorChanged`、`OnInputActivationRequested`、`OnDocumentTitleChanged`、`OnSourceChanged`、`OnCanGoBackChanged`、`OnCanGoForwardChanged`、`OnDownloadStarting`、`OnDownloadUpdated`、`OnPrintToPdfCompleted`、`OnMouseButtonDoubleClickEvent`、`OnMonitoredEvent`（统一监控事件，便于日志/遥测）。

### 5.2 `UCBWebView2WorldWidget`（世界空间网页）

用于 `WidgetComponent` 工作流，把它放进一个 UserWidget，再由 WidgetComponent 在场景中呈现。除与 UMG 控件相同的导航/事件 API 外，额外提供：

| 成员 | 说明 |
| --- | --- |
| `RefreshRate` | 世界空间纹理刷新率 |
| `LastRenderedTexture` | 最近一次推送给材质的纹理 |
| `SetRefreshRateEx(Rate)` | 运行时改刷新率 |
| `RequestRefresh()` | 主动请求刷新一帧 |

### 5.3 `UWebView2Subsystem`（引擎子系统）

引擎级子系统，负责注册 Windows 消息处理器、跟踪 WebView 是否拥有输入焦点。提供 `OnWebMessageReceived` 与 `OnMonitoredEvent` 全局事件，C++ 侧还有 `*Native` 委托。`UWebView2Subsystem::Get()` 获取单例。

### 5.4 `UWebView2Settings`（项目设置）

集中管理所有公开配置，见[第 10 节](#10-项目设置)。`UWebView2Settings::Get()` 可在运行时只读访问。

---

## 6. JavaScript ⇄ 蓝图通信

通信基于标准 WebView2 WebMessage 机制。

### 6.1 蓝图 → 网页（执行 JS）

```
ExecuteScript("document.title", <ScriptExecuted 回调>)
// 回调参数 Result 为 JS 求值结果字符串
```

### 6.2 网页 → 蓝图（发消息）

在页面 JS 中调用：

```javascript
// 发送字符串
window.chrome.webview.postMessage("hello from page");

// 或发送对象（host 收到 JSON 字符串）
window.chrome.webview.postMessage({ type: "score", value: 42 });
```

蓝图侧绑定控件的 `OnMessageReceived(Message: String)` 即可收到。也可通过 `UWebView2Subsystem::OnWebMessageReceived` 全局接收。

> 安全：可在项目设置的 **Security** 中开启来源校验（`bEnableWebMessageOriginCheck`）并配置允许的来源（`AllowedMessageOrigins`，支持 `*`、完整来源或 `https://*.example.com` 后缀通配）。

---

## 7. 透明穿透点击

在 **Visual WinComp** 模式下，网页可与游戏画面分层混合。插件自动注入 `transparency_check.js`，实现"透明区域点击穿透到下层场景，交互元素正常响应"。

- 用 `bEnableTransparencyHitTest` 开关注入。
- 页面上希望强制可交互/强制穿透的元素，可用类名/属性标记：
  - 穿透：`class="transparent-pass-through"` 或 `data-transparent-pass-through`
  - 强制可交互：`class="transparent-force-interactive"` 或 `data-transparent-interactive`
- `bAllowNonInteractiveElementPassthrough` 控制纯绘制（非交互）区域是否也允许穿透。

把 WebView 背景设为透明（`BackgroundColor` 的 Alpha=0 或 `SetBackgroundColorEx`），即可叠加在游戏 HUD 之上。

---

## 8. 世界空间 3D 网页

`UCBWebView2WorldWidget` 通过 `WidgetComponent` 把网页渲染为场景中的纹理，可贴到任意网格体、做 3D 操作面板或数据大屏。

**输出路径自动选择**（无需手动配置，见 `ECBWebView2WorldOutputMode`）：

- `D3D12 共享纹理`：D3D12 下走 NT-handle 共享纹理 + 共享 fence，纯 GPU 路径，无 CPU 回读。
- `D3D11 GPU 拷贝`：D3D11 下在 GPU 上拷贝。
- `CPU 回读`：兜底路径，Win64 始终可用；任何 GPU 路径失败会自动回退到此。

`RefreshRate` 控制刷新频率；隐藏时可由性能设置自动挂起/降内存（见第 10 节）。

---

## 9. IME 中文输入

插件注入 `input_event_bridge.js` 跟踪可编辑元素的焦点与光标矩形（caret rect），把光标位置回传给原生层，从而让 Windows IME 候选框定位正确，支持中文/日文/韩文输入与合成（composition）。无需额外配置，聚焦到网页输入框即可使用。

---

## 10. 项目设置

**编辑 → 项目设置 → 插件 → CB WebView2**（`UWebView2Settings`，配置写入 `CBWebView2` 配置）。

| 分组 | 关键项 | 说明 |
| --- | --- | --- |
| **General** | `Mode` | `Windowed`（子 HWND 嵌入，简单但无法与 Slate 混合）/ `VisualWinComp`（默认，无窗口合成，支持分层与透明）。**改后需重启编辑器。** |
| **Environment** | `Language`、`bEnableSingleSignOn`、`bTrackingPrevention`、`bEnableBrowserExtensions`、`AdditionalBrowserArguments` | 环境级选项，仅在创建 WebView2 Environment 时生效一次，**改后需重启**。 |
| **Controller** | `ProfileName`、`bInPrivate`、`DownloadPath`、`ScriptLocale`、`bAllowHostInputProcessing` | 控制器创建选项。 |
| **Features** | `bEnableContextMenus`、`bEnableScript`、`bEnableDevTools`、`bEnableWebMessage`、`bEnableZoomControl`、`bMuted` 等 | 常用功能开关。 |
| **Security** | `bEnableWebMessageOriginCheck`、`bWebMessageOriginWarnOnly`、`AllowedMessageOrigins`、`bFilterInternalMessagesFromBlueprint`、`DefaultPermissionPolicy` | 消息来源校验与权限策略。 |
| **Performance** | `bSuspendWhenHidden`、`SuspendDelaySeconds`、`bReduceMemoryWhenHidden` | 隐藏后挂起进程/降低内存。 |
| **World** | `TextureAlphaThreshold` | 世界空间纹理 Alpha 穿透阈值（0–255）。 |
| **Appearance** | `DefaultBackgroundColor` | 默认背景色，Alpha=0 透明。 |

> 标注 `ConfigRestartRequired` 的项（如 `Mode`、`Environment`、`Controller`）修改后需重启编辑器，或至少销毁并重建所有 WebView 实例后生效。

---

## 11. 下载、打印与开发者工具

- **下载**：绑定 `OnDownloadStarting`（创建时触发一次）与 `OnDownloadUpdated`（进度/状态变化）。回调携带 `FCBWebView2DownloadInfo`：`Uri`、`MimeType`、`ResultFilePath`、`BytesReceived`、`TotalBytesToReceive`、`State`（InProgress/Interrupted/Completed）。下载目录可在 Controller 设置 `DownloadPath`。
- **打印 PDF**：`PrintToPdf(OutputPath, bLandscape)`，完成后触发 `OnPrintToPdfCompleted(bSuccess, OutputPath)`。
- **DevTools**：`OpenDevToolsWindow()` 打开 Chromium 开发者工具（需 `Features.bEnableDevTools=true`）。

---

## 12. 常见问题

**Q：运行后白屏/不显示网页？**
A：确认终端机已安装 Microsoft Edge WebView2 Runtime；新版 Win10/11 默认预装，否则到微软官网免费下载安装。

**Q：在 Mac/Linux/移动端不加载？**
A：插件仅支持 Win64（`.uplugin` 的 `PlatformAllowList` 限定）。其他平台模块不会加载，属预期行为。

**Q：网页无法叠加透明？点击穿不过去？**
A：需 `Mode=VisualWinComp`，将 `BackgroundColor` Alpha 设为 0，并开启 `bEnableTransparencyHitTest`。纯绘制区域穿透需 `bAllowNonInteractiveElementPassthrough`。

**Q：改了项目设置不生效？**
A：`Mode`/`Environment`/`Controller` 等标注 `ConfigRestartRequired` 的项需重启编辑器，或销毁重建 WebView 实例。

**Q：网页发的消息蓝图收不到？**
A：确认 `Features.bEnableWebMessage=true`；页面用 `window.chrome.webview.postMessage(...)`；若开启了来源校验，确认来源在 `AllowedMessageOrigins` 白名单内。
