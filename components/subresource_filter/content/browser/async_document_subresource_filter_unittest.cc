// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/subresource_filter/content/browser/async_document_subresource_filter.h"

#include <memory>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/test/test_simple_task_runner.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "components/subresource_filter/content/browser/async_document_subresource_filter_test_utils.h"
#include "components/subresource_filter/core/common/load_policy.h"
#include "components/subresource_filter/core/common/memory_mapped_ruleset.h"
#include "components/subresource_filter/core/common/proto/rules.pb.h"
#include "components/subresource_filter/core/common/test_ruleset_creator.h"
#include "components/subresource_filter/core/common/test_ruleset_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace subresource_filter {

class AsyncDocumentSubresourceFilterTest : public ::testing::Test {
 public:
  AsyncDocumentSubresourceFilterTest() = default;

 protected:
  void SetUp() override {
    std::vector<proto::UrlRule> rules;
    rules.push_back(testing::CreateWhitelistRuleForDocument(
        "whitelisted.subframe.com", proto::ACTIVATION_TYPE_GENERICBLOCK,
        {"example.com"}));
    rules.push_back(testing::CreateSuffixRule("disallowed.html"));

    ASSERT_NO_FATAL_FAILURE(test_ruleset_creator_.CreateRulesetWithRules(
        rules, &test_ruleset_pair_));

    dealer_handle_.reset(
        new VerifiedRulesetDealer::Handle(blocking_task_runner_));
  }

  void TearDown() override {
    dealer_handle_.reset(nullptr);
    RunUntilIdle();
  }

  const testing::TestRuleset& ruleset() const {
    return test_ruleset_pair_.indexed;
  }

  void RunUntilIdle() {
    base::RunLoop().RunUntilIdle();
    while (blocking_task_runner_->HasPendingTask()) {
      blocking_task_runner_->RunUntilIdle();
      base::RunLoop().RunUntilIdle();
    }
  }

  VerifiedRulesetDealer::Handle* dealer_handle() {
    return dealer_handle_.get();
  }

  std::unique_ptr<VerifiedRuleset::Handle> CreateRulesetHandle() {
    return base::MakeUnique<VerifiedRuleset::Handle>(dealer_handle());
  }

 private:
  testing::TestRulesetCreator test_ruleset_creator_;
  testing::TestRulesetPair test_ruleset_pair_;

  // Note: ADSF assumes a task runner is associated with the current thread.
  // Instantiate a MessageLoop on the current thread and use RunLoop to handle
  // the replies ADSF tasks generate.
  base::MessageLoop message_loop_;
  scoped_refptr<base::TestSimpleTaskRunner> blocking_task_runner_ =
      new base::TestSimpleTaskRunner;

  std::unique_ptr<VerifiedRulesetDealer::Handle> dealer_handle_;

  DISALLOW_COPY_AND_ASSIGN(AsyncDocumentSubresourceFilterTest);
};

namespace {

// TODO(csharrison): If more consumers need to test these callbacks at this
// granularity, consider moving these classes into
// async_document_subresource_filter_test_utils.
class TestCallbackReceiver {
 public:
  TestCallbackReceiver() = default;

  base::Closure GetClosure() {
    return base::Bind(&TestCallbackReceiver::Callback, base::Unretained(this));
  }
  int callback_count() const { return callback_count_; }

 private:
  void Callback() { ++callback_count_; }

  int callback_count_ = 0;

  DISALLOW_COPY_AND_ASSIGN(TestCallbackReceiver);
};

class LoadPolicyCallbackReceiver {
 public:
  LoadPolicyCallbackReceiver() = default;

  AsyncDocumentSubresourceFilter::LoadPolicyCallback GetCallback() {
    return base::Bind(&LoadPolicyCallbackReceiver::Callback,
                      base::Unretained(this));
  }
  void ExpectReceivedOnce(LoadPolicy load_policy) const {
    ASSERT_EQ(1, callback_count_);
    EXPECT_EQ(load_policy, last_load_policy_);
  }

 private:
  void Callback(LoadPolicy load_policy) {
    ++callback_count_;
    last_load_policy_ = load_policy;
  }

  int callback_count_ = 0;
  LoadPolicy last_load_policy_;

  DISALLOW_COPY_AND_ASSIGN(LoadPolicyCallbackReceiver);
};

}  // namespace

TEST_F(AsyncDocumentSubresourceFilterTest, ActivationStateIsReported) {
  dealer_handle()->SetRulesetFile(testing::TestRuleset::Open(ruleset()));
  auto ruleset_handle = CreateRulesetHandle();

  AsyncDocumentSubresourceFilter::InitializationParams params(
      GURL("http://example.com"), ActivationLevel::ENABLED, false);

  testing::TestActivationStateCallbackReceiver activation_state;
  auto filter = base::MakeUnique<AsyncDocumentSubresourceFilter>(
      ruleset_handle.get(), std::move(params), activation_state.GetCallback());

  RunUntilIdle();
  activation_state.ExpectReceivedOnce(
      ActivationState(ActivationLevel::ENABLED));
}

