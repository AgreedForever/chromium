/*
 * Copyright (C) 2007 Rob Buis <buis@kde.org>
 * Copyright (C) 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "core/layout/svg/line/SVGInlineTextBox.h"

#include "core/layout/HitTestResult.h"
#include "core/layout/PointerEventsHitRules.h"
#include "core/layout/api/LineLayoutSVGInlineText.h"
#include "core/layout/svg/LayoutSVGInlineText.h"
#include "core/paint/SVGInlineTextBoxPainter.h"
#include "platform/wtf/MathExtras.h"

namespace blink {

struct ExpectedSVGInlineTextBoxSize : public InlineTextBox {
  LayoutUnit float1;
  uint32_t bitfields : 1;
  Vector<SVGTextFragment> vector;
};

static_assert(sizeof(SVGInlineTextBox) == sizeof(ExpectedSVGInlineTextBoxSize),
              "SVGInlineTextBox has an unexpected size");

SVGInlineTextBox::SVGInlineTextBox(LineLayoutItem item,
                                   int start,
                                   unsigned short length)
    : InlineTextBox(item, start, length), starts_new_text_chunk_(false) {}

void SVGInlineTextBox::DirtyLineBoxes() {
  InlineTextBox::DirtyLineBoxes();

  // Clear the now stale text fragments.
  ClearTextFragments();

  // And clear any following text fragments as the text on which they depend may
  // now no longer exist, or glyph positions may be wrong.
  InlineTextBox* next_box = NextTextBox();
  if (next_box)
    next_box->DirtyLineBoxes();
}

int SVGInlineTextBox::OffsetForPosition(LayoutUnit, bool) const {
  // SVG doesn't use the standard offset <-> position selection system, as it's
  // not suitable for SVGs complex needs. Vertical text selection, inline boxes
  // spanning multiple lines (contrary to HTML, etc.)
  NOTREACHED();
  return 0;
}

int SVGInlineTextBox::OffsetForPositionInFragment(
    const SVGTextFragment& fragment,
    LayoutUnit position,
    bool include_partial_glyphs) const {
  LineLayoutSVGInlineText line_layout_item =
      LineLayoutSVGInlineText(this->GetLineLayoutItem());

  float scaling_factor = line_layout_item.ScalingFactor();
  DCHECK(scaling_factor);

  const ComputedStyle& style = line_layout_item.StyleRef();

  TextRun text_run = ConstructTextRun(style, fragment);

  // Eventually handle lengthAdjust="spacingAndGlyphs".
  // FIXME: Handle vertical text.
  if (fragment.IsTransformed()) {
    AffineTransform fragment_transform = fragment.BuildFragmentTransform();
    text_run.SetHorizontalGlyphStretch(
        clampTo<float>(fragment_transform.XScale()));
  }

  return fragment.character_offset - Start() +
         line_layout_item.ScaledFont().OffsetForPosition(
             text_run, position * scaling_factor, include_partial_glyphs);
}

LayoutUnit SVGInlineTextBox::PositionForOffset(int) const {
  // SVG doesn't use the offset <-> position selection system.
  NOTREACHED();
  return LayoutUnit();
}

FloatRect SVGInlineTextBox::SelectionRectForTextFragment(
    const SVGTextFragment& fragment,
    int start_position,
    int end_position,
    const ComputedStyle& style) const {
  DCHECK_LT(start_position, end_position);

  LineLayoutSVGInlineText line_layout_item =
      LineLayoutSVGInlineText(this->GetLineLayoutItem());

  float scaling_factor = line_layout_item.ScalingFactor();
  DCHECK(scaling_factor);

  const Font& scaled_font = line_layout_item.ScaledFont();
  const SimpleFontData* font_data = scaled_font.PrimaryFont();
  DCHECK(font_data);
  if (!font_data)
    return FloatRect();

  const FontMetrics& scaled_font_metrics = font_data->GetFontMetrics();
  FloatPoint text_origin(fragment.x, fragment.y);
  if (scaling_factor != 1)
    text_origin.Scale(scaling_factor, scaling_factor);

  text_origin.Move(0, -scaled_font_metrics.FloatAscent());

  FloatRect selection_rect = scaled_font.SelectionRectForText(
      ConstructTextRun(style, fragment), text_origin,
      fragment.height * scaling_factor, start_position, end_position);
  if (scaling_factor == 1)
    return selection_rect;

  selection_rect.Scale(1 / scaling_factor);
  return selection_rect;
}

LayoutRect SVGInlineTextBox::LocalSelectionRect(int start_position,
                                                int end_position) const {
  int box_start = Start();
  start_position = std::max(start_position - box_start, 0);
  end_position = std::min(end_position - box_start, static_cast<int>(Len()));
  if (start_position >= end_position)
    return LayoutRect();

  const ComputedStyle& style = GetLineLayoutItem().StyleRef();

  FloatRect selection_rect;
  int fragment_start_position = 0;
  int fragment_end_position = 0;

  unsigned text_fragments_size = text_fragments_.size();
  for (unsigned i = 0; i < text_fragments_size; ++i) {
    const SVGTextFragment& fragment = text_fragments_.at(i);

    fragment_start_position = start_position;
    fragment_end_position = end_position;
    if (!MapStartEndPositionsIntoFragmentCoordinates(
            fragment, fragment_start_position, fragment_end_position))
      continue;

    FloatRect fragment_rect = SelectionRectForTextFragment(
        fragment, fragment_start_position, fragment_end_position, style);
    if (fragment.IsTransformed())
      fragment_rect = fragment.BuildFragmentTransform().MapRect(fragment_rect);

    selection_rect.Unite(fragment_rect);
  }

  return LayoutRect(EnclosingIntRect(selection_rect));
}

void SVGInlineTextBox::Paint(const PaintInfo& paint_info,
                             const LayoutPoint& paint_offset,
                             LayoutUnit,
                             LayoutUnit) const {
  SVGInlineTextBoxPainter(*this).Paint(paint_info, paint_offset);
}

TextRun SVGInlineTextBox::ConstructTextRun(
    const ComputedStyle& style,
    const SVGTextFragment& fragment) const {
  LineLayoutText text = GetLineLayoutItem();
  CHECK(!text.NeedsLayout());

  TextRun run(
      // characters, will be set below if non-zero.
      static_cast<const LChar*>(nullptr),
      0,  // length, will be set below if non-zero.
      0,  // xPos, only relevant with allowTabs=true
      0,  // padding, only relevant for justified text, not relevant for SVG
      TextRun::kAllowTrailingExpansion, Direction(),
      DirOverride() ||
          style.RtlOrdering() == EOrder::kVisual /* directionalOverride */);

  if (fragment.length) {
    if (text.Is8Bit())
      run.SetText(text.Characters8() + fragment.character_offset,
                  fragment.length);
    else
      run.SetText(text.Characters16() + fragment.character_offset,
                  fragment.length);
  }

  // We handle letter & word spacing ourselves.
  run.DisableSpacing();

  // Propagate the maximum length of the characters buffer to the TextRun, even
  // when we're only processing a substring.
  run.SetCharactersLength(text.TextLength() - fragment.character_offset);
  DCHECK_GE(run.CharactersLength(), run.length());
  return run;
}

