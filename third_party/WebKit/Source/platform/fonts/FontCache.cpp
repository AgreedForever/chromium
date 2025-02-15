/*
 * Copyright (C) 2006, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Nicholas Shanks <webkit@nickshanks.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/fonts/FontCache.h"

#include <memory>
#include "base/trace_event/process_memory_dump.h"
#include "platform/FontFamilyNames.h"
#include "platform/Histogram.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/fonts/AcceptLanguagesResolver.h"
#include "platform/fonts/AlternateFontFamily.h"
#include "platform/fonts/FontCacheClient.h"
#include "platform/fonts/FontCacheKey.h"
#include "platform/fonts/FontDataCache.h"
#include "platform/fonts/FontDescription.h"
#include "platform/fonts/FontGlobalContext.h"
#include "platform/fonts/FontPlatformData.h"
#include "platform/fonts/FontSmoothingMode.h"
#include "platform/fonts/SimpleFontData.h"
#include "platform/fonts/TextRenderingMode.h"
#include "platform/fonts/opentype/OpenTypeVerticalData.h"
#include "platform/fonts/shaping/ShapeCache.h"
#include "platform/instrumentation/tracing/web_memory_allocator_dump.h"
#include "platform/instrumentation/tracing/web_process_memory_dump.h"
#include "platform/wtf/HashMap.h"
#include "platform/wtf/ListHashSet.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/StdLibExtras.h"
#include "platform/wtf/Vector.h"
#include "platform/wtf/debug/Alias.h"
#include "platform/wtf/text/AtomicStringHash.h"
#include "platform/wtf/text/StringHash.h"
#include "public/platform/Platform.h"
#include "ui/gfx/font_list.h"

using namespace WTF;

namespace blink {

#if !OS(WIN) && !OS(LINUX)
FontCache::FontCache() : purge_prevent_count_(0), font_manager_(nullptr) {}
#endif  // !OS(WIN) && !OS(LINUX)

typedef HashMap<unsigned,
                std::unique_ptr<FontPlatformData>,
                WTF::IntHash<unsigned>,
                WTF::UnsignedWithZeroKeyHashTraits<unsigned>>
    SizedFontPlatformDataSet;
typedef HashMap<FontCacheKey,
                SizedFontPlatformDataSet,
                FontCacheKeyHash,
                FontCacheKeyTraits>
    FontPlatformDataCache;
typedef HashMap<FallbackListCompositeKey,
                std::unique_ptr<ShapeCache>,
                FallbackListCompositeKeyHash,
                FallbackListCompositeKeyTraits>
    FallbackListShaperCache;

static FontPlatformDataCache* g_font_platform_data_cache = nullptr;
static FallbackListShaperCache* g_fallback_list_shaper_cache = nullptr;

SkFontMgr* FontCache::static_font_manager_ = nullptr;

#if OS(WIN)
bool FontCache::antialiased_text_enabled_ = false;
bool FontCache::lcd_text_enabled_ = false;
float FontCache::device_scale_factor_ = 1.0;
bool FontCache::use_skia_font_fallback_ = false;
#endif  // OS(WIN)

FontCache* FontCache::GetFontCache() {
  return &FontGlobalContext::GetFontCache();
}

#if !OS(MACOSX)
FontPlatformData* FontCache::SystemFontPlatformData(
    const FontDescription& font_description) {
  const AtomicString& family = FontCache::SystemFontFamily();
#if OS(LINUX)
  if (family.IsEmpty() || family == FontFamilyNames::system_ui)
    return nullptr;
#else
  DCHECK(!family.IsEmpty() && family != FontFamilyNames::system_ui);
#endif
  return GetFontPlatformData(font_description, FontFaceCreationParams(family),
                             AlternateFontName::kNoAlternate);
}
#endif

FontPlatformData* FontCache::GetFontPlatformData(
    const FontDescription& font_description,
    const FontFaceCreationParams& creation_params,
    AlternateFontName alternate_font_name) {
  if (!g_font_platform_data_cache) {
    g_font_platform_data_cache = new FontPlatformDataCache;
    PlatformInit();
  }

#if !OS(MACOSX)
  if (creation_params.CreationType() == kCreateFontByFamily &&
      creation_params.Family() == FontFamilyNames::system_ui) {
    return SystemFontPlatformData(font_description);
  }
#endif

  float size = font_description.EffectiveFontSize();
  unsigned rounded_size = size * FontCacheKey::PrecisionMultiplier();
  FontCacheKey key = font_description.CacheKey(creation_params);

  // Remove the font size from the cache key, and handle the font size
  // separately in the inner HashMap. So that different size of FontPlatformData
  // can share underlying SkTypeface.
  if (RuntimeEnabledFeatures::FontCacheScalingEnabled())
    key.ClearFontSize();

  FontPlatformData* result;
  bool found_result;

  {
    // addResult's scope must end before we recurse for alternate family names
    // below, to avoid trigering its dtor hash-changed asserts.
    SizedFontPlatformDataSet* sized_fonts =
        &g_font_platform_data_cache->insert(key, SizedFontPlatformDataSet())
             .stored_value->value;
    bool was_empty = sized_fonts->IsEmpty();

    // Take a different size instance of the same font before adding an entry to
    // |sizedFont|.
    FontPlatformData* another_size =
        was_empty ? nullptr : sized_fonts->begin()->value.get();
    auto add_result = sized_fonts->insert(rounded_size, nullptr);
    std::unique_ptr<FontPlatformData>* found = &add_result.stored_value->value;
    if (add_result.is_new_entry) {
      if (was_empty) {
        *found = CreateFontPlatformData(font_description, creation_params, size,
                                        alternate_font_name);
      } else if (another_size) {
        *found = ScaleFontPlatformData(*another_size, font_description,
                                       creation_params, size);
      }
    }

    result = found->get();
    found_result = result || !add_result.is_new_entry;
  }

  if (!found_result &&
      alternate_font_name == AlternateFontName::kAllowAlternate &&
      creation_params.CreationType() == kCreateFontByFamily) {
    // We were unable to find a font. We have a small set of fonts that we alias
    // to other names, e.g., Arial/Helvetica, Courier/Courier New, etc. Try
    // looking up the font under the aliased name.
    const AtomicString& alternate_name =
        AlternateFamilyName(creation_params.Family());
    if (!alternate_name.IsEmpty()) {
      FontFaceCreationParams create_by_alternate_family(alternate_name);
      result = GetFontPlatformData(font_description, create_by_alternate_family,
                                   AlternateFontName::kNoAlternate);
    }
    if (result) {
      // Cache the result under the old name.
      auto adding =
          &g_font_platform_data_cache->insert(key, SizedFontPlatformDataSet())
               .stored_value->value;
      adding->Set(rounded_size, WTF::WrapUnique(new FontPlatformData(*result)));
    }
  }

  return result;
}

std::unique_ptr<FontPlatformData> FontCache::ScaleFontPlatformData(
    const FontPlatformData& font_platform_data,
    const FontDescription& font_description,
    const FontFaceCreationParams& creation_params,
    float font_size) {
#if OS(MACOSX)
  return CreateFontPlatformData(font_description, creation_params, font_size);
#else
  return WTF::MakeUnique<FontPlatformData>(font_platform_data, font_size);
#endif
}

ShapeCache* FontCache::GetShapeCache(const FallbackListCompositeKey& key) {
  if (!g_fallback_list_shaper_cache)
    g_fallback_list_shaper_cache = new FallbackListShaperCache;

  FallbackListShaperCache::iterator it =
      g_fallback_list_shaper_cache->find(key);
  ShapeCache* result = nullptr;
  if (it == g_fallback_list_shaper_cache->end()) {
    result = new ShapeCache();
    g_fallback_list_shaper_cache->Set(key, WTF::WrapUnique(result));
  } else {
    result = it->value.get();
  }

  DCHECK(result);
  return result;
}

typedef HashMap<FontCache::FontFileKey,
                RefPtr<OpenTypeVerticalData>,
                IntHash<FontCache::FontFileKey>,
                UnsignedWithZeroKeyHashTraits<FontCache::FontFileKey>>
    FontVerticalDataCache;

FontVerticalDataCache& FontVerticalDataCacheInstance() {
  DEFINE_STATIC_LOCAL(FontVerticalDataCache, font_vertical_data_cache, ());
  return font_vertical_data_cache;
}

void FontCache::SetFontManager(sk_sp<SkFontMgr> font_manager) {
  DCHECK(!static_font_manager_);
  static_font_manager_ = font_manager.release();
}

PassRefPtr<OpenTypeVerticalData> FontCache::GetVerticalData(
    const FontFileKey& key,
    const FontPlatformData& platform_data) {
  FontVerticalDataCache& font_vertical_data_cache =
      FontVerticalDataCacheInstance();
  FontVerticalDataCache::iterator result = font_vertical_data_cache.find(key);
  if (result != font_vertical_data_cache.end())
    return result.Get()->value;

  RefPtr<OpenTypeVerticalData> vertical_data =
      OpenTypeVerticalData::Create(platform_data);
  if (!vertical_data->IsOpenType())
    vertical_data.Clear();
  font_vertical_data_cache.Set(key, vertical_data);
  return vertical_data;
}

void FontCache::AcceptLanguagesChanged(const String& accept_languages) {
  AcceptLanguagesResolver::AcceptLanguagesChanged(accept_languages);
  GetFontCache()->InvalidateShapeCache();
}

static FontDataCache* g_font_data_cache = 0;

PassRefPtr<SimpleFontData> FontCache::GetFontData(
    const FontDescription& font_description,
    const AtomicString& family,
    AlternateFontName altername_font_name,
    ShouldRetain should_retain) {
  if (FontPlatformData* platform_data = GetFontPlatformData(
          font_description,
          FontFaceCreationParams(
              AdjustFamilyNameToAvoidUnsupportedFonts(family)),
          altername_font_name)) {
    return FontDataFromFontPlatformData(
        platform_data, should_retain, font_description.SubpixelAscentDescent());
  }

  return nullptr;
}

PassRefPtr<SimpleFontData> FontCache::FontDataFromFontPlatformData(
    const FontPlatformData* platform_data,
    ShouldRetain should_retain,
    bool subpixel_ascent_descent) {
  if (!g_font_data_cache)
    g_font_data_cache = new FontDataCache;

#if DCHECK_IS_ON()
  if (should_retain == kDoNotRetain)
    DCHECK(purge_prevent_count_);
#endif

  return g_font_data_cache->Get(platform_data, should_retain,
                                subpixel_ascent_descent);
}

bool FontCache::IsPlatformFamilyMatchAvailable(
    const FontDescription& font_description,
    const AtomicString& family) {
  return GetFontPlatformData(
      font_description,
      FontFaceCreationParams(AdjustFamilyNameToAvoidUnsupportedFonts(family)),
      AlternateFontName::kNoAlternate);
}

bool FontCache::IsPlatformFontUniqueNameMatchAvailable(
    const FontDescription& font_description,
    const AtomicString& unique_font_name) {
  return GetFontPlatformData(font_description,
                             FontFaceCreationParams(unique_font_name),
                             AlternateFontName::kLocalUniqueFace);
}

String FontCache::FirstAvailableOrFirst(const String& families) {
  // The conversions involve at least two string copies, and more if non-ASCII.
  // For now we prefer shared code over the cost because a) inputs are
  // only from grd/xtb and all ASCII, and b) at most only a few times per
  // setting change/script.
  return String::FromUTF8(
      gfx::FontList::FirstAvailableOrFirst(families.Utf8().data()).c_str());
}

SimpleFontData* FontCache::GetNonRetainedLastResortFallbackFont(
    const FontDescription& font_description) {
  return GetLastResortFallbackFont(font_description, kDoNotRetain).LeakRef();
}

void FontCache::ReleaseFontData(const SimpleFontData* font_data) {
  DCHECK(g_font_data_cache);

  g_font_data_cache->Release(font_data);
}

static inline void PurgePlatformFontDataCache() {
  if (!g_font_platform_data_cache)
    return;

  Vector<FontCacheKey> keys_to_remove;
  keys_to_remove.ReserveInitialCapacity(g_font_platform_data_cache->size());
  for (auto& sized_fonts : *g_font_platform_data_cache) {
    Vector<unsigned> sizes_to_remove;
    sizes_to_remove.ReserveInitialCapacity(sized_fonts.value.size());
    for (const auto& platform_data : sized_fonts.value) {
      if (platform_data.value &&
          !g_font_data_cache->Contains(platform_data.value.get()))
        sizes_to_remove.push_back(platform_data.key);
    }
    sized_fonts.value.RemoveAll(sizes_to_remove);
    if (sized_fonts.value.IsEmpty())
      keys_to_remove.push_back(sized_fonts.key);
  }
  g_font_platform_data_cache->RemoveAll(keys_to_remove);
}

static inline void PurgeFontVerticalDataCache() {
  FontVerticalDataCache& font_vertical_data_cache =
      FontVerticalDataCacheInstance();
  if (!font_vertical_data_cache.IsEmpty()) {
    // Mark & sweep unused verticalData
    FontVerticalDataCache::iterator vertical_data_end =
        font_vertical_data_cache.end();
    for (FontVerticalDataCache::iterator vertical_data =
             font_vertical_data_cache.begin();
         vertical_data != vertical_data_end; ++vertical_data) {
      if (vertical_data->value)
        vertical_data->value->SetInFontCache(false);
    }

    g_font_data_cache->MarkAllVerticalData();

    Vector<FontCache::FontFileKey> keys_to_remove;
    keys_to_remove.ReserveInitialCapacity(font_vertical_data_cache.size());
    for (FontVerticalDataCache::iterator vertical_data =
             font_vertical_data_cache.begin();
         vertical_data != vertical_data_end; ++vertical_data) {
      if (!vertical_data->value || !vertical_data->value->InFontCache())
        keys_to_remove.push_back(vertical_data->key);
    }
    font_vertical_data_cache.RemoveAll(keys_to_remove);
  }
}

static inline void PurgeFallbackListShaperCache() {
  unsigned items = 0;
  if (g_fallback_list_shaper_cache) {
    FallbackListShaperCache::iterator iter;
    for (iter = g_fallback_list_shaper_cache->begin();
         iter != g_fallback_list_shaper_cache->end(); ++iter) {
      items += iter->value->size();
    }
    g_fallback_list_shaper_cache->clear();
  }
  DEFINE_STATIC_LOCAL(CustomCountHistogram, shape_cache_histogram,
                      ("Blink.Fonts.ShapeCache", 1, 1000000, 50));
  shape_cache_histogram.Count(items);
}

void FontCache::InvalidateShapeCache() {
  PurgeFallbackListShaperCache();
}

void FontCache::Purge(PurgeSeverity purge_severity) {
  // Ideally we should never be forcing the purge while the
  // FontCachePurgePreventer is in scope, but we call purge() at any timing
  // via MemoryCoordinator.
  if (purge_prevent_count_)
    return;

  if (!g_font_data_cache || !g_font_data_cache->Purge(purge_severity))
    return;

  PurgePlatformFontDataCache();
  PurgeFontVerticalDataCache();
  PurgeFallbackListShaperCache();
}

static bool g_invalidate_font_cache = false;

HeapHashSet<WeakMember<FontCacheClient>>& FontCacheClients() {
  DEFINE_STATIC_LOCAL(HeapHashSet<WeakMember<FontCacheClient>>, clients,
                      (new HeapHashSet<WeakMember<FontCacheClient>>));
  g_invalidate_font_cache = true;
  return clients;
}

void FontCache::AddClient(FontCacheClient* client) {
  CHECK(client);
  DCHECK(!FontCacheClients().Contains(client));
  FontCacheClients().insert(client);
}

static unsigned short g_generation = 0;

unsigned short FontCache::Generation() {
  return g_generation;
}

void FontCache::Invalidate() {
  if (!g_invalidate_font_cache) {
    DCHECK(!g_font_platform_data_cache);
    return;
  }

  if (g_font_platform_data_cache) {
    delete g_font_platform_data_cache;
    g_font_platform_data_cache = new FontPlatformDataCache;
  }

  g_generation++;

  HeapVector<Member<FontCacheClient>> clients;
  CopyToVector(FontCacheClients(), clients);
  for (const auto& client : clients)
    client->FontCacheInvalidated();

  Purge(kForcePurge);
}

void FontCache::CrashWithFontInfo(const FontDescription* font_description) {
  FontCache* font_cache = FontCache::GetFontCache();
  SkFontMgr* font_mgr = nullptr;
  int num_families = std::numeric_limits<int>::min();
  if (font_cache) {
    font_mgr = font_cache->font_manager_.get();
    if (font_mgr)
      num_families = font_mgr->countFamilies();
  }

  FontDescription font_description_copy = *font_description;
  debug::Alias(&font_description_copy);

  debug::Alias(&font_cache);
  debug::Alias(&font_mgr);
  debug::Alias(&num_families);

  CHECK(false);
}

void FontCache::DumpFontPlatformDataCache(
    base::trace_event::ProcessMemoryDump* memory_dump) {
  DCHECK(IsMainThread());
  if (!g_font_platform_data_cache)
    return;
  base::trace_event::MemoryAllocatorDump* dump =
      memory_dump->CreateAllocatorDump("font_caches/font_platform_data_cache");
  size_t font_platform_data_objects_size =
      g_font_platform_data_cache->size() * sizeof(FontPlatformData);
  dump->AddScalar("size", "bytes", font_platform_data_objects_size);
  memory_dump->AddSuballocation(dump->guid(),
                                WTF::Partitions::kAllocatedObjectPoolName);
}

void FontCache::DumpShapeResultCache(
    base::trace_event::ProcessMemoryDump* memory_dump) {
  DCHECK(IsMainThread());
  if (!g_fallback_list_shaper_cache) {
    return;
  }
  base::trace_event::MemoryAllocatorDump* dump =
      memory_dump->CreateAllocatorDump("font_caches/shape_caches");
  size_t shape_result_cache_size = 0;
  FallbackListShaperCache::iterator iter;
  for (iter = g_fallback_list_shaper_cache->begin();
       iter != g_fallback_list_shaper_cache->end(); ++iter) {
    shape_result_cache_size += iter->value->ByteSize();
  }
  dump->AddScalar("size", "bytes", shape_result_cache_size);
  memory_dump->AddSuballocation(dump->guid(),
                                WTF::Partitions::kAllocatedObjectPoolName);
}

}  // namespace blink