TEST_F(AsyncDocumentSubresourceFilterTest, ActivationStateIsComputedCorrectly) {
  dealer_handle()->SetRulesetFile(testing::TestRuleset::Open(ruleset()));
  auto ruleset_handle = CreateRulesetHandle();

  AsyncDocumentSubresourceFilter::InitializationParams params(
      GURL("http://whitelisted.subframe.com"), ActivationLevel::ENABLED, false);
  params.parent_document_origin = url::Origin(GURL("http://example.com"));

  testing::TestActivationStateCallbackReceiver activation_state;
  auto filter = base::MakeUnique<AsyncDocumentSubresourceFilter>(
      ruleset_handle.get(), std::move(params), activation_state.GetCallback());

  RunUntilIdle();

  ActivationState expected_activation_state(ActivationLevel::ENABLED);
  expected_activation_state.generic_blocking_rules_disabled = true;
  activation_state.ExpectReceivedOnce(expected_activation_state);
}

TEST_F(AsyncDocumentSubresourceFilterTest, DisabledForCorruptRuleset) {
  testing::TestRuleset::CorruptByFilling(ruleset(), 0, 100, 0xFF);
  dealer_handle()->SetRulesetFile(testing::TestRuleset::Open(ruleset()));

  auto ruleset_handle = CreateRulesetHandle();

  AsyncDocumentSubresourceFilter::InitializationParams params(
      GURL("http://example.com"), ActivationLevel::ENABLED, false);

  testing::TestActivationStateCallbackReceiver activation_state;
  auto filter = base::MakeUnique<AsyncDocumentSubresourceFilter>(
      ruleset_handle.get(), std::move(params), activation_state.GetCallback());

  RunUntilIdle();
  activation_state.ExpectReceivedOnce(
      ActivationState(ActivationLevel::DISABLED));
}

TEST_F(AsyncDocumentSubresourceFilterTest, GetLoadPolicyForSubdocument) {
  dealer_handle()->SetRulesetFile(testing::TestRuleset::Open(ruleset()));
  auto ruleset_handle = CreateRulesetHandle();

  AsyncDocumentSubresourceFilter::InitializationParams params(
      GURL("http://example.com"), ActivationLevel::ENABLED, false);

  testing::TestActivationStateCallbackReceiver activation_state;
  auto filter = base::MakeUnique<AsyncDocumentSubresourceFilter>(
      ruleset_handle.get(), std::move(params), activation_state.GetCallback());

  LoadPolicyCallbackReceiver load_policy_1;
  LoadPolicyCallbackReceiver load_policy_2;
  filter->GetLoadPolicyForSubdocument(GURL("http://example.com/allowed.html"),
                                      load_policy_1.GetCallback());
  filter->GetLoadPolicyForSubdocument(
      GURL("http://example.com/disallowed.html"), load_policy_2.GetCallback());

  RunUntilIdle();
  load_policy_1.ExpectReceivedOnce(LoadPolicy::ALLOW);
  load_policy_2.ExpectReceivedOnce(LoadPolicy::DISALLOW);
}

TEST_F(AsyncDocumentSubresourceFilterTest, FirstDisallowedLoadIsReported) {
  dealer_handle()->SetRulesetFile(testing::TestRuleset::Open(ruleset()));
  auto ruleset_handle = CreateRulesetHandle();

  TestCallbackReceiver first_disallowed_load_receiver;
  AsyncDocumentSubresourceFilter::InitializationParams params(
      GURL("http://example.com"), ActivationLevel::ENABLED, false);

  testing::TestActivationStateCallbackReceiver activation_state;
  auto filter = base::MakeUnique<AsyncDocumentSubresourceFilter>(
      ruleset_handle.get(), std::move(params), activation_state.GetCallback());
  filter->set_first_disallowed_load_callback(
      first_disallowed_load_receiver.GetClosure());

  LoadPolicyCallbackReceiver load_policy_1;
  filter->GetLoadPolicyForSubdocument(GURL("http://example.com/allowed.html"),
                                      load_policy_1.GetCallback());
  RunUntilIdle();
  load_policy_1.ExpectReceivedOnce(LoadPolicy::ALLOW);
  EXPECT_EQ(0, first_disallowed_load_receiver.callback_count());

  LoadPolicyCallbackReceiver load_policy_2;
  filter->GetLoadPolicyForSubdocument(
      GURL("http://example.com/disallowed.html"), load_policy_2.GetCallback());
  RunUntilIdle();
  load_policy_2.ExpectReceivedOnce(LoadPolicy::DISALLOW);
  EXPECT_EQ(0, first_disallowed_load_receiver.callback_count());

  filter->ReportDisallowedLoad();
  EXPECT_EQ(1, first_disallowed_load_receiver.callback_count());
  RunUntilIdle();
}

