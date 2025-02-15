// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_TRANSLATE_CORE_BROWSER_TRANSLATE_RANKER_IMPL_H_
#define COMPONENTS_TRANSLATE_CORE_BROWSER_TRANSLATE_RANKER_IMPL_H_

#include <memory>
#include <string>

#include "base/feature_list.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/sequenced_task_runner.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/translate/core/browser/ranker_model_loader.h"
#include "components/translate/core/browser/translate_ranker.h"
#include "url/gurl.h"

class GURL;

namespace chrome_intelligence {
class RankerModel;
}  // namespace chrome_intelligence

namespace ukm {
class UkmRecorder;
}  // namespace ukm

namespace metrics {
class TranslateEventProto;
}  // namespace metrics

namespace translate {

// Features used to enable ranker query, enforcement and logging. Note that
// enabling enforcement implies (forces) enabling queries.
extern const base::Feature kTranslateRankerQuery;
extern const base::Feature kTranslateRankerEnforcement;
extern const base::Feature kTranslateRankerDecisionOverride;

struct TranslateRankerFeatures {
  TranslateRankerFeatures();

  TranslateRankerFeatures(int accepted,
                          int denied,
                          int ignored,
                          const std::string& src,
                          const std::string& dst,
                          const std::string& cntry,
                          const std::string& locale);

  TranslateRankerFeatures(const metrics::TranslateEventProto& tep);

  ~TranslateRankerFeatures();

  void WriteTo(std::ostream& stream) const;

  // Input value used to generate the features.
  int accepted_count;
  int denied_count;
  int ignored_count;
  int total_count;

  // Used for inference.
  std::string src_lang;
  std::string dst_lang;
  std::string country;
  std::string app_locale;
  double accepted_ratio;
  double denied_ratio;
  double ignored_ratio;
};

// If enabled, downloads a translate ranker model and uses it to determine
// whether the user should be given a translation prompt or not.
class TranslateRankerImpl : public TranslateRanker {
 public:
  TranslateRankerImpl(const base::FilePath& model_path,
                      const GURL& model_url,
                      ukm::UkmRecorder* ukm_recorder);
  ~TranslateRankerImpl() override;

  // Get the file path of the translate ranker model, by default with a fixed
  // name within |data_dir|.
  static base::FilePath GetModelPath(const base::FilePath& data_dir);

  // Get the URL from which the download the translate ranker model, by default
  // from Finch.
  static GURL GetModelURL();

  // TranslateRanker...
  void EnableLogging(bool value) override;
  uint32_t GetModelVersion() const override;
  bool ShouldOfferTranslation(
      metrics::TranslateEventProto* translate_event) override;
  void FlushTranslateEvents(
      std::vector<metrics::TranslateEventProto>* events) override;
  void RecordTranslateEvent(
      int event_type,
      const GURL& url,
      metrics::TranslateEventProto* translate_event) override;
  bool ShouldOverrideDecision(
      int event_type,
      const GURL& url,
      metrics::TranslateEventProto* translate_event) override;

  void OnModelAvailable(
      std::unique_ptr<chrome_intelligence::RankerModel> model);

  // Get the model decision on whether we should show the translate
  // UI or not given |translate_event|.
  bool GetModelDecision(const metrics::TranslateEventProto& translate_event);

  // Check if the ModelLoader has been initialized. Used to test ModelLoader
  // logic.
  bool CheckModelLoaderForTesting();

 private:
  void SendEventToUKM(const metrics::TranslateEventProto& translate_event,
                      const GURL& url);

  // Caches the translate event.
  void AddTranslateEvent(const metrics::TranslateEventProto& translate_event,
                         const GURL& url);

  // Used to log URL-keyed metrics. This pointer will outlive |this|.
  ukm::UkmRecorder* ukm_recorder_;

  // Used to sanity check the threading of this ranker.
  base::SequenceChecker sequence_checker_;

  // A helper to load the translate ranker model from disk cache or a URL.
  std::unique_ptr<RankerModelLoader> model_loader_;

  // The translation ranker model.
  std::unique_ptr<chrome_intelligence::RankerModel> model_;

  // Tracks whether or not translate event logging is enabled.
  bool is_logging_enabled_ = true;

  // Tracks whether or not translate ranker querying is enabled.
  bool is_query_enabled_ = true;

  // Tracks whether or not translate ranker enforcement is enabled. Note that
  // that also enables the code paths for translate ranker querying.
  bool is_enforcement_enabled_ = true;

  // Tracks whether or not translate ranker decision override is enabled. This
  // will override suppression heuristics.
  bool is_decision_override_enabled_ = true;

  // Saved cache of translate event protos.
  std::vector<metrics::TranslateEventProto> event_cache_;

  base::WeakPtrFactory<TranslateRankerImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(TranslateRankerImpl);
};

}  // namespace translate

std::ostream& operator<<(std::ostream& stream,
                         const translate::TranslateRankerFeatures& features);

#endif  // COMPONENTS_TRANSLATE_CORE_BROWSER_TRANSLATE_RANKER_IMPL_H_