bool SVGInlineTextBox::MapStartEndPositionsIntoFragmentCoordinates(
    const SVGTextFragment& fragment,
    int& start_position,
    int& end_position) const {
  int fragment_offset_in_box =
      static_cast<int>(fragment.character_offset) - Start();

  // Compute positions relative to the fragment.
  start_position -= fragment_offset_in_box;
  end_position -= fragment_offset_in_box;

  // Intersect with the fragment range.
  start_position = std::max(start_position, 0);
  end_position = std::min(end_position, static_cast<int>(fragment.length));

  return start_position < end_position;
}

void SVGInlineTextBox::PaintDocumentMarker(GraphicsContext&,
                                           const LayoutPoint&,
                                           const DocumentMarker&,
                                           const ComputedStyle&,
                                           const Font&,
                                           bool) const {
  // SVG does not have support for generic document markers (e.g.,
  // spellchecking, etc).
}

void SVGInlineTextBox::PaintTextMatchMarkerForeground(
    const PaintInfo& paint_info,
    const LayoutPoint& point,
    const TextMatchMarker& marker,
    const ComputedStyle& style,
    const Font& font) const {
  SVGInlineTextBoxPainter(*this).PaintTextMatchMarkerForeground(
      paint_info, point, marker, style, font);
}

void SVGInlineTextBox::PaintTextMatchMarkerBackground(
    const PaintInfo& paint_info,
    const LayoutPoint& point,
    const TextMatchMarker& marker,
    const ComputedStyle& style,
    const Font& font) const {
  SVGInlineTextBoxPainter(*this).PaintTextMatchMarkerBackground(
      paint_info, point, marker, style, font);
}

