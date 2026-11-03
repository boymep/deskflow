/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#import "platform/OSXWindowSwitcherPanel.h"

#if !__has_feature(objc_arc)
#error "OSXWindowSwitcherPanel.mm must be compiled with ARC (-fobjc-arc)"
#endif

namespace {

// layout, in points
constexpr CGFloat kIconSize = 64;
constexpr CGFloat kTitleHeight = 16;
constexpr CGFloat kCellSpacing = 6;
constexpr CGFloat kPanelPadding = 16;
constexpr CGFloat kHeaderHeight = 20;
constexpr CGFloat kHeaderGap = 10;
constexpr CGFloat kCornerRadius = 18;
constexpr CGFloat kSelectionRadius = 10;
constexpr NSUInteger kMaxVisibleRows = 3;

// icon-only tiles
constexpr CGFloat kCellPadding = 12;
constexpr CGFloat kCellWidth = 128;
constexpr CGFloat kCellHeight = kCellPadding + kIconSize + 8 + kTitleHeight + kCellPadding;
constexpr NSUInteger kMaxColumns = 8;

// thumbnail tiles: preview on top, app icon overlaid at its bottom, title below
constexpr CGFloat kThumbPadding = 10;
constexpr CGFloat kThumbBoxWidth = 200;
constexpr CGFloat kThumbBoxHeight = 124;
constexpr CGFloat kThumbTitleGap = 6;
constexpr CGFloat kThumbCellWidth = kThumbBoxWidth + 2 * kThumbPadding;
constexpr CGFloat kThumbCellHeight = kThumbPadding + kThumbBoxHeight + kThumbTitleGap + kTitleHeight + kThumbPadding;
constexpr CGFloat kThumbRadius = 6;
constexpr CGFloat kOverlayIconSize = 32;
constexpr NSUInteger kThumbMaxColumns = 7;

struct GridLayout
{
  CGFloat cellWidth;
  CGFloat cellHeight;
  NSUInteger maxColumns;
};

GridLayout gridLayout(BOOL thumbnails)
{
  return thumbnails ? GridLayout{kThumbCellWidth, kThumbCellHeight, kThumbMaxColumns}
                    : GridLayout{kCellWidth, kCellHeight, kMaxColumns};
}

// largest rect with \p size's aspect ratio inside \p box, centered, never scaled up
NSRect aspectFit(NSSize size, NSRect box)
{
  if (size.width <= 0 || size.height <= 0) {
    return box;
  }
  const CGFloat factor = MIN(MIN(box.size.width / size.width, box.size.height / size.height), 1.0);
  const CGFloat w = round(size.width * factor);
  const CGFloat h = round(size.height * factor);
  return NSMakeRect(round(NSMidX(box) - w / 2), round(NSMidY(box) - h / 2), w, h);
}

NSImage *roundedMask(CGFloat radius)
{
  const CGFloat side = radius * 2 + 1;
  NSImage *mask = [NSImage imageWithSize:NSMakeSize(side, side)
                                 flipped:NO
                          drawingHandler:^BOOL(NSRect rect) {
                            [[NSColor blackColor] setFill];
                            [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius] fill];
                            return YES;
                          }];
  mask.capInsets = NSEdgeInsetsMake(radius, radius, radius, radius);
  mask.resizingMode = NSImageResizingModeStretch;
  return mask;
}

NSString *displayTitle(DeskflowSwitcherItem *item)
{
  return item.title.length > 0 ? item.title : (item.appName ?: @"");
}

} // namespace

@implementation DeskflowSwitcherItem
- (instancetype)init
{
  if ((self = [super init])) {
    _title = @"";
    _appName = @"";
  }
  return self;
}
@end

//! Tinted rounded background drawn over the blur, so the panel reads well on any wallpaper.
@interface DeskflowSwitcherBackgroundView : NSView
@end

