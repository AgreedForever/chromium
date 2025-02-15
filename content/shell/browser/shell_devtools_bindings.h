// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_SHELL_DEVTOOLS_BINDINGS_H_
#define CONTENT_SHELL_BROWSER_SHELL_DEVTOOLS_BINDINGS_H_

#include <memory>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_frontend_host.h"
#include "content/public/browser/web_contents_observer.h"
#include "net/url_request/url_fetcher_delegate.h"

#if !defined(OS_ANDROID)
#include "content/public/browser/devtools_frontend_host.h"
#endif

namespace base {
class Value;
}

namespace content {

class NavigationHandle;

class ShellDevToolsDelegate {
 public:
  virtual void Close() = 0;
  virtual ~ShellDevToolsDelegate(){};
};

class WebContents;

class ShellDevToolsBindings : public WebContentsObserver,
                              public DevToolsAgentHostClient,
                              public net::URLFetcherDelegate {
 public:
  ShellDevToolsBindings(WebContents* devtools_contents,
                        WebContents* inspected_contents,
                        ShellDevToolsDelegate* delegate);

  void InspectElementAt(int x, int y);

  void CallClientFunction(const std::string& function_name,
                          const base::Value* arg1,
                          const base::Value* arg2,
                          const base::Value* arg3);
  ~ShellDevToolsBindings() override;

 protected:
  // content::DevToolsAgentHostClient implementation.
  void AgentHostClosed(DevToolsAgentHost* agent_host, bool replaced) override;
  void DispatchProtocolMessage(DevToolsAgentHost* agent_host,
                               const std::string& message) override;

  void SetPreferences(const std::string& json);
  virtual void HandleMessageFromDevToolsFrontend(const std::string& message);
  void CreateFrontendHost();

 private:
  // WebContentsObserver overrides
  void ReadyToCommitNavigation(NavigationHandle* navigation_handle) override;
  void RenderViewCreated(RenderViewHost* render_view_host) override;
  void DocumentAvailableInMainFrame() override;
  void WebContentsDestroyed() override;

  // net::URLFetcherDelegate overrides.
  void OnURLFetchComplete(const net::URLFetcher* source) override;

  void SendMessageAck(int request_id, const base::Value* arg1);

  WebContents* inspected_contents_;
  ShellDevToolsDelegate* delegate_;
  scoped_refptr<DevToolsAgentHost> agent_host_;
  int inspect_element_at_x_;
  int inspect_element_at_y_;
#if !defined(OS_ANDROID)
  std::unique_ptr<DevToolsFrontendHost> frontend_host_;
#endif
  using PendingRequestsMap = std::map<const net::URLFetcher*, int>;
  PendingRequestsMap pending_requests_;
  base::DictionaryValue preferences_;
  using ExtensionsAPIs = std::map<std::string, std::string>;
  ExtensionsAPIs extensions_api_;
  base::WeakPtrFactory<ShellDevToolsBindings> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ShellDevToolsBindings);
};

}  // namespace content

#endif  // CONTENT_SHELL_BROWSER_SHELL_DEVTOOLS_BINDINGS_H_
