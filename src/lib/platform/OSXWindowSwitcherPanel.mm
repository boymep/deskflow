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
constexpr CGFloat kTitleHeight = 16;
constexpr CGFloat kCellSpacing = 8;
constexpr CGFloat kPanelPadding = 18;
constexpr CGFloat kHeaderHeight = 20;
constexpr CGFloat kHeaderGap = 10;
constexpr CGFloat kCornerRadius = 18;
constexpr CGFloat kSelectionRadius = 10;

// Space tiles: preview on top, app icon overlaid at its bottom (fullscreen apps), title below
constexpr CGFloat kThumbPadding = 10;
constexpr CGFloat kStripBoxWidth = 220;
constexpr CGFloat kStripBoxHeight = 138;
constexpr CGFloat kThumbTitleGap = 6;
constexpr CGFloat kStripCellWidth = kStripBoxWidth + 2 * kThumbPadding;
constexpr CGFloat kStripCellHeight = kThumbPadding + kStripBoxHeight + kThumbTitleGap + kTitleHeight + kThumbPadding;
constexpr CGFloat kThumbRadius = 6;
constexpr CGFloat kOverlayIconSize = 32;
constexpr CGFloat kPlaceholderIconSize = 56;

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

//! One Space tile: thumbnail (wallpaper or fullscreen app), one-line title, selection highlight.
@interface DeskflowSwitcherItemView : NSView
@property(nonatomic, strong) DeskflowSwitcherItem *item;
@property(nonatomic, assign) BOOL selected;
@property(nonatomic, strong) NSTextField *titleField;
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
    _titleField.textColor = NSColor.labelColor;
    _titleField.frame = NSMakeRect(6, kThumbPadding - 2, frame.size.width - 12, kTitleHeight + 2);
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
  [self drawThumbnailTile];
}

- (NSRect)thumbnailBox
{
  const NSRect b = self.bounds;
  return NSMakeRect(
      floor((b.size.width - kStripBoxWidth) / 2), b.size.height - kThumbPadding - kStripBoxHeight, kStripBoxWidth,
      kStripBoxHeight
  );
}

//! Accent outline and a small badge marking the current Space on its thumbnail \p rect.
- (void)drawCurrentMarkIn:(NSRect)rect
{
  if (!self.item.currentSpace) {
    return;
  }
  NSBezierPath *outline = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(rect, 1, 1)
                                                          xRadius:kThumbRadius
                                                          yRadius:kThumbRadius];
  outline.lineWidth = 2;
  [[NSColor.controlAccentColor colorWithAlphaComponent:0.85] setStroke];
  [outline stroke];
  if (self.item.badge.length == 0) {
    return;
  }
  NSDictionary *attributes = @{
    NSFontAttributeName : [NSFont systemFontOfSize:10 weight:NSFontWeightMedium],
    NSForegroundColorAttributeName : NSColor.whiteColor
  };
  const NSSize text = [self.item.badge sizeWithAttributes:attributes];
  const CGFloat width = MIN(ceil(text.width) + 10, rect.size.width - 8);
  const NSRect pill = NSMakeRect(NSMinX(rect) + 5, NSMaxY(rect) - 5 - 15, width, 15);
  [[NSColor.controlAccentColor colorWithAlphaComponent:0.85] setFill];
  [[NSBezierPath bezierPathWithRoundedRect:pill xRadius:7.5 yRadius:7.5] fill];
  [self.item.badge drawInRect:NSMakeRect(NSMinX(pill) + 5, NSMinY(pill) + floor((15 - text.height) / 2), width - 10, text.height)
               withAttributes:attributes];
}