LayoutRect SVGInlineTextBox::CalculateBoundaries() const {
  LineLayoutSVGInlineText line_layout_item =
      LineLayoutSVGInlineText(this->GetLineLayoutItem());
  const SimpleFontData* font_data = line_layout_item.ScaledFont().PrimaryFont();
  DCHECK(font_data);
  if (!font_data)
    return LayoutRect();

  float scaling_factor = line_layout_item.ScalingFactor();
  DCHECK(scaling_factor);
  LayoutUnit baseline(font_data->GetFontMetrics().FloatAscent() /
                      scaling_factor);

  LayoutRect text_bounding_rect;
  for (const SVGTextFragment& fragment : text_fragments_)
    text_bounding_rect.Unite(
        LayoutRect(fragment.OverflowBoundingBox(baseline)));

  return text_bounding_rect;
}

bool SVGInlineTextBox::NodeAtPoint(HitTestResult& result,
                                   const HitTestLocation& location_in_container,
                                   const LayoutPoint& accumulated_offset,
                                   LayoutUnit,
                                   LayoutUnit) {
  // FIXME: integrate with InlineTextBox::nodeAtPoint better.
  DCHECK(!IsLineBreak());

  PointerEventsHitRules hit_rules(PointerEventsHitRules::SVG_TEXT_HITTESTING,
                                  result.GetHitTestRequest(),
                                  GetLineLayoutItem().Style()->PointerEvents());
  bool is_visible =
      GetLineLayoutItem().Style()->Visibility() == EVisibility::kVisible;
  if (is_visible || !hit_rules.require_visible) {
    if (hit_rules.can_hit_bounding_box ||
        (hit_rules.can_hit_stroke &&
         (GetLineLayoutItem().Style()->SvgStyle().HasStroke() ||
          !hit_rules.require_stroke)) ||
        (hit_rules.can_hit_fill &&
         (GetLineLayoutItem().Style()->SvgStyle().HasFill() ||
          !hit_rules.require_fill))) {
      LayoutRect rect(Location(), Size());
      rect.MoveBy(accumulated_offset);
      if (location_in_container.Intersects(rect)) {
        LineLayoutSVGInlineText line_layout_item =
            LineLayoutSVGInlineText(this->GetLineLayoutItem());
        const SimpleFontData* font_data =
            line_layout_item.ScaledFont().PrimaryFont();
        DCHECK(font_data);
        if (!font_data)
          return false;

        DCHECK(line_layout_item.ScalingFactor());
        float baseline = font_data->GetFontMetrics().FloatAscent() /
                         line_layout_item.ScalingFactor();
        FloatPoint float_location = FloatPoint(location_in_container.Point());
        for (const SVGTextFragment& fragment : text_fragments_) {
          FloatQuad fragment_quad = fragment.BoundingQuad(baseline);
          if (fragment_quad.ContainsPoint(float_location)) {
            line_layout_item.UpdateHitTestResult(
                result, location_in_container.Point() -
                            ToLayoutSize(accumulated_offset));
            if (result.AddNodeToListBasedTestResult(line_layout_item.GetNode(),
                                                    location_in_container,
                                                    rect) == kStopHitTesting)
              return true;
          }
        }
      }
    }
  }
  return false;
}

}  // namespace blink