@implementation DeskflowSwitcherBackgroundView
- (void)drawRect:(NSRect)dirtyRect
{
  const NSRect bounds = NSInsetRect(self.bounds, 0.5, 0.5);
  NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:bounds xRadius:kCornerRadius yRadius:kCornerRadius];
  [[NSColor.windowBackgroundColor colorWithAlphaComponent:0.55] setFill];
  [path fill];
  [[NSColor.separatorColor colorWithAlphaComponent:0.4] setStroke];
  path.lineWidth = 1;
  [path stroke];
}
@end

//! One grid cell: icon or thumbnail, one-line title, selection highlight.
@interface DeskflowSwitcherItemView : NSView
@property(nonatomic, strong) DeskflowSwitcherItem *item;
@property(nonatomic, assign) BOOL selected;
@property(nonatomic, assign) BOOL thumbnails; //!< thumbnail tile layout
@property(nonatomic, strong) NSTextField *titleField;
@end

@implementation DeskflowSwitcherItemView

- (instancetype)initWithItem:(DeskflowSwitcherItem *)item frame:(NSRect)frame thumbnails:(BOOL)thumbnails
{
  if ((self = [super initWithFrame:frame])) {
    _item = item;
    _thumbnails = thumbnails;
    const CGFloat padding = thumbnails ? kThumbPadding : kCellPadding;

    _titleField = [NSTextField labelWithString:displayTitle(item)];
    _titleField.font = [NSFont systemFontOfSize:12];
    _titleField.alignment = NSTextAlignmentCenter;
    _titleField.lineBreakMode = NSLineBreakByTruncatingTail;
    _titleField.maximumNumberOfLines = 1;
    _titleField.cell.truncatesLastVisibleLine = YES;
    _titleField.textColor = (item.minimized || item.appHidden) ? NSColor.secondaryLabelColor : NSColor.labelColor;
    _titleField.frame = NSMakeRect(6, padding - 2, frame.size.width - 12, kTitleHeight + 2);
    [self addSubview:_titleField];
  }
  return self;
}

- (BOOL)isFlipped
{
  return NO;
}

- (void)setSelected:(BOOL)selected
{
  if (_selected != selected) {
    _selected = selected;
    self.needsDisplay = YES;
  }
}

- (NSRect)iconRect
{
  const NSRect b = self.bounds;
  return NSMakeRect(
      floor((b.size.width - kIconSize) / 2), b.size.height - kCellPadding - kIconSize, kIconSize, kIconSize
  );
}

- (void)drawRect:(NSRect)dirtyRect
{
  if (self.selected) {
    NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 1.5, 1.5)
                                                         xRadius:kSelectionRadius
                                                         yRadius:kSelectionRadius];
    [[NSColor.labelColor colorWithAlphaComponent:0.12] setFill];
    [path fill];
    [[NSColor.controlAccentColor colorWithAlphaComponent:0.9] setStroke];
    path.lineWidth = 2.5;
    [path stroke];
  }

  if (self.thumbnails) {
    [self drawThumbnailTile];
    return;
  }

  const NSRect iconRect = [self iconRect];
  NSImage *icon = self.item.icon ?: [NSImage imageNamed:NSImageNameApplicationIcon];
  const CGFloat alpha = (self.item.minimized || self.item.appHidden) ? 0.45 : 1.0;
  [icon drawInRect:iconRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:alpha];

  if (self.item.minimized) {
    [self drawMinimizedBadgeAt:NSMakePoint(NSMaxX(iconRect) + 2, NSMinY(iconRect) - 2)];
  }
}

//! A small dash in a pill whose bottom right corner is \p corner.
- (void)drawMinimizedBadgeAt:(NSPoint)corner
{
  const NSRect badge = NSMakeRect(corner.x - 24, corner.y, 24, 16);
  NSBezierPath *pill = [NSBezierPath bezierPathWithRoundedRect:badge xRadius:8 yRadius:8];
  [NSColor.controlAccentColor setFill];
  [pill fill];
  [[NSColor colorWithWhite:1 alpha:0.95] setFill];
  NSRectFill(NSMakeRect(NSMidX(badge) - 5, NSMidY(badge) - 1, 10, 2));
}

- (NSRect)thumbnailBox
{
  const NSRect b = self.bounds;
  return NSMakeRect(
      floor((b.size.width - kThumbBoxWidth) / 2), b.size.height - kThumbPadding - kThumbBoxHeight, kThumbBoxWidth,
      kThumbBoxHeight
  );
}