- (void)drawThumbnailTile
{
  DeskflowSwitcherItem *item = self.item;
  NSImage *icon = item.icon ?: [NSImage imageNamed:NSImageNameApplicationIcon];
  const NSRect box = [self thumbnailBox];

  const CGFloat placeholderIcon = kPlaceholderIconSize;
  const CGFloat overlayIcon = kOverlayIconSize;

  if (item.thumbnail == nil) {
    // placeholder until the preview arrives (or if it cannot be captured): the app icon
    NSBezierPath *frame = [NSBezierPath bezierPathWithRoundedRect:box xRadius:kThumbRadius yRadius:kThumbRadius];
    [[NSColor.labelColor colorWithAlphaComponent:item.desktop ? 0.12 : 0.05] setFill];
    [frame fill];
    if (!item.desktop) {
      const NSRect iconRect = NSMakeRect(
          floor(NSMidX(box) - placeholderIcon / 2), floor(NSMidY(box) - placeholderIcon / 2), placeholderIcon,
          placeholderIcon
      );
      [icon drawInRect:iconRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
    }
    [self drawCurrentMarkIn:box];
    return;
  }

  const NSRect imageRect = aspectFit(item.thumbnail.size, box);
  NSBezierPath *clip = [NSBezierPath bezierPathWithRoundedRect:imageRect xRadius:kThumbRadius yRadius:kThumbRadius];
  [NSGraphicsContext saveGraphicsState];
  [clip addClip];
  [item.thumbnail drawInRect:imageRect
                    fromRect:NSZeroRect
                   operation:NSCompositingOperationSourceOver
                    fraction:1.0];
  [NSGraphicsContext restoreGraphicsState];
  [[NSColor.separatorColor colorWithAlphaComponent:0.6] setStroke];
  NSBezierPath *border = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(imageRect, 0.5, 0.5)
                                                         xRadius:kThumbRadius
                                                         yRadius:kThumbRadius];
  border.lineWidth = 1;
  [border stroke];

  [self drawCurrentMarkIn:imageRect];
  if (item.desktop) {
    return; // a desktop shows its wallpaper only
  }

  // app icon overlaid at the bottom center of the preview, like Windows Alt+Tab
  const NSRect iconRect = NSMakeRect(
      floor(NSMidX(imageRect) - overlayIcon / 2), NSMinY(imageRect) + 4, overlayIcon, overlayIcon
  );
  [NSGraphicsContext saveGraphicsState];
  NSShadow *shadow = [NSShadow new];
  shadow.shadowColor = [NSColor colorWithWhite:0 alpha:0.45];
  shadow.shadowBlurRadius = 4;
  shadow.shadowOffset = NSMakeSize(0, -1);
  [shadow set];
  [icon drawInRect:iconRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
  [NSGraphicsContext restoreGraphicsState];
}

@end

@interface DeskflowSwitcherPanel ()
@property(nonatomic, strong) NSPanel *panel;
@property(nonatomic, strong) NSArray<DeskflowSwitcherItemView *> *itemViews;
@property(nonatomic, strong) NSTextField *header;
@property(nonatomic, assign, readwrite) NSUInteger selection;
@property(nonatomic, assign, readwrite) CGFloat backingScale;
@end

@implementation DeskflowSwitcherPanel

+ (NSSize)thumbnailBoxSize
{
  return NSMakeSize(kStripBoxWidth, kStripBoxHeight);
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

//! A clipping scroll view of \p document, \p visibleSize large; scrolls if the document is larger.
static NSScrollView *makeScroll(NSView *document, NSRect frame, BOOL horizontal)
{
  NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:frame];
  scroll.drawsBackground = NO;
  scroll.borderType = NSNoBorder;
  scroll.hasVerticalScroller = !horizontal && document.frame.size.height > frame.size.height;
  scroll.hasHorizontalScroller = horizontal && document.frame.size.width > frame.size.width;
  scroll.autohidesScrollers = YES;
  scroll.scrollerStyle = NSScrollerStyleOverlay;
  scroll.documentView = document;
  // start at the top left (documents are not flipped)
  [document scrollPoint:NSMakePoint(0, NSMaxY(document.bounds))];
  return scroll;
}

- (NSView *)buildContentForItems:(NSArray<DeskflowSwitcherItem *> *)items
                       selection:(NSUInteger)selection
                     screenFrame:(NSRect)screenFrame
{
  const NSUInteger count = items.count;
  const CGFloat maxContentWidth = screenFrame.size.width * 0.9 - 2 * kPanelPadding;

  // one row of Space tiles, centered; scrolls sideways if it does not fit
  const CGFloat stripWidth = count == 0 ? 0 : count * kStripCellWidth + (count - 1) * kCellSpacing;
  const CGFloat visibleStripWidth = MIN(stripWidth, maxContentWidth);
  const CGFloat contentWidth = MAX(visibleStripWidth, kStripCellWidth);
  const NSSize size = NSMakeSize(
      contentWidth + 2 * kPanelPadding, 2 * kPanelPadding + kStripCellHeight + kHeaderGap + kHeaderHeight
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

  NSMutableArray<DeskflowSwitcherItemView *> *views = [NSMutableArray arrayWithCapacity:count];
  NSView *strip = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, stripWidth, kStripCellHeight)];
  for (NSUInteger i = 0; i < count; ++i) {
    const CGFloat x = i * (kStripCellWidth + kCellSpacing);
    DeskflowSwitcherItemView *view =
        [[DeskflowSwitcherItemView alloc] initWithItem:items[i]
                                                  frame:NSMakeRect(x, 0, kStripCellWidth, kStripCellHeight)];
    [strip addSubview:view];
    [views addObject:view];
  }
  const NSRect stripFrame = NSMakeRect(
      kPanelPadding + floor((contentWidth - visibleStripWidth) / 2), size.height - kPanelPadding - kStripCellHeight,
      visibleStripWidth, kStripCellHeight
  );
  [root addSubview:makeScroll(strip, stripFrame, YES)];

  NSTextField *header = [NSTextField labelWithString:@""];
  header.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
  header.textColor = NSColor.labelColor;
  header.alignment = NSTextAlignmentCenter;
  header.lineBreakMode = NSLineBreakByTruncatingTail;
  header.maximumNumberOfLines = 1;
  header.frame = NSMakeRect(kPanelPadding, kPanelPadding, contentWidth, kHeaderHeight);
  [root addSubview:header];

  self.itemViews = views;
  self.header = header;
  [self selectIndex:selection];
  return root;
}

- (void)showItems:(NSArray<DeskflowSwitcherItem *> *)items selection:(NSUInteger)selection
{
  NSScreen *screen = [DeskflowSwitcherPanel screenWithMouse];
  const NSRect screenFrame = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1440, 900);
  self.backingScale = screen ? screen.backingScaleFactor : 2.0;
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
  self.header.stringValue = displayTitle(item);
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
}

@end