// Tests for ComputeActivationState:

class SubresourceFilterComputeActivationStateTest : public ::testing::Test {
 public:
  SubresourceFilterComputeActivationStateTest() {}

 protected:
  void SetUp() override {
    constexpr int32_t kDocument = proto::ACTIVATION_TYPE_DOCUMENT;
    constexpr int32_t kGenericBlock = proto::ACTIVATION_TYPE_GENERICBLOCK;

    std::vector<proto::UrlRule> rules;
    rules.push_back(testing::CreateWhitelistRuleForDocument(
        "child1.com", kDocument, {"parent1.com", "parent2.com"}));
    rules.push_back(testing::CreateWhitelistRuleForDocument(
        "child2.com", kGenericBlock, {"parent1.com", "parent2.com"}));
    rules.push_back(testing::CreateWhitelistRuleForDocument(
        "child3.com", kDocument | kGenericBlock,
        {"parent1.com", "parent2.com"}));

    testing::TestRulesetPair test_ruleset_pair;
    ASSERT_NO_FATAL_FAILURE(test_ruleset_creator_.CreateRulesetWithRules(
        rules, &test_ruleset_pair));
    ruleset_ = new MemoryMappedRuleset(
        testing::TestRuleset::Open(test_ruleset_pair.indexed));
  }

  static ActivationState MakeState(
      bool filtering_disabled_for_document,
      bool generic_blocking_rules_disabled = false,
      ActivationLevel activation_level = ActivationLevel::ENABLED) {
    ActivationState activation_state(activation_level);
    activation_state.filtering_disabled_for_document =
        filtering_disabled_for_document;
    activation_state.generic_blocking_rules_disabled =
        generic_blocking_rules_disabled;
    return activation_state;
  }

  const MemoryMappedRuleset* ruleset() { return ruleset_.get(); }

 private:
  testing::TestRulesetCreator test_ruleset_creator_;
  scoped_refptr<const MemoryMappedRuleset> ruleset_;

  DISALLOW_COPY_AND_ASSIGN(SubresourceFilterComputeActivationStateTest);
};

TEST_F(SubresourceFilterComputeActivationStateTest,
       ActivationBitsCorrectlyPropagateToChildDocument) {
  // Make sure that the |generic_blocking_rules_disabled| flag is disregarded
  // when |filtering_disabled_for_document| is true.
  ASSERT_EQ(MakeState(true, false), MakeState(true, true));

  // TODO(pkalinnikov): Find a short way to express all these tests.
  const struct {
    const char* document_url;
    const char* parent_document_origin;
    ActivationState parent_activation;
    ActivationState expected_activation_state;
  } kTestCases[] = {
      {"http://example.com", "http://example.com", MakeState(false, false),
       MakeState(false, false)},
      {"http://example.com", "http://example.com", MakeState(false, true),
       MakeState(false, true)},
      {"http://example.com", "http://example.com", MakeState(true, false),
       MakeState(true)},
      {"http://example.com", "http://example.com", MakeState(true, true),
       MakeState(true)},

      {"http://child1.com", "http://parrrrent1.com", MakeState(false, false),
       MakeState(false, false)},
      {"http://child1.com", "http://parent1.com", MakeState(false, false),
       MakeState(true, false)},
      {"http://child1.com", "http://parent2.com", MakeState(false, false),
       MakeState(true, false)},
      {"http://child1.com", "http://parent2.com", MakeState(true, false),
       MakeState(true)},
      {"http://child1.com", "http://parent2.com", MakeState(false, true),
       MakeState(true)},

      {"http://child2.com", "http://parent1.com", MakeState(false, false),
       MakeState(false, true)},
      {"http://child2.com", "http://parent1.com", MakeState(false, true),
       MakeState(false, true)},
      {"http://child2.com", "http://parent1.com", MakeState(true, false),
       MakeState(true)},
      {"http://child2.com", "http://parent1.com", MakeState(true, true),
       MakeState(true)},

      {"http://child3.com", "http://parent1.com", MakeState(false, false),
       MakeState(true)},
      {"http://child3.com", "http://parent1.com", MakeState(false, true),
       MakeState(true)},
      {"http://child3.com", "http://parent1.com", MakeState(true, false),
       MakeState(true)},
      {"http://child3.com", "http://parent1.com", MakeState(true, true),
       MakeState(true)},
  };

  for (size_t i = 0, size = arraysize(kTestCases); i != size; ++i) {
    SCOPED_TRACE(::testing::Message() << "Test number: " << i);
    const auto& test_case = kTestCases[i];

    GURL document_url(test_case.document_url);
    url::Origin parent_document_origin(GURL(test_case.parent_document_origin));
    ActivationState activation_state =
        ComputeActivationState(document_url, parent_document_origin,
                               test_case.parent_activation, ruleset());
    EXPECT_EQ(test_case.expected_activation_state, activation_state);
  }
}

}  // namespace subresource_filter
