// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_DECLARATIVE_EVENT_H_
#define EXTENSIONS_RENDERER_DECLARATIVE_EVENT_H_

#include <vector>

#include "gin/wrappable.h"
#include "v8/include/v8.h"

namespace gin {
class Arguments;
}

namespace extensions {
class APIRequestHandler;
class APITypeReferenceMap;

// A gin::Wrappable object for declarative events (i.e., events that support
// "rules"). Unlike regular events, these do not have associated listeners, and
// extensions register an action to perform when the event happens.
class DeclarativeEvent final : public gin::Wrappable<DeclarativeEvent> {
 public:
  DeclarativeEvent(const std::string& name,
                   APITypeReferenceMap* type_refs,
                   APIRequestHandler* request_handler,
                   const std::vector<std::string>& actions_list,
                   const std::vector<std::string>& conditions_list,
                   int webview_instance_id);
  ~DeclarativeEvent() override;

  static gin::WrapperInfo kWrapperInfo;

  // gin::Wrappable:
  gin::ObjectTemplateBuilder GetObjectTemplateBuilder(
      v8::Isolate* isolate) final;

 private:
  // Bound methods for the JS object.
  void AddRules(gin::Arguments* arguments);
  void RemoveRules(gin::Arguments* arguments);
  void GetRules(gin::Arguments* arguments);

  void HandleFunction(const std::string& signature_name,
                      const std::string& request_name,
                      gin::Arguments* arguments);

  std::string event_name_;

  APITypeReferenceMap* type_refs_;

  APIRequestHandler* request_handler_;

  const int webview_instance_id_;

  DISALLOW_COPY_AND_ASSIGN(DeclarativeEvent);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_DECLARATIVE_EVENT_H_
