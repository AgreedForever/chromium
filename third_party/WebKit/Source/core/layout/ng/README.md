# LayoutNG #

This directory contains the implementation of Blink's new layout engine
"LayoutNG".

This README can be viewed in formatted form [here](https://chromium.googlesource.com/chromium/src/+/master/third_party/WebKit/Source/core/layout/ng/README.md).

The original design document can be seen [here](https://docs.google.com/document/d/1uxbDh4uONFQOiGuiumlJBLGgO4KDWB8ZEkp7Rd47fw4/edit).

## High level overview ##

CSS has many different types of layout modes, controlled by the `display`
property. (In addition to this specific HTML elements have custom layout modes
as well). For each different type of layout, we have a
[NGLayoutAlgorithm](ng_layout_algorithm.h).

The input to an [NGLayoutAlgorithm](ng_layout_algorithm.h) is the same tuple
for every kind of layout:

 - The [NGBlockNode](ng_block_node.h) which we are currently performing layout for. The
   following information is accessed:

   - The [ComputedStyle](../../style/ComputedStyle.h) for the node which we are
     currently performing laying for.

   - The list of children [NGBlockNode](ng_block_node.h)es to perform layout upon, and their
     respective style objects.

 - The [NGConstraintSpace](ng_constraint_space.h) which represents the "space"
   in which the current layout should produce a
   [NGPhysicalFragment](ng_physical_fragment.h).

 - TODO(layout-dev): BreakTokens should go here once implemented.

The current layout should not access any information outside this set, this
will break invariants in the system. (As a concrete example we intend to cache
[NGPhysicalFragment](ng_physical_fragment.h)s based on this set, accessing
additional information outside this set will break caching behaviour).

### Box Tree ###

TODO(layout-dev): Document with lots of pretty pictures.

### Inline Layout ###

Inline layout is performed in the following phases:

1. Pre-layout.
2. Line breaking.
3. Line box construction.

This is similar to [CSS Text Processing Order of Operations],
but not exactly the same,
because the spec prioritizes the simple description than being accurate.

[CSS Text Processing Order of Operations]: https://drafts.csswg.org/css-text-3/#order

#### Pre-layout ####

For inline layout there is a pre-layout pass that prepares the internal data
structures needed to perform line layout.

The pre-layout pass, triggered by calling `NGInlineNode::PrepareLayout()`, has
three separate steps or stages that are executed in order:

  - `CollectInlines`: Performs a depth-first scan of the container collecting
    all non-atomic inlines and `TextNodes`s. Atomic inlines are represented as a
    unicode object replacement character but are otherwise skipped.
    Each non-atomic inline and `TextNodes` is fed to a
    [NGInlineItemsBuilder](ng_inline_items_builder.h) instance which collects
    the text content for all non-atomic inlines in the container.

    During this process white-space is collapsed and normalized according to CSS
    white-space processing rules.

    The CSS [text-transform] is already applied in LayoutObject tree.
    The plan is to implement in this phase when LayoutNG builds the tree from DOM.

  - `SegmentText`: Performs BiDi segmentation and resolution.
    See [Bidirectional text] below.

  - `ShapeText`: Shapes the resolved BiDi runs using HarfBuzz.
    TODO(eae): Fill out

[text-transform]: https://drafts.csswg.org/css-text-3/#propdef-text-transform

[NGInlineItem]: inline/ng_inline_item.h

#### Line Breaking ####

[NGLineBreaker] takes a list of [NGInlineItem],
measure them, break into lines, and
produces a list of [NGInlineItemResult] for each line.

[NGInlineItemResult] keeps several information
needed during the line box construction,
such as inline size and [ShapeResult],
though the inline position is recomputed later
because [Bidirectional text] may change it.

This phase:
1. Measures each item.
2. Breaks text [NGInlineItem] into multiple [NGInlineItemResult].
   The core logic of this part is implemented in [ShapingLineBreaker].
3. Computes inline size of borders/margins/paddings.
   The block size of borders/margins/paddings are ignored
   for inline non-replaced elements, but
   the inline size affects layout, and hence line breaking.
   See [CSS Calculating widths and margins] for more details.
4. Determines which item can fit in the line.
5. Determines the break opportunities between items.
   If an item overflows, and there were no break opportunity before it,
   the item before must also overflow.

[ShapingLineBreaker] is... TODO(kojii): fill out.

[CSS Calculating widths and margins]: https://drafts.csswg.org/css2/visudet.html#Computing_widths_and_margins

[NGInlineItemResult]: inline/ng_inline_item_result.h
[NGLineBreaker]: inline/ng_line_breaker.h
[ShapeResult]: ../../platform/fonts/shaping/ShapeResult.h
[ShapingLineBreaker]: ../../platform/fonts/shaping/ShapingLineBreaker.h

#### Line Box Construction ####

`NGInlineLayoutAlgorithm::CreateLine()` takes a list of [NGInlineItemResult] and
produces [NGPhysicalLineBoxFragment] for each line.

Lines are then wrapped in an anonymous [NGPhysicalBoxFragment]
so that one [NGInlineNode] has one corresponding fragment.

This phase consists of following sub-phases:

1. Bidirectional reordering:
   Reorder the list of [NGInlineItemResult]
   according to [UAX#9 Reordering Resolved Levels].
   See [Bidirectional text] below.

   After this point forward, the list of [NGInlineItemResult] is
   in _visual order_; which is from [line-left] to [line-right].
   The block direction is still logical,
   but the inline direction is physical.

2. Create a [NGPhysicalFragment] for each [NGInlineItemResult]
   in visual ([line-left] to [line-right]) order,
   and place them into [NGPhysicalLineBoxFragment].

   1. A text item produces a [NGPhysicalTextFragment].
   2. An open-tag item pushes a new stack entry of [NGInlineBoxState],
      and a close-tag item pops a stack entry.
      Performs operations that require the size of the inline box,
      or ancestor boxes.
      See [Inline Box Tree] below.

   The inline size of each item was already determined by [NGLineBreaker],
   but the inline position is recomputed
   because BiDi reordering may have changed it.

   In block direction, [NGPhysicalFragment] is placed
   as if the baseline is at 0.
   This is adjusted later, possibly multiple times.
   See [Inline Box Tree] and the post-process below.

3. Post-process the constructed line box.
   This includes:
   1. Process all pending operations in [Inline Box Tree].
   2. Moves the baseline to the correct position
      based on the height of the line box.
   3. Applies the CSS [text-align] property.

[line-left]: https://drafts.csswg.org/css-writing-modes-3/#line-left
[line-right]: https://drafts.csswg.org/css-writing-modes-3/#line-right
[text-align]: https://drafts.csswg.org/css-text-3/#propdef-text-align

[NGInlineNode]: inline/ng_inline_node.h
[NGInlineLayoutAlgorithm]: inline/ng_inlineLlayout_algorithm.h
[NGPhysicalBoxFragment]: ng_physical_box_fragment.h
[NGPhysicalFragment]: ng_physical_fragment.h
[NGPhysicalLineBoxFragment]: inline/ng_physical_line_box_fragment.h
[NGPhysicalTextFragment]: inline/ng_physical_text_fragment.h

#### Inline Box Tree ####
[Inline Box Tree]: #inline-box-tree

A flat list structure is suitable for many inline operations,
but some operations require an inline box tree structure.
A stack of [NGInlineBoxState] is constructed
from a list of [NGInlineItemResult] to represent the box tree structure.

This stack:
1. Caches common values for an inline box.
   For instance, the primary font metrics do not change within an inline box.
2. Computes the height of an inline box.
   The [height of inline, non-replaced elements depends on the content area],
   but CSS doesn't define how to compute the content area.
3. Creates [NGPhysicalBoxFragment]s when needed.
   CSS doesn't define when an inline box should have a box,
   but existing implementations are interoperable that
   there should be a box when it has borders.
   Also, background should have a box if it is not empty.
   Other cases are where paint or scroller will need a box.
4. Applies [vertical-align] to shift baselines.
   Some values are applicable immediately.
   Some values need the size of the box, or the parent box.
   Some values need the size of the root inline box.
   Depends on the value,
   the operation is queued to the appropriate stack entry.

Because of index-based operations to the list of [NGInlineItemResult],
the list is append-only during the process.
When all operations are done,
`OnEndPlaceItems()` turns the list into the final fragment tree structure.

[height of inline, non-replaced elements depends on the content area]: https://drafts.csswg.org/css2/visudet.html#inline-non-replaced
[vertical-align]: https://drafts.csswg.org/css2/visudet.html#propdef-vertical-align

[NGInlineBoxState]: inline/NGInlineBoxState

#### <a name="bidi"></a>Bidirectional Text ####
[Bidirectional Text]: #bidi

[UAX#9 Unicode Bidirectional Algorithm] defines
processing algorithm for bidirectional text.

The core logic is implemented in [NGBidiParagraph],
which is a thin wrapper for [ICU BiDi].

In a bird's‐eye view, it consists of two parts:

1. Before line breaking: Segmenting text and resolve embedding levels
   as defined in [UAX#9 Resolving Embedding Levels].

   The core logic uses [ICU BiDi] `ubidi_getLogicalRun()` function.

   This is part of the Pre-layout phase above.
2. After line breaking:  Reorder text
   as defined in [UAX#9 Reordering Resolved Levels].

   The core logic uses [ICU BiDi] `ubidi_reorderVisual()` function.

   This is part of the Line Box Construction phase above.


[ICU BiDi]: http://userguide.icu-project.org/transforms/bidi
[UAX#9 Unicode Bidirectional Algorithm]: http://unicode.org/reports/tr9/
[UAX#9 Resolving Embedding Levels]: http://www.unicode.org/reports/tr9/#Resolving_Embedding_Levels
[UAX#9 Reordering Resolved Levels]: http://www.unicode.org/reports/tr9/#Reordering_Resolved_Levels

[NGBidiParagraph]: inline/ng_bidi_paragraph.h

### Fragment Tree ###

TODO(layout-dev): Document with lots of pretty pictures.

### Constraint Spaces ###

TODO(layout-dev): Document with lots of pretty pictures.

## Block Flow Algorithm ##

This section contains details specific to the
[NGBlockLayoutAlgorithm](ng_block_layout_algorithm.h).

### Collapsing Margins ###

TODO(layout-dev): Document with lots of pretty pictures.

### Float Positioning ###

TODO(layout-dev): Document with lots of pretty pictures.

### Code coverage ###

The latest code coverage (from Feb 14 2017) can be found [here](https://glebl.users.x20web.corp.google.com/www/layout_ng_code_coverage/index.html).
Here is the instruction how to generate a new result.

#### Environment setup ####
 1. Set up Chromium development environment for Windows
 2. Download DynamoRIO from [www.dynamorio.org](www.dynamorio.org)
 3. Extract downloaded DynamoRIO package into your chromium/src folder.
 4. Get dynamorio.git and extract it into your chromium/src folder `git clone https://github.com/DynamoRIO/dynamorio.git`
 5. Install Node js from https://nodejs.org/en/download/
 6. Install lcov-merger dependencies:  `npm install vinyl, npm install vinyl-fs`
 7. Install Perl from https://www.perl.org/get.html#win32
 8. Get lcov-result-merger and extract into your chromium/src folder `git clone https://github.com/mweibel/lcov-result-merger`

#### Generating code coverage ####
* Build the unit tets target with debug information
`chromium\src> ninja -C out\Debug webkit_unit_tests`
* Run DynamoRIO with drcov tool
`chromium\src>DynamoRIO\bin64\drrun.exe -t drcov -- .\out\Debug\webkit_unit_tests.exe --gtest_filter=NG*`
* Convert the output information to lcov format
`chromium\src>for %file in (*.log) do DynamoRIO\tools\bin64\drcov2lcov.exe -input %file -output %file.info -src_filter layout/ng -src_skip_filter _test`
* Merge all lcov files into one file
`chromium\src>node lcov-result-merger\bin\lcov-result-merger.js *.info output.info`
* Generate the coverage html from the master lcov file
`chromium\src>C:\Perl64\bin\perl.exe dynamorio.git\third_party\lcov\genhtml output.info -o output`