- (void)drawThumbnailTile
{
  DeskflowSwitcherItem *item = self.item;
  NSImage *icon = item.icon ?: [NSImage imageNamed:NSImageNameApplicationIcon];
  const BOOL dimmed = item.minimized || item.appHidden;
  const NSRect box = [self thumbnailBox];

  if (item.thumbnail == nil) {
    // placeholder until the preview arrives (or if it cannot be captured): the app icon
    NSBezierPath *frame = [NSBezierPath bezierPathWithRoundedRect:box xRadius:kThumbRadius yRadius:kThumbRadius];
    [[NSColor.labelColor colorWithAlphaComponent:0.05] setFill];
    [frame fill];
    const NSRect iconRect = NSMakeRect(
        floor(NSMidX(box) - kIconSize / 2), floor(NSMidY(box) - kIconSize / 2), kIconSize, kIconSize
    );
    [icon drawInRect:iconRect
            fromRect:NSZeroRect
           operation:NSCompositingOperationSourceOver
            fraction:dimmed ? 0.45 : 1.0];
    if (item.minimized) {
      [self drawMinimizedBadgeAt:NSMakePoint(NSMaxX(iconRect) + 2, NSMinY(iconRect) - 2)];
    }
    return;
  }

  const NSRect imageRect = aspectFit(item.thumbnail.size, box);
  NSBezierPath *clip = [NSBezierPath bezierPathWithRoundedRect:imageRect xRadius:kThumbRadius yRadius:kThumbRadius];
  [NSGraphicsContext saveGraphicsState];
  [clip addClip];
  [item.thumbnail drawInRect:imageRect
                    fromRect:NSZeroRect
                   operation:NSCompositingOperationSourceOver
                    fraction:dimmed ? 0.55 : 1.0];
  [NSGraphicsContext restoreGraphicsState];
  [[NSColor.separatorColor colorWithAlphaComponent:0.6] setStroke];
  NSBezierPath *border = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(imageRect, 0.5, 0.5)
                                                         xRadius:kThumbRadius
                                                         yRadius:kThumbRadius];
  border.lineWidth = 1;
  [border stroke];

  // app icon overlaid at the bottom center of the preview, like Windows Alt+Tab
  const NSRect iconRect = NSMakeRect(
      floor(NSMidX(imageRect) - kOverlayIconSize / 2), NSMinY(imageRect) + 4, kOverlayIconSize, kOverlayIconSize
  );
  [NSGraphicsContext saveGraphicsState];
  NSShadow *shadow = [NSShadow new];
  shadow.shadowColor = [NSColor colorWithWhite:0 alpha:0.45];
  shadow.shadowBlurRadius = 4;
  shadow.shadowOffset = NSMakeSize(0, -1);
  [shadow set];
  [icon drawInRect:iconRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
  [NSGraphicsContext restoreGraphicsState];
  if (item.minimized) {
    [self drawMinimizedBadgeAt:NSMakePoint(NSMaxX(imageRect) - 4, NSMinY(imageRect) + 4)];
  }
}

@end

@interface DeskflowSwitcherPanel ()
@property(nonatomic, strong) NSPanel *panel;
@property(nonatomic, strong) NSArray<DeskflowSwitcherItemView *> *itemViews;
@property(nonatomic, strong) NSTextField *header;
@property(nonatomic, strong) NSScrollView *scrollView;
@property(nonatomic, assign, readwrite) NSUInteger selection;
@property(nonatomic, assign, readwrite) CGFloat backingScale;
@end

@implementation DeskflowSwitcherPanel

+ (NSSize)thumbnailBoxSize
{
  return NSMakeSize(kThumbBoxWidth, kThumbBoxHeight);
}

