// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/persistent_system_profile.h"

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/persistent_memory_allocator.h"
#include "base/rand_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace metrics {

class PersistentSystemProfileTest : public testing::Test {
 public:
  const int32_t kAllocatorMemorySize = 1 << 20;  // 1 MiB

  PersistentSystemProfileTest() {}
  ~PersistentSystemProfileTest() override {}

  void SetUp() override {
    memory_allocator_ = base::MakeUnique<base::LocalPersistentMemoryAllocator>(
        kAllocatorMemorySize, 0, "");
    records_ = base::MakeUnique<PersistentSystemProfile::RecordAllocator>(
        memory_allocator_.get());
    persistent_profile_.RegisterPersistentAllocator(memory_allocator_.get());
  }

  void TearDown() override {
    persistent_profile_.DeregisterPersistentAllocator(memory_allocator_.get());
    records_.reset();
    memory_allocator_.reset();
  }

  void WriteRecord(uint8_t type, const std::string& record) {
    persistent_profile_.allocators_[0].Write(
        static_cast<PersistentSystemProfile::RecordType>(type), record);
  }

  bool ReadRecord(uint8_t* type, std::string* record) {
    PersistentSystemProfile::RecordType rec_type;

    bool success = records_->Read(&rec_type, record);
    *type = rec_type;  // Convert to uint8_t for testing.
    return success;
  }

  base::PersistentMemoryAllocator* memory_allocator() {
    return memory_allocator_.get();
  }

  PersistentSystemProfile* persistent_profile() { return &persistent_profile_; }

 private:
  PersistentSystemProfile persistent_profile_;
  std::unique_ptr<base::PersistentMemoryAllocator> memory_allocator_;
  std::unique_ptr<PersistentSystemProfile::RecordAllocator> records_;

  DISALLOW_COPY_AND_ASSIGN(PersistentSystemProfileTest);
};

TEST_F(PersistentSystemProfileTest, Create) {
  uint32_t type;
  base::PersistentMemoryAllocator::Iterator iter(memory_allocator());
  base::PersistentMemoryAllocator::Reference ref = iter.GetNext(&type);
  DCHECK(ref);
  DCHECK_NE(0U, type);
}

TEST_F(PersistentSystemProfileTest, RecordSplitting) {
  const size_t kRecordSize = 100 << 10;  // 100 KiB
  std::vector<char> buffer;
  buffer.resize(kRecordSize);
  base::RandBytes(&buffer[0], kRecordSize);

  WriteRecord(42, std::string(&buffer[0], kRecordSize));

  uint8_t type;
  std::string record;
  ASSERT_TRUE(ReadRecord(&type, &record));
  EXPECT_EQ(42U, type);
  ASSERT_EQ(kRecordSize, record.size());
  for (size_t i = 0; i < kRecordSize; ++i)
    EXPECT_EQ(buffer[i], record[i]);
}

TEST_F(PersistentSystemProfileTest, ProfileStorage) {
  SystemProfileProto proto1;
  SystemProfileProto::FieldTrial* trial = proto1.add_field_trial();
  trial->set_name_id(123);
  trial->set_group_id(456);

  persistent_profile()->SetSystemProfile(proto1, false);

  SystemProfileProto proto2;
  ASSERT_TRUE(PersistentSystemProfile::HasSystemProfile(*memory_allocator()));
  ASSERT_TRUE(
      PersistentSystemProfile::GetSystemProfile(*memory_allocator(), &proto2));
  ASSERT_EQ(1, proto2.field_trial_size());
  EXPECT_EQ(123U, proto2.field_trial(0).name_id());
  EXPECT_EQ(456U, proto2.field_trial(0).group_id());

  // Check that the profile can be overwritten.

  trial = proto1.add_field_trial();
  trial->set_name_id(78);
  trial->set_group_id(90);

  persistent_profile()->SetSystemProfile(proto1, true);

  ASSERT_TRUE(
      PersistentSystemProfile::GetSystemProfile(*memory_allocator(), &proto2));
  ASSERT_EQ(2, proto2.field_trial_size());
  EXPECT_EQ(123U, proto2.field_trial(0).name_id());
  EXPECT_EQ(456U, proto2.field_trial(0).group_id());
  EXPECT_EQ(78U, proto2.field_trial(1).name_id());
  EXPECT_EQ(90U, proto2.field_trial(1).group_id());

  // Check that the profile won't be overwritten by a new non-complete profile.

  trial = proto1.add_field_trial();
  trial->set_name_id(0xC0DE);
  trial->set_group_id(0xFEED);

  persistent_profile()->SetSystemProfile(proto1, false);

  ASSERT_TRUE(
      PersistentSystemProfile::GetSystemProfile(*memory_allocator(), &proto2));
  ASSERT_EQ(2, proto2.field_trial_size());
  EXPECT_EQ(123U, proto2.field_trial(0).name_id());
  EXPECT_EQ(456U, proto2.field_trial(0).group_id());
  EXPECT_EQ(78U, proto2.field_trial(1).name_id());
  EXPECT_EQ(90U, proto2.field_trial(1).group_id());
}

}  // namespace metrics
