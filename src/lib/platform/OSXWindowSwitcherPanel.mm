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
constexpr CGFloat kCellWidth = 128;
constexpr CGFloat kCellPadding = 12;
constexpr CGFloat kTitleHeight = 16;
constexpr CGFloat kCellHeight = kCellPadding + kIconSize + 8 + kTitleHeight + kCellPadding;
constexpr CGFloat kCellSpacing = 6;
constexpr CGFloat kPanelPadding = 16;
constexpr CGFloat kHeaderHeight = 20;
constexpr CGFloat kHeaderGap = 10;
constexpr CGFloat kCornerRadius = 18;
constexpr CGFloat kSelectionRadius = 10;
constexpr NSUInteger kMaxColumns = 8;
constexpr NSUInteger kMaxVisibleRows = 3;

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

//! One grid cell: icon, one-line title, selection highlight, hidden thumbnail slot.
@interface DeskflowSwitcherItemView : NSView
@property(nonatomic, strong) DeskflowSwitcherItem *item;
@property(nonatomic, assign) BOOL selected;
@property(nonatomic, strong) NSTextField *titleField;
@property(nonatomic, strong) NSImageView *thumbnailView; //!< reserved for window previews, hidden for now
@end

@implementation DeskflowSwitcherItemView

- (instancetype)initWithItem:(DeskflowSwitcherItem *)item frame:(NSRect)frame
{
  if ((self = [super initWithFrame:frame])) {
    _item = item;

    _titleField = [NSTextField labelWithString:displayTitle(item)];
    _titleField.font = [NSFont systemFontOfSize:12];
    _titleField.alignment = NSTextAlignmentCenter;
    _titleField.lineBreakMode = NSLineBreakByTruncatingTail;
    _titleField.maximumNumberOfLines = 1;
    _titleField.cell.truncatesLastVisibleLine = YES;
    _titleField.textColor = (item.minimized || item.appHidden) ? NSColor.secondaryLabelColor : NSColor.labelColor;
    _titleField.frame = NSMakeRect(6, kCellPadding - 2, frame.size.width - 12, kTitleHeight + 2);
    [self addSubview:_titleField];

    _thumbnailView = [[NSImageView alloc] initWithFrame:NSZeroRect];
    _thumbnailView.imageScaling = NSImageScaleProportionallyDown;
    _thumbnailView.hidden = YES;
    [self addSubview:_thumbnailView];
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

  const NSRect iconRect = [self iconRect];
  NSImage *icon = self.item.icon ?: [NSImage imageNamed:NSImageNameApplicationIcon];
  const CGFloat alpha = (self.item.minimized || self.item.appHidden) ? 0.45 : 1.0;
  [icon drawInRect:iconRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:alpha];

  if (self.item.minimized) {
    // badge: a small dash in a pill at the icon's bottom right
    const NSRect badge = NSMakeRect(NSMaxX(iconRect) - 22, NSMinY(iconRect) - 2, 24, 16);
    NSBezierPath *pill = [NSBezierPath bezierPathWithRoundedRect:badge xRadius:8 yRadius:8];
    [NSColor.controlAccentColor setFill];
    [pill fill];
    [[NSColor colorWithWhite:1 alpha:0.95] setFill];
    NSRectFill(NSMakeRect(NSMidX(badge) - 5, NSMidY(badge) - 1, 10, 2));
  }
}

@end

@interface DeskflowSwitcherPanel ()
@property(nonatomic, strong) NSPanel *panel;
@property(nonatomic, strong) NSArray<DeskflowSwitcherItemView *> *itemViews;
@property(nonatomic, strong) NSTextField *header;
@property(nonatomic, strong) NSScrollView *scrollView;
@property(nonatomic, assign, readwrite) NSUInteger selection;
@end

@implementation DeskflowSwitcherPanel

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
{
  const NSUInteger count = items.count;

  // as many columns as fit on the screen (at most kMaxColumns), rows wrap, extra rows scroll
  const CGFloat usableWidth = screenFrame.size.width * 0.9 - 2 * kPanelPadding;
  NSUInteger fitColumns = (NSUInteger)MAX(1.0, floor((usableWidth + kCellSpacing) / (kCellWidth + kCellSpacing)));
  const NSUInteger columns = MAX((NSUInteger)1, MIN(MIN(count, kMaxColumns), fitColumns));
  const NSUInteger rows = count == 0 ? 1 : (count + columns - 1) / columns;
  const CGFloat usableHeight = screenFrame.size.height * 0.8 - 2 * kPanelPadding - kHeaderHeight - kHeaderGap;
  NSUInteger fitRows = (NSUInteger)MAX(1.0, floor((usableHeight + kCellSpacing) / (kCellHeight + kCellSpacing)));
  const NSUInteger visibleRows = MIN(rows, MIN(kMaxVisibleRows, fitRows));

  const CGFloat gridWidth = columns * kCellWidth + (columns - 1) * kCellSpacing;
  const CGFloat gridHeight = rows * kCellHeight + (rows - 1) * kCellSpacing;
  const CGFloat visibleGridHeight = visibleRows * kCellHeight + (visibleRows - 1) * kCellSpacing;
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
    const CGFloat x = column * (kCellWidth + kCellSpacing);
    const CGFloat y = gridHeight - (row + 1) * kCellHeight - row * kCellSpacing;
    DeskflowSwitcherItemView *view =
        [[DeskflowSwitcherItemView alloc] initWithItem:items[i] frame:NSMakeRect(x, y, kCellWidth, kCellHeight)];
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

- (void)showItems:(NSArray<DeskflowSwitcherItem *> *)items selection:(NSUInteger)selection
{
  NSScreen *screen = [DeskflowSwitcherPanel screenWithMouse];
  const NSRect screenFrame = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1440, 900);
  NSPanel *panel = [self ensurePanel];
  NSView *content = [self buildContentForItems:items selection:selection screenFrame:screenFrame];
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