- (NSPanel *)ensurePanel
{
  if (self.panel == nil) {
    NSPanel *panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 200, 100)
                                                styleMask:NSWindowStyleMaskBorderless |
                                                          NSWindowStyleMaskNonactivatingPanel
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    panel.releasedWhenClosed = NO;
    panel.opaque = NO;
    panel.backgroundColor = NSColor.clearColor;
    panel.hasShadow = YES;
    panel.level = NSPopUpMenuWindowLevel;
    panel.hidesOnDeactivate = NO;
    panel.becomesKeyOnlyIfNeeded = YES;
    panel.floatingPanel = YES;
    panel.ignoresMouseEvents = YES;
    panel.animationBehavior = NSWindowAnimationBehaviorNone;
    panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                               NSWindowCollectionBehaviorFullScreenAuxiliary |
                               NSWindowCollectionBehaviorStationary | NSWindowCollectionBehaviorIgnoresCycle;
    self.panel = panel;
  }
  return self.panel;
}

- (BOOL)visible
{
  return self.panel != nil && self.panel.visible;
}

+ (NSScreen *)screenWithMouse
{
  const NSPoint mouse = [NSEvent mouseLocation];
  for (NSScreen *screen in [NSScreen screens]) {
    if (NSMouseInRect(mouse, screen.frame, NO)) {
      return screen;
    }
  }
  return [NSScreen mainScreen] ?: [NSScreen screens].firstObject;
}

- (NSView *)buildContentForItems:(NSArray<DeskflowSwitcherItem *> *)items
                       selection:(NSUInteger)selection
                     screenFrame:(NSRect)screenFrame
                      thumbnails:(BOOL)thumbnails
{
  const NSUInteger count = items.count;
  const GridLayout layout = gridLayout(thumbnails);
  const CGFloat cellWidth = layout.cellWidth;
  const CGFloat cellHeight = layout.cellHeight;

  // as many columns as fit on the screen (at most layout.maxColumns), rows wrap, extra rows scroll
  const CGFloat usableWidth = screenFrame.size.width * 0.9 - 2 * kPanelPadding;
  NSUInteger fitColumns = (NSUInteger)MAX(1.0, floor((usableWidth + kCellSpacing) / (cellWidth + kCellSpacing)));
  const NSUInteger columns = MAX((NSUInteger)1, MIN(MIN(count, layout.maxColumns), fitColumns));
  const NSUInteger rows = count == 0 ? 1 : (count + columns - 1) / columns;
  const CGFloat usableHeight = screenFrame.size.height * 0.8 - 2 * kPanelPadding - kHeaderHeight - kHeaderGap;
  NSUInteger fitRows = (NSUInteger)MAX(1.0, floor((usableHeight + kCellSpacing) / (cellHeight + kCellSpacing)));
  const NSUInteger visibleRows = MIN(rows, MIN(kMaxVisibleRows, fitRows));

  const CGFloat gridWidth = columns * cellWidth + (columns - 1) * kCellSpacing;
  const CGFloat gridHeight = rows * cellHeight + (rows - 1) * kCellSpacing;
  const CGFloat visibleGridHeight = visibleRows * cellHeight + (visibleRows - 1) * kCellSpacing;
  const NSSize size = NSMakeSize(
      gridWidth + 2 * kPanelPadding, visibleGridHeight + 2 * kPanelPadding + kHeaderHeight + kHeaderGap
  );

  NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];

  NSVisualEffectView *blur = [[NSVisualEffectView alloc] initWithFrame:root.bounds];
  blur.material = NSVisualEffectMaterialHUDWindow;
  blur.blendingMode = NSVisualEffectBlendingModeBehindWindow;
  blur.state = NSVisualEffectStateActive;
  blur.maskImage = roundedMask(kCornerRadius);
  blur.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [root addSubview:blur];

  DeskflowSwitcherBackgroundView *background = [[DeskflowSwitcherBackgroundView alloc] initWithFrame:root.bounds];
  background.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [root addSubview:background];

  NSTextField *header = [NSTextField labelWithString:@""];
  header.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
  header.textColor = NSColor.labelColor;
  header.alignment = NSTextAlignmentCenter;
  header.lineBreakMode = NSLineBreakByTruncatingTail;
  header.maximumNumberOfLines = 1;
  header.frame =
      NSMakeRect(kPanelPadding, size.height - kPanelPadding - kHeaderHeight, gridWidth, kHeaderHeight);
  [root addSubview:header];

  // flipped document view so items fill top-down
  NSView *grid = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, gridWidth, gridHeight)];
  NSMutableArray<DeskflowSwitcherItemView *> *views = [NSMutableArray arrayWithCapacity:count];
  for (NSUInteger i = 0; i < count; ++i) {
    const NSUInteger row = i / columns;
    const NSUInteger column = i % columns;
    const CGFloat x = column * (cellWidth + kCellSpacing);
    const CGFloat y = gridHeight - (row + 1) * cellHeight - row * kCellSpacing;
    DeskflowSwitcherItemView *view =
        [[DeskflowSwitcherItemView alloc] initWithItem:items[i]
                                                  frame:NSMakeRect(x, y, cellWidth, cellHeight)
                                             thumbnails:thumbnails];
    [grid addSubview:view];
    [views addObject:view];
  }

  NSScrollView *scroll =
      [[NSScrollView alloc] initWithFrame:NSMakeRect(kPanelPadding, kPanelPadding, gridWidth, visibleGridHeight)];
  scroll.drawsBackground = NO;
  scroll.borderType = NSNoBorder;
  scroll.hasVerticalScroller = rows > visibleRows;
  scroll.autohidesScrollers = YES;
  scroll.scrollerStyle = NSScrollerStyleOverlay;
  scroll.documentView = grid;
  [root addSubview:scroll];

  self.itemViews = views;
  self.header = header;
  self.scrollView = scroll;
  [self selectIndex:selection];
  return root;
}

