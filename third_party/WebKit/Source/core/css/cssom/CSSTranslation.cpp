// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/cssom/CSSTranslation.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/css/CSSPrimitiveValue.h"
#include "core/css/cssom/CSSNumericValue.h"

namespace blink {

CSSTranslation* CSSTranslation::Create(CSSNumericValue* x,
                                       CSSNumericValue* y,
                                       CSSNumericValue* z,
                                       ExceptionState& exception_state) {
  if (z->ContainsPercent()) {
    exception_state.ThrowTypeError(
        "CSSTranslation does not support z CSSNumericValue with percent units");
    return nullptr;
  }
  return new CSSTranslation(x, y, z);
}

CSSFunctionValue* CSSTranslation::ToCSSValue() const {
  CSSFunctionValue* result = CSSFunctionValue::Create(
      Is2D() ? CSSValueTranslate : CSSValueTranslate3d);
  result->Append(*x_->ToCSSValue());
  result->Append(*y_->ToCSSValue());
  if (!Is2D())
    result->Append(*z_->ToCSSValue());
  return result;
}

}  // namespace blink
