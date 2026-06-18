// Copyright 2026-Present SteveSantoso. Additional CBWebView2 distribution notice for Fab; original Microsoft copyright and license terms remain below.
// Copyright (C) Microsoft Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __core_webview2_experimental_environment_options_h__
#define __core_webview2_experimental_environment_options_h__

#include <objbase.h>
#include <wrl/implements.h>

#include "WebView2EnvironmentOptions.h"
#include "WebView2Experimental.h"

// This is a base COM class that implements IUnknown if there is no Experimental
// options, or ICoreWebView2ExperimentalEnvironmentOptions when there are
// Experimental options.
template <typename allocate_fn_t,
          allocate_fn_t allocate_fn,
          typename deallocate_fn_t,
          deallocate_fn_t deallocate_fn>
class CoreWebView2ExperimentalEnvironmentOptionsBase
    : public Microsoft::WRL::Implements<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          // Placeholder
          IUnknown> {
 public:

  CoreWebView2ExperimentalEnvironmentOptionsBase() {}

 protected:
  ~CoreWebView2ExperimentalEnvironmentOptionsBase() = default;
};

template <typename allocate_fn_t,
          allocate_fn_t allocate_fn,
          typename deallocate_fn_t,
          deallocate_fn_t deallocate_fn>
class CoreWebView2ExperimentalEnvironmentOptionsClass
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          CoreWebView2EnvironmentOptionsBase<allocate_fn_t,
                                             allocate_fn,
                                             deallocate_fn_t,
                                             deallocate_fn>,
          CoreWebView2ExperimentalEnvironmentOptionsBase<allocate_fn_t,
                                                         allocate_fn,
                                                         deallocate_fn_t,
                                                         deallocate_fn>> {
 public:
  CoreWebView2ExperimentalEnvironmentOptionsClass() {}

  ~CoreWebView2ExperimentalEnvironmentOptionsClass() override{};
};

typedef CoreWebView2ExperimentalEnvironmentOptionsClass<
    decltype(&::CoTaskMemAlloc),
    ::CoTaskMemAlloc,
    decltype(&::CoTaskMemFree),
    ::CoTaskMemFree>
    CoreWebView2ExperimentalEnvironmentOptions;

#endif  // __core_webview2_experimental_environment_options_h__