- (void)showItems:(NSArray<DeskflowSwitcherItem *> *)items
        selection:(NSUInteger)selection
       thumbnails:(BOOL)thumbnails
{
  NSScreen *screen = [DeskflowSwitcherPanel screenWithMouse];
  const NSRect screenFrame = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1440, 900);
  self.backingScale = screen ? screen.backingScaleFactor : 2.0;
  NSPanel *panel = [self ensurePanel];
  NSView *content = [self buildContentForItems:items
                                     selection:selection
                                   screenFrame:screenFrame
                                    thumbnails:thumbnails];
  const NSSize size = content.frame.size;
  panel.contentView = content;
  const NSRect frame = NSMakeRect(
      floor(NSMidX(screenFrame) - size.width / 2), floor(NSMidY(screenFrame) - size.height / 2), size.width,
      size.height
  );
  [panel setFrame:frame display:YES];
  [panel orderFrontRegardless];
  [panel invalidateShadow];
}

- (void)selectIndex:(NSUInteger)index
{
  const NSUInteger count = self.itemViews.count;
  if (count == 0) {
    self.selection = 0;
    self.header.stringValue = @"";
    return;
  }
  self.selection = MIN(index, count - 1);
  for (NSUInteger i = 0; i < count; ++i) {
    self.itemViews[i].selected = (i == self.selection);
  }
  DeskflowSwitcherItemView *selected = self.itemViews[self.selection];
  DeskflowSwitcherItem *item = selected.item;
  NSString *title = displayTitle(item);
  if (item.title.length > 0 && item.appName.length > 0 && ![item.title isEqualToString:item.appName]) {
    title = [NSString stringWithFormat:@"%@ — %@", item.title, item.appName];
  }
  self.header.stringValue = title;
  [selected scrollRectToVisible:NSInsetRect(selected.bounds, 0, -kCellSpacing)];
}

- (void)refreshItemAtIndex:(NSUInteger)index
{
  if (index < self.itemViews.count) {
    DeskflowSwitcherItemView *view = self.itemViews[index];
    view.titleField.stringValue = displayTitle(view.item);
    view.needsDisplay = YES;
    if (index == self.selection) {
      [self selectIndex:index]; // the header shows the title too
    }
  }
}

- (void)hide
{
  if (self.panel != nil) {
    [self.panel orderOut:nil];
    // drop the item views (and their icons) until the next open
    self.panel.contentView = [[NSView alloc] initWithFrame:NSZeroRect];
  }
  self.itemViews = @[];
  self.header = nil;
  self.scrollView = nil;
}

@end
