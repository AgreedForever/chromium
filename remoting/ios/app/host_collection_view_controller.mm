// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#import "remoting/ios/app/host_collection_view_controller.h"

#import "ios/third_party/material_components_ios/src/components/Ink/src/MaterialInk.h"
#import "ios/third_party/material_components_ios/src/components/NavigationBar/src/MaterialNavigationBar.h"
#import "ios/third_party/material_components_ios/src/components/ShadowElevations/src/MaterialShadowElevations.h"
#import "ios/third_party/material_components_ios/src/components/ShadowLayer/src/MaterialShadowLayer.h"
#import "remoting/ios/app/host_collection_header_view.h"

static NSString* const kReusableIdentifierItem =
    @"remotingHostCollectionViewControllerItem";

static CGFloat kHostCollectionViewControllerCellHeight = 70.f;
static CGFloat kHostCollectionHeaderViewHeight = 25.f;

@implementation HostCollectionViewController

@synthesize delegate = _delegate;
@synthesize flexHeaderContainerViewController =
    _flexHeaderContainerViewController;

- (instancetype)initWithCollectionViewLayout:(UICollectionViewLayout*)layout {
  self = [super initWithCollectionViewLayout:layout];
  if (self) {
    self.collectionView.backgroundColor = [UIColor clearColor];
    [self.collectionView registerClass:[HostCollectionViewCell class]
            forCellWithReuseIdentifier:NSStringFromClass(
                                           [HostCollectionViewCell class])];

    [self.collectionView registerClass:[HostCollectionHeaderView class]
            forSupplementaryViewOfKind:UICollectionElementKindSectionHeader
                   withReuseIdentifier:UICollectionElementKindSectionHeader];
  }
  return self;
}

#pragma mark - UIViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  self.styler.cellStyle = MDCCollectionViewCellStyleDefault;
  self.styler.cellLayoutType = MDCCollectionViewCellLayoutTypeList;
}

- (void)viewWillTransitionToSize:(CGSize)size
       withTransitionCoordinator:
           (id<UIViewControllerTransitionCoordinator>)coordinator {
  [self.collectionView.collectionViewLayout invalidateLayout];
}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  [self.collectionView.collectionViewLayout invalidateLayout];
}

#pragma mark - UICollectionViewDataSource

- (NSInteger)collectionView:(UICollectionView*)collectionView
     numberOfItemsInSection:(NSInteger)section {
  return [_delegate getHostCount];
}

- (UICollectionViewCell*)collectionView:(UICollectionView*)collectionView
                 cellForItemAtIndexPath:(NSIndexPath*)indexPath {
  HostCollectionViewCell* cell =
      [collectionView dequeueReusableCellWithReuseIdentifier:
                          NSStringFromClass([HostCollectionViewCell class])
                                                forIndexPath:indexPath];
  HostInfo* host = [_delegate getHostAtIndexPath:indexPath];
  if (host) {
    [cell populateContentWithHostInfo:host];
  }
  return cell;
}

- (UICollectionReusableView*)collectionView:(UICollectionView*)collectionView
          viewForSupplementaryElementOfKind:(NSString*)kind
                                atIndexPath:(NSIndexPath*)indexPath {
  HostCollectionHeaderView* supplementaryView =
      [collectionView dequeueReusableSupplementaryViewOfKind:kind
                                         withReuseIdentifier:kind
                                                forIndexPath:indexPath];
  if ([kind isEqualToString:UICollectionElementKindSectionHeader]) {
    supplementaryView.text = @"Remote devices";
  }
  return supplementaryView;
}

#pragma mark - UICollectionViewDelegate

- (void)collectionView:(UICollectionView*)collectionView
    didSelectItemAtIndexPath:(NSIndexPath*)indexPath {
  [super collectionView:collectionView didSelectItemAtIndexPath:indexPath];
  HostCollectionViewCell* cell = (HostCollectionViewCell*)[collectionView
      cellForItemAtIndexPath:indexPath];
  [_delegate didSelectCell:cell
                completion:^{
                }];
}

#pragma mark - MDCCollectionViewStylingDelegate

- (CGFloat)collectionView:(UICollectionView*)collectionView
    cellHeightAtIndexPath:(NSIndexPath*)indexPath {
  return kHostCollectionViewControllerCellHeight;
}

#pragma mark - UIScrollViewDelegate

- (void)scrollViewDidScroll:(UIScrollView*)scrollView {
  [self.flexHeaderContainerViewController.headerViewController
      scrollViewDidScroll:scrollView];
}

#pragma mark - UICollectionViewDelegateFlowLayout

- (CGSize)collectionView:(UICollectionView*)collectionView
                             layout:
                                 (UICollectionViewLayout*)collectionViewLayout
    referenceSizeForHeaderInSection:(NSInteger)section {
  return CGSizeMake(collectionView.bounds.size.width,
                    kHostCollectionHeaderViewHeight);
}

#pragma mark - Private

- (void)setFlexHeaderContainerViewController:
    (MDCFlexibleHeaderContainerViewController*)
        flexHeaderContainerViewController {
  _flexHeaderContainerViewController = flexHeaderContainerViewController;
  MDCFlexibleHeaderView* headerView =
      _flexHeaderContainerViewController.headerViewController.headerView;
  headerView.trackingScrollView = self.collectionView;
  headerView.backgroundColor = [UIColor clearColor];

  // Use a custom shadow under the flexible header.
  MDCShadowLayer* shadowLayer = [MDCShadowLayer layer];
  [headerView setShadowLayer:shadowLayer
      intensityDidChangeBlock:^(CALayer* layer, CGFloat intensity) {
        CGFloat elevation = MDCShadowElevationAppBar * intensity;
        [(MDCShadowLayer*)layer setElevation:elevation];
      }];
}

@end
