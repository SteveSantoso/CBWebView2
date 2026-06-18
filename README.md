# CBWebView2 Developer Guide 

> Native Microsoft Edge **WebView2** (Chromium) integration for Unreal Engine.
> Embed full web pages in UMG and world-space 3D widgets, with transparency hit-test,
> IME input, and a JavaScript ⇄ Blueprint bridge.
>
> [中文版](https://github.com/SteveSantoso/CBWebView2/blob/main/Developer_Guide_zh-CN.md)
> 
> QQ交流群：1038275218
> 
> [Official link for WebView2](https://developer.microsoft.com/en-us/microsoft-edge/webview2?form=MA13LH)
---

## Table of Contents

1. [Overview](#1-overview)
2. [Requirements & Installation](#2-requirements--installation)
3. [Module Layout](#3-module-layout)
4. [Quick Start](#4-quick-start)
5. [Core Classes & API](#5-core-classes--api)
6. [JavaScript ⇄ Blueprint Bridge](#6-javascript--blueprint-bridge)
7. [Transparency Hit-Test](#7-transparency-hit-test)
8. [World-Space 3D Web Pages](#8-world-space-3d-web-pages)
9. [IME Text Input](#9-ime-text-input)
10. [Project Settings](#10-project-settings)
11. [Downloads, Printing & DevTools](#11-downloads-printing--devtools)
12. [FAQ](#12-faq)

---

## 1. Overview

CBWebView2 embeds Microsoft's **Evergreen Edge WebView2** runtime directly inside Unreal
Engine. Unlike the legacy built-in Web Browser, it renders modern web content at full
fidelity (WebGL, CSS animations, video, ECharts/Three.js dashboards) and provides
two-way communication between your game and the page.

**Key capabilities:**

- **UMG widget** `UCBWebView2Widget` — drop a real browser into any Widget Blueprint.
- **World-space widget** `UCBWebView2WorldWidget` — render web UI onto meshes / 3D HUDs
  through a `WidgetComponent`.
- **Windowless composition (Visual WinComp) mode** — true layering and transparency with
  Slate content, plus a transparent hit-test system so clicks pass through transparent
  regions to the game underneath.
- **JavaScript bridge** — call `ExecuteScript()` from Blueprint; receive page messages
  via `OnMessageReceived` (`window.chrome.webview.postMessage`).
- **Full IME** — Chinese / Japanese / Korean input with correct caret tracking.
- **Navigation & history** — `LoadURL`/`GoBack`/`GoForward`/`Reload`/`StopLoading`, plus
  title, source-URL, and can-go-back/forward change events.
- **Downloads, Print-to-PDF, DevTools, runtime background-color control.**
- **Automatic world-space GPU output path**: D3D12 shared texture / D3D11 GPU copy /
  CPU readback fallback, selected per active RHI.

---

## 2. Requirements & Installation

| Item | Requirement |
| --- | --- |
| Platform | **Windows 64-bit (Win64) only** |
| Engine | UE 5.x (developed and verified on UE 5.7) |
| Runtime dependency | **Microsoft Edge WebView2 Runtime** on the end-user machine (preinstalled on up-to-date Win10/11; otherwise free from Microsoft) |

**Steps:**

1. Place the `CBWebView2` folder into your project's `Plugins/` directory (or the engine's
   `Engine/Plugins/Marketplace/`).
2. Launch the project and confirm `CBWebView2` is enabled under **Edit → Plugins** (category **Web**).
3. C++ projects: regenerate project files from the `.uproject` and compile. Blueprint-only
   projects will be prompted to build the plugin modules on first launch.
4. `WebView2Loader.dll` ships with the plugin — no extra setup required.

---

## 3. Module Layout

The plugin contains two **Runtime** modules (both `LoadingPhase=Default`, Win64 only):

- **`CBWebView2`** — the project-facing UMG / Slate layer. Blueprints usually interact with
  this module's classes directly (`UCBWebView2Widget`, `UCBWebView2WorldWidget`).
- **`WebView2Utils`** — the native integration layer (Win32 / Windows.UI.Composition /
  WebView2 Runtime / D3D interop), hosting the engine subsystem and project settings.

It depends on a third-party `WebView2` module (`Source/ThirdParty/WebView2`) bundling the
WebView2 SDK headers, libs, and `WebView2Loader.dll`.

---

## 4. Quick Start

### 4.1 Embed a page in UMG

1. Create/open a **Widget Blueprint**.
2. In the palette, search for **CB Web View 2** and drag it onto the canvas.
3. In **Details → CBWebView2**, set `Initial Url` (e.g. `https://example.com` or a local `file:///...`).
4. Create the widget from your HUD/level Blueprint and `Add to Viewport`.

Change the URL at runtime:

```
// Blueprint: on a UCBWebView2Widget reference
LoadURL("https://www.unrealengine.com")
```

### 4.2 Load a local page

Put your HTML in the project (see the plugin's `Content/Web/index.html`) and load it via an
absolute `file:///` path:

```
LoadURL("file:///D:/MyProject/Content/Web/index.html")
```

---

## 5. Core Classes & API

### 5.1 `UCBWebView2Widget` (UMG browser)

The most common project-side entry point. Key editable properties:

| Property | Description |
| --- | --- |
| `InitialUrl` | URL loaded on creation |
| `BackgroundColor` | Default background; Alpha=0 means transparent |
| `bEnableTransparencyHitTest` | Inject the transparency hit-test script |
| `bAllowNonInteractiveElementPassthrough` | Allow non-interactive painted areas (e.g. plain panels) to pass clicks through |

**Common functions (all BlueprintCallable):**

| Function | Description |
| --- | --- |
| `LoadURL(Url)` | Navigate to a URL |
| `GetCurrentURL()` / `GetCurrentTitle()` | Current URL / title |
| `ExecuteScript(Script, Callback)` | Run JS; result string returned via callback |
| `GoForward()` / `GoBack()` / `Reload()` / `StopLoading()` | Navigation control |
| `OpenDevToolsWindow()` | Open DevTools |
| `PrintToPdf(OutputPath, bLandscape)` | Export the current page to PDF |
| `SetBackgroundColorEx(Color)` | Change background color at runtime |
| `SetWebViewVisibility(Visibility)` | Keep UMG and native WebView visibility in sync |

**Events (BlueprintAssignable):**

`OnMessageReceived`, `OnLoadStarted`, `OnLoadCompleted`, `OnNewWindowRequested`,
`OnCursorChanged`, `OnInputActivationRequested`, `OnDocumentTitleChanged`, `OnSourceChanged`,
`OnCanGoBackChanged`, `OnCanGoForwardChanged`, `OnDownloadStarting`, `OnDownloadUpdated`,
`OnPrintToPdfCompleted`, `OnMouseButtonDoubleClickEvent`, `OnMonitoredEvent` (unified
monitoring stream for logging/telemetry).

### 5.2 `UCBWebView2WorldWidget` (world-space web page)

For `WidgetComponent` workflows: place it inside a UserWidget and let a WidgetComponent
present it in the scene. In addition to the same navigation/event API as the UMG widget, it adds:

| Member | Description |
| --- | --- |
| `RefreshRate` | World-space texture refresh rate |
| `LastRenderedTexture` | Most recent texture pushed to the material |
| `SetRefreshRateEx(Rate)` | Change refresh rate at runtime |
| `RequestRefresh()` | Explicitly request one frame refresh |

### 5.3 `UWebView2Subsystem` (engine subsystem)

Engine-level subsystem that registers the Windows message handler and tracks whether a
WebView currently owns input focus. Exposes global `OnWebMessageReceived` and
`OnMonitoredEvent` events (plus `*Native` C++ delegates). Get the singleton with
`UWebView2Subsystem::Get()`.

### 5.4 `UWebView2Settings` (project settings)

Centralizes all public configuration — see [section 10](#10-project-settings). Access it
read-only at runtime via `UWebView2Settings::Get()`.

---

## 6. JavaScript ⇄ Blueprint Bridge

Communication uses the standard WebView2 WebMessage mechanism.

### 6.1 Blueprint → page (run JS)

```
ExecuteScript("document.title", <ScriptExecuted callback>)
// The callback's Result parameter holds the JS evaluation result as a string
```

### 6.2 Page → Blueprint (send a message)

From page JavaScript:

```javascript
// Send a string
window.chrome.webview.postMessage("hello from page");

// Or send an object (the host receives a JSON string)
window.chrome.webview.postMessage({ type: "score", value: 42 });
```

Bind the widget's `OnMessageReceived(Message: String)` to receive it. You can also receive
globally through `UWebView2Subsystem::OnWebMessageReceived`.

> Security: enable origin validation in **Security** project settings
> (`bEnableWebMessageOriginCheck`) and configure `AllowedMessageOrigins` (supports `*`,
> full origins, or suffix wildcards like `https://*.example.com`).

---

## 7. Transparency Hit-Test

In **Visual WinComp** mode, the page can layer and blend with the game image. The plugin
auto-injects `transparency_check.js` so that clicks on transparent regions pass through to
the scene below while interactive elements still respond.

- Toggle injection with `bEnableTransparencyHitTest`.
- Mark elements to force pass-through or force interactive:
  - Pass-through: `class="transparent-pass-through"` or `data-transparent-pass-through`
  - Force interactive: `class="transparent-force-interactive"` or `data-transparent-interactive`
- `bAllowNonInteractiveElementPassthrough` controls whether purely painted (non-interactive)
  areas also pass clicks through.

Set the WebView background to transparent (`BackgroundColor` Alpha=0, or `SetBackgroundColorEx`)
to overlay it on your game HUD.

---

## 8. World-Space 3D Web Pages

`UCBWebView2WorldWidget` renders the page to a texture in the scene via a `WidgetComponent` —
apply it to any mesh for 3D control panels or data dashboards.

**Output path is selected automatically** (no manual config; see `ECBWebView2WorldOutputMode`):

- **D3D12 shared texture**: under D3D12, share the captured frame via an NT-handle shared
  texture and a shared fence — full GPU path, no CPU readback.
- **D3D11 GPU copy**: under D3D11, copy the captured frame on the GPU.
- **CPU readback**: fallback path, always available on Win64; any GPU-path failure falls back here.

`RefreshRate` controls update frequency; hidden views can be auto-suspended / memory-reduced
via the Performance settings (see section 10).

---

## 9. IME Text Input

The plugin injects `input_event_bridge.js` to track focus and the caret rectangle of editable
elements, forwarding caret position to the native layer so the Windows IME candidate window is
positioned correctly. This enables Chinese / Japanese / Korean input and composition with no
extra setup — just focus a web input field.

---

## 10. Project Settings

**Edit → Project Settings → Plugins → CB WebView2** (`UWebView2Settings`, written to the
`CBWebView2` config).

| Group | Key items | Notes |
| --- | --- | --- |
| **General** | `Mode` | `Windowed` (child-HWND embedding, simple but cannot blend with Slate) / `VisualWinComp` (default; windowless composition with layering & transparency). **Editor restart required after change.** |
| **Environment** | `Language`, `bEnableSingleSignOn`, `bTrackingPrevention`, `bEnableBrowserExtensions`, `AdditionalBrowserArguments` | Environment-level; applied once at Environment creation. **Restart required.** |
| **Controller** | `ProfileName`, `bInPrivate`, `DownloadPath`, `ScriptLocale`, `bAllowHostInputProcessing` | Controller creation options. |
| **Features** | `bEnableContextMenus`, `bEnableScript`, `bEnableDevTools`, `bEnableWebMessage`, `bEnableZoomControl`, `bMuted`, etc. | Common feature toggles. |
| **Security** | `bEnableWebMessageOriginCheck`, `bWebMessageOriginWarnOnly`, `AllowedMessageOrigins`, `bFilterInternalMessagesFromBlueprint`, `DefaultPermissionPolicy` | Message-origin validation and permission policy. |
| **Performance** | `bSuspendWhenHidden`, `SuspendDelaySeconds`, `bReduceMemoryWhenHidden` | Suspend / reduce memory when hidden. |
| **World** | `TextureAlphaThreshold` | Alpha threshold (0–255) for world-space texture passthrough. |
| **Appearance** | `DefaultBackgroundColor` | Default background; Alpha=0 is transparent. |

> Items marked `ConfigRestartRequired` (e.g. `Mode`, `Environment`, `Controller`) take effect
> only after restarting the editor, or at least destroying and recreating all WebView instances.

---

## 11. Downloads, Printing & DevTools

- **Downloads**: bind `OnDownloadStarting` (fired once on creation) and `OnDownloadUpdated`
  (progress/state changes). The callback carries `FCBWebView2DownloadInfo`: `Uri`, `MimeType`,
  `ResultFilePath`, `BytesReceived`, `TotalBytesToReceive`, `State`
  (InProgress/Interrupted/Completed). Set the download folder via Controller `DownloadPath`.
- **Print to PDF**: `PrintToPdf(OutputPath, bLandscape)`; on completion `OnPrintToPdfCompleted(bSuccess, OutputPath)` fires.
- **DevTools**: `OpenDevToolsWindow()` opens Chromium DevTools (requires `Features.bEnableDevTools=true`).

---

## 12. FAQ

**Q: Blank/white screen, page not showing?**
A: Ensure the Microsoft Edge WebView2 Runtime is installed on the machine. Up-to-date Win10/11
ship with it; otherwise download it free from Microsoft.

**Q: Doesn't load on Mac/Linux/mobile?**
A: The plugin is Win64-only (restricted by `PlatformAllowList` in the `.uplugin`). Modules will
not load on other platforms — this is expected.

**Q: Page won't overlay transparently / clicks won't pass through?**
A: Use `Mode=VisualWinComp`, set `BackgroundColor` Alpha to 0, and enable
`bEnableTransparencyHitTest`. Pass-through over purely painted areas requires
`bAllowNonInteractiveElementPassthrough`.

**Q: Changed a project setting and nothing happened?**
A: Items marked `ConfigRestartRequired` (`Mode`/`Environment`/`Controller`) require an editor
restart, or destroying and recreating WebView instances.

**Q: Blueprint doesn't receive messages from the page?**
A: Confirm `Features.bEnableWebMessage=true`; the page must call
`window.chrome.webview.postMessage(...)`; if origin validation is enabled, ensure the origin is
in the `AllowedMessageOrigins` allowlist.
