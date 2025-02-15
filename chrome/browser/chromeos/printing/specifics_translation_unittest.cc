// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/memory/ptr_util.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/printing/specifics_translation.h"
#include "chromeos/printing/printer_configuration.h"
#include "components/sync/protocol/printer_specifics.pb.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

constexpr char kId[] = "UNIQUE_ID";
constexpr char kDisplayName[] = "Best Printer Ever";
constexpr char kDescription[] = "The green one";
constexpr char kManufacturer[] = "Manufacturer";
constexpr char kModel[] = "MODEL";
constexpr char kUri[] = "ipps://notaprinter.chromium.org/ipp/print";
constexpr char kUuid[] = "UUIDUUIDUUID";
const base::Time kUpdateTime = base::Time::FromInternalValue(22114455660000);

constexpr char kUserSuppliedPPD[] = "file://foo/bar/baz/eeaaaffccdd00";
constexpr char kEffectiveMakeAndModel[] = "Manufacturer Model T1000";

}  // namespace

namespace chromeos {
namespace printing {

TEST(SpecificsTranslationTest, SpecificsToPrinter) {
  sync_pb::PrinterSpecifics specifics;
  specifics.set_id(kId);
  specifics.set_display_name(kDisplayName);
  specifics.set_description(kDescription);
  specifics.set_manufacturer(kManufacturer);
  specifics.set_model(kModel);
  specifics.set_uri(kUri);
  specifics.set_uuid(kUuid);
  specifics.set_updated_timestamp(kUpdateTime.ToJavaTime());

  sync_pb::PrinterPPDReference ppd;
  ppd.set_effective_make_and_model(kEffectiveMakeAndModel);
  *specifics.mutable_ppd_reference() = ppd;

  std::unique_ptr<Printer> result = SpecificsToPrinter(specifics);
  EXPECT_EQ(kId, result->id());
  EXPECT_EQ(kDisplayName, result->display_name());
  EXPECT_EQ(kDescription, result->description());
  EXPECT_EQ(kManufacturer, result->manufacturer());
  EXPECT_EQ(kModel, result->model());
  EXPECT_EQ(kUri, result->uri());
  EXPECT_EQ(kUuid, result->uuid());
  EXPECT_EQ(kUpdateTime, result->last_updated());

  EXPECT_EQ(kEffectiveMakeAndModel,
            result->ppd_reference().effective_make_and_model);
  EXPECT_FALSE(result->IsIppEverywhere());
}

TEST(SpecificsTranslationTest, PrinterToSpecifics) {
  Printer printer;
  printer.set_id(kId);
  printer.set_display_name(kDisplayName);
  printer.set_description(kDescription);
  printer.set_manufacturer(kManufacturer);
  printer.set_model(kModel);
  printer.set_uri(kUri);
  printer.set_uuid(kUuid);

  Printer::PpdReference ppd;
  ppd.effective_make_and_model = kEffectiveMakeAndModel;
  *printer.mutable_ppd_reference() = ppd;

  std::unique_ptr<sync_pb::PrinterSpecifics> result =
      PrinterToSpecifics(printer);
  EXPECT_EQ(kId, result->id());
  EXPECT_EQ(kDisplayName, result->display_name());
  EXPECT_EQ(kDescription, result->description());
  EXPECT_EQ(kManufacturer, result->manufacturer());
  EXPECT_EQ(kModel, result->model());
  EXPECT_EQ(kUri, result->uri());
  EXPECT_EQ(kUuid, result->uuid());

  EXPECT_EQ(kEffectiveMakeAndModel,
            result->ppd_reference().effective_make_and_model());
}

TEST(SpecificsTranslationTest, SpecificsToPrinterRoundTrip) {
  Printer printer;
  printer.set_id(kId);
  printer.set_display_name(kDisplayName);
  printer.set_description(kDescription);
  printer.set_manufacturer(kManufacturer);
  printer.set_model(kModel);
  printer.set_uri(kUri);
  printer.set_uuid(kUuid);

  Printer::PpdReference ppd;
  ppd.autoconf = true;
  *printer.mutable_ppd_reference() = ppd;

  std::unique_ptr<sync_pb::PrinterSpecifics> temp = PrinterToSpecifics(printer);
  std::unique_ptr<Printer> result = SpecificsToPrinter(*temp);

  EXPECT_EQ(kId, result->id());
  EXPECT_EQ(kDisplayName, result->display_name());
  EXPECT_EQ(kDescription, result->description());
  EXPECT_EQ(kManufacturer, result->manufacturer());
  EXPECT_EQ(kModel, result->model());
  EXPECT_EQ(kUri, result->uri());
  EXPECT_EQ(kUuid, result->uuid());

  EXPECT_TRUE(result->ppd_reference().effective_make_and_model.empty());
  EXPECT_TRUE(result->ppd_reference().autoconf);
}

TEST(SpecificsTranslationTest, MergePrinterToSpecifics) {
  sync_pb::PrinterSpecifics original;
  original.set_id(kId);
  original.mutable_ppd_reference()->set_autoconf(true);

  Printer printer(kId);
  printer.mutable_ppd_reference()->effective_make_and_model =
      kEffectiveMakeAndModel;

  MergePrinterToSpecifics(printer, &original);

  EXPECT_EQ(kId, original.id());
  EXPECT_EQ(kEffectiveMakeAndModel,
            original.ppd_reference().effective_make_and_model());

  // Verify that autoconf is cleared.
  EXPECT_FALSE(original.ppd_reference().autoconf());
}

// Tests that the autoconf value overrides other PpdReference fields.
TEST(SpecificsTranslationTest, AutoconfOverrides) {
  sync_pb::PrinterSpecifics original;
  original.set_id(kId);
  auto* ppd_reference = original.mutable_ppd_reference();
  ppd_reference->set_autoconf(true);
  ppd_reference->set_user_supplied_ppd_url(kUserSuppliedPPD);

  auto printer = SpecificsToPrinter(original);

  EXPECT_TRUE(printer->ppd_reference().autoconf);
  EXPECT_TRUE(printer->ppd_reference().user_supplied_ppd_url.empty());
  EXPECT_TRUE(printer->ppd_reference().effective_make_and_model.empty());
}

// Tests that user_supplied_ppd_url overwrites other PpdReference fields if
// autoconf is false.
TEST(SpecificsTranslationTest, UserSuppliedOverrides) {
  sync_pb::PrinterSpecifics original;
  original.set_id(kId);
  auto* ppd_reference = original.mutable_ppd_reference();
  ppd_reference->set_user_supplied_ppd_url(kUserSuppliedPPD);
  ppd_reference->set_effective_make_and_model(kEffectiveMakeAndModel);

  auto printer = SpecificsToPrinter(original);

  EXPECT_FALSE(printer->ppd_reference().autoconf);
  EXPECT_FALSE(printer->ppd_reference().user_supplied_ppd_url.empty());
  EXPECT_TRUE(printer->ppd_reference().effective_make_and_model.empty());
}

}  // namespace printing
}  // namespace chromeos
