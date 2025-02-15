/*
 * Copyright (C) 2004, 2006, 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef Position_h
#define Position_h

#include "core/CoreExport.h"
#include "core/dom/ContainerNode.h"
#include "core/editing/EditingBoundary.h"
#include "core/editing/EditingStrategy.h"
#include "platform/heap/Handle.h"
#include "platform/text/TextDirection.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/PassRefPtr.h"
#include "platform/wtf/RefPtr.h"

namespace blink {

class Node;
enum class TextAffinity;
class TreeScope;

enum class PositionAnchorType : unsigned {
  kOffsetInAnchor,
  kBeforeAnchor,
  kAfterAnchor,
  kBeforeChildren,
  kAfterChildren,
};

// Instances of |PositionTemplate<Strategy>| are immutable.
// TODO(editing-dev): Make constructor of |PositionTemplate| take |const Node*|.
template <typename Strategy>
class CORE_TEMPLATE_CLASS_EXPORT PositionTemplate {
  DISALLOW_NEW();

 public:
  PositionTemplate()
      : offset_(0), anchor_type_(PositionAnchorType::kOffsetInAnchor) {}

  static const TreeScope* CommonAncestorTreeScope(
      const PositionTemplate<Strategy>&,
      const PositionTemplate<Strategy>& b);
  static PositionTemplate<Strategy> EditingPositionOf(Node* anchor_node,
                                                      int offset);

  // For creating before/after positions:
  PositionTemplate(Node* anchor_node, PositionAnchorType);

  // For creating offset positions:
  // FIXME: This constructor should eventually go away. See bug 63040.
  PositionTemplate(Node* anchor_node, int offset);

  PositionTemplate(const PositionTemplate&);

  PositionAnchorType AnchorType() const { return anchor_type_; }
  bool IsAfterAnchor() const {
    return anchor_type_ == PositionAnchorType::kAfterAnchor;
  }
  bool IsAfterChildren() const {
    return anchor_type_ == PositionAnchorType::kAfterChildren;
  }
  bool IsBeforeAnchor() const {
    return anchor_type_ == PositionAnchorType::kBeforeAnchor;
  }
  bool IsBeforeChildren() const {
    return anchor_type_ == PositionAnchorType::kBeforeChildren;
  }
  bool IsOffsetInAnchor() const {
    return anchor_type_ == PositionAnchorType::kOffsetInAnchor;
  }

  // These are always DOM compliant values.  Editing positions like [img, 0]
  // (aka [img, before]) will return img->parentNode() and img->nodeIndex() from
  // these functions.

  // null for a before/after position anchored to a node with no parent
  Node* ComputeContainerNode() const;

  // O(n) for before/after-anchored positions, O(1) for parent-anchored
  // positions
  int ComputeOffsetInContainerNode() const;

  // Convenience method for DOM positions that also fixes up some positions for
  // editing
  PositionTemplate<Strategy> ParentAnchoredEquivalent() const;

  // Returns |PositionIsAnchor| type |Position| which is compatible with
  // |RangeBoundaryPoint| as safe to pass |Range| constructor. Return value
  // of this function is different from |parentAnchoredEquivalent()| which
  // returns editing specific position.
  PositionTemplate<Strategy> ToOffsetInAnchor() const;

  // Inline O(1) access for Positions which callers know to be parent-anchored
  int OffsetInContainerNode() const {
    DCHECK(IsOffsetInAnchor());
    return offset_;
  }

  // Returns an offset for editing based on anchor type for using with
  // |anchorNode()| function:
  //   - OffsetInAnchor  m_offset
  //   - BeforeChildren  0
  //   - BeforeAnchor    0
  //   - AfterChildren   last editing offset in anchor node
  //   - AfterAnchor     last editing offset in anchor node
  // Editing operations will change in anchor node rather than nodes around
  // anchor node.
  int ComputeEditingOffset() const;

  // These are convenience methods which are smart about whether the position is
  // neighbor anchored or parent anchored
  Node* ComputeNodeBeforePosition() const;
  Node* ComputeNodeAfterPosition() const;

  // Returns node as |Range::firstNode()|. This position must be a
  // |PositionAnchorType::OffsetInAhcor| to behave as |Range| boundary point.
  Node* NodeAsRangeFirstNode() const;

  // Similar to |nodeAsRangeLastNode()|, but returns a node in a range.
  Node* NodeAsRangeLastNode() const;

  // Returns a node as past last as same as |Range::pastLastNode()|. This
  // function is supposed to used in HTML serialization and plain text
  // iterator. This position must be a |PositionAnchorType::OffsetInAhcor| to
  // behave as |Range| boundary point.
  Node* NodeAsRangePastLastNode() const;

  Node* CommonAncestorContainer(const PositionTemplate<Strategy>&) const;

  Node* AnchorNode() const { return anchor_node_.Get(); }

  Document* GetDocument() const {
    return anchor_node_ ? &anchor_node_->GetDocument() : 0;
  }
  bool IsConnected() const {
    return anchor_node_ && anchor_node_->isConnected();
  }

  bool IsNull() const { return !anchor_node_; }
  bool IsNotNull() const { return anchor_node_; }
  bool IsOrphan() const { return anchor_node_ && !anchor_node_->isConnected(); }

  // Note: Comparison of positions require both parameters are non-null. You
  // should check null-position before comparing them.
  // TODO(yosin): We should use |Position::operator<()| instead of
  // |Position::comapreTo()| to utilize |DHCECK_XX()|.
  int CompareTo(const PositionTemplate<Strategy>&) const;
  bool operator<(const PositionTemplate<Strategy>&) const;
  bool operator<=(const PositionTemplate<Strategy>&) const;
  bool operator>(const PositionTemplate<Strategy>&) const;
  bool operator>=(const PositionTemplate<Strategy>&) const;

  bool IsEquivalent(const PositionTemplate<Strategy>&) const;

  // These can be either inside or just before/after the node, depending on
  // if the node is ignored by editing or not.
  // FIXME: These should go away. They only make sense for legacy positions.
  bool AtFirstEditingPositionForNode() const;
  bool AtLastEditingPositionForNode() const;

  bool AtStartOfTree() const;
  bool AtEndOfTree() const;

  static PositionTemplate<Strategy> BeforeNode(Node* anchor_node);
  static PositionTemplate<Strategy> AfterNode(Node* anchor_node);
  static PositionTemplate<Strategy> InParentBeforeNode(const Node& anchor_node);
  static PositionTemplate<Strategy> InParentAfterNode(const Node& anchor_node);
  static int LastOffsetInNode(Node* anchor_node);
  static PositionTemplate<Strategy> FirstPositionInNode(Node* anchor_node);
  static PositionTemplate<Strategy> LastPositionInNode(Node* anchor_node);
  static PositionTemplate<Strategy> FirstPositionInOrBeforeNode(
      Node* anchor_node);
  static PositionTemplate<Strategy> LastPositionInOrAfterNode(
      Node* anchor_node);

  String ToAnchorTypeAndOffsetString() const;
#ifndef NDEBUG
  void ShowTreeForThis() const;
  void ShowTreeForThisInFlatTree() const;
#endif

  DECLARE_TRACE();

 private:
  bool IsAfterAnchorOrAfterChildren() const {
    return IsAfterAnchor() || IsAfterChildren();
  }

  Member<Node> anchor_node_;
  // m_offset can be the offset inside m_anchorNode, or if
  // editingIgnoresContent(m_anchorNode) returns true, then other places in
  // editing will treat m_offset == 0 as "before the anchor" and m_offset > 0 as
  // "after the anchor node".  See parentAnchoredEquivalent for more info.
  int offset_;
  PositionAnchorType anchor_type_;
};

extern template class CORE_EXTERN_TEMPLATE_EXPORT
    PositionTemplate<EditingStrategy>;
extern template class CORE_EXTERN_TEMPLATE_EXPORT
    PositionTemplate<EditingInFlatTreeStrategy>;

using Position = PositionTemplate<EditingStrategy>;
using PositionInFlatTree = PositionTemplate<EditingInFlatTreeStrategy>;

template <typename Strategy>
bool operator==(const PositionTemplate<Strategy>& a,
                const PositionTemplate<Strategy>& b) {
  if (a.IsNull())
    return b.IsNull();

  if (a.AnchorNode() != b.AnchorNode() || a.AnchorType() != b.AnchorType())
    return false;

  if (!a.IsOffsetInAnchor()) {
    // Note: |m_offset| only has meaning when
    // |PositionAnchorType::OffsetInAnchor|.
    return true;
  }

  // FIXME: In <div><img></div> [div, 0] != [img, 0] even though most of the
  // editing code will treat them as identical.
  return a.OffsetInContainerNode() == b.OffsetInContainerNode();
}

template <typename Strategy>
bool operator!=(const PositionTemplate<Strategy>& a,
                const PositionTemplate<Strategy>& b) {
  return !(a == b);
}

CORE_EXPORT PositionInFlatTree ToPositionInFlatTree(const Position&);
CORE_EXPORT Position ToPositionInDOMTree(const Position&);
CORE_EXPORT Position ToPositionInDOMTree(const PositionInFlatTree&);

template <typename Strategy>
PositionTemplate<Strategy> FromPositionInDOMTree(const Position&);

template <>
inline Position FromPositionInDOMTree<EditingStrategy>(
    const Position& position) {
  return position;
}

template <>
inline PositionInFlatTree FromPositionInDOMTree<EditingInFlatTreeStrategy>(
    const Position& position) {
  return ToPositionInFlatTree(position);
}

CORE_EXPORT std::ostream& operator<<(std::ostream&, PositionAnchorType);
CORE_EXPORT std::ostream& operator<<(std::ostream&, const Position&);
CORE_EXPORT std::ostream& operator<<(std::ostream&, const PositionInFlatTree&);

}  // namespace blink

#ifndef NDEBUG
// Outside the WebCore namespace for ease of invocation from gdb.
void showTree(const blink::Position&);
void showTree(const blink::Position*);
#endif

#endif  // Position_h
