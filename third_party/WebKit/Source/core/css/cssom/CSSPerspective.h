// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CSSPerspective_h
#define CSSPerspective_h

#include "core/CoreExport.h"
#include "core/css/cssom/CSSNumericValue.h"
#include "core/css/cssom/CSSTransformComponent.h"

namespace blink {

class DOMMatrix;
class ExceptionState;

// Represents a perspective value in a CSSTransformValue used for properties
// like "transform".
// See CSSPerspective.idl for more information about this class.
class CORE_EXPORT CSSPerspective : public CSSTransformComponent {
  WTF_MAKE_NONCOPYABLE(CSSPerspective);
  DEFINE_WRAPPERTYPEINFO();

 public:
  // Constructor defined in the IDL.
  static CSSPerspective* Create(const CSSNumericValue*, ExceptionState&);

  // Blink-internal ways of creating CSSPerspectives.
  static CSSPerspective* FromCSSValue(const CSSFunctionValue&);

  // Getters and setters for attributes defined in the IDL.
  // Bindings require a non const return value.
  CSSNumericValue* length() const {
    return const_cast<CSSNumericValue*>(length_.Get());
  }

  // Internal methods - from CSSTransformComponent.
  TransformComponentType GetType() const override { return kPerspectiveType; }
  // TODO: Implement AsMatrix for CSSPerspective.
  DOMMatrix* AsMatrix() const override { return nullptr; }
  CSSFunctionValue* ToCSSValue() const override;

  DEFINE_INLINE_VIRTUAL_TRACE() {
    visitor->Trace(length_);
    CSSTransformComponent::Trace(visitor);
  }

 private:
  CSSPerspective(const CSSNumericValue* length) : length_(length) {}

  Member<const CSSNumericValue> length_;
};

}  // namespace blink

#endif
