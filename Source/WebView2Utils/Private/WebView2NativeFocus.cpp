// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "WebView2NativeFocus.h"

#if PLATFORM_WINDOWS

namespace CBWebView2NativeFocus
{
	namespace
	{
		/** Guards against a malformed parent chain looping forever. Real chains are only a handful of levels deep. */
		constexpr int32 MaxParentWalkDepth = 32;

		bool IsWebView2WindowClass(HWND Window)
		{
			WIDECHAR ClassName[64];
			const int32 Length = ::GetClassNameW(Window, ClassName, UE_ARRAY_COUNT(ClassName));
			if (Length <= 0)
			{
				return false;
			}

			const FString ClassStr(Length, ClassName);
			// WebView2's child HWND classes all start with "Chrome_" (Chrome_WidgetWin_1, Chrome_RenderWidgetHostHWND, ...).
			return ClassStr.StartsWith(TEXT("Chrome_")) || ClassStr.StartsWith(TEXT("Intermediate D3D Window"));
		}
	}

	bool IsWebView2OwnedWindow(HWND Candidate, HWND StopAt, HWND OwnedRoot)
	{
		if (!Candidate)
		{
			return false;
		}

		HWND Walk = Candidate;
		for (int32 Depth = 0; Walk && Walk != StopAt && Depth < MaxParentWalkDepth; ++Depth)
		{
			if (OwnedRoot && Walk == OwnedRoot)
			{
				return true;
			}

			if (IsWebView2WindowClass(Walk))
			{
				return true;
			}

			Walk = ::GetParent(Walk);
		}

		return false;
	}

	bool ThreadOwnsCaretFor(HWND Window)
	{
		if (!Window)
		{
			return false;
		}

		GUITHREADINFO ThreadInfo = {};
		ThreadInfo.cbSize = sizeof(ThreadInfo);
		// A thread id of 0 queries the calling thread, which is where every caret call in this plugin happens.
		if (!::GetGUIThreadInfo(0, &ThreadInfo))
		{
			return false;
		}

		return ThreadInfo.hwndCaret == Window;
	}
}

#endif
