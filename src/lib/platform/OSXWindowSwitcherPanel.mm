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

// Spaces strip: smaller thumbnail tiles in one row above the grid
constexpr CGFloat kStripBoxWidth = 160;
constexpr CGFloat kStripBoxHeight = 100;
constexpr CGFloat kStripCellWidth = kStripBoxWidth + 2 * kThumbPadding;
constexpr CGFloat kStripCellHeight = kThumbPadding + kStripBoxHeight + kThumbTitleGap + kTitleHeight + kThumbPadding;
constexpr CGFloat kStripIconSize = 24;
constexpr CGFloat kSectionGap = 8;

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
@property(nonatomic, assign) NSSize boxSize;  //!< thumbnail area of a thumbnail tile
@property(nonatomic, strong) NSTextField *titleField;
@end

@implementation DeskflowSwitcherItemView

- (instancetype)initWithItem:(DeskflowSwitcherItem *)item frame:(NSRect)frame thumbnails:(BOOL)thumbnails
{
  if ((self = [super initWithFrame:frame])) {
    _item = item;
    _thumbnails = thumbnails || item.space;
    _boxSize = item.space ? NSMakeSize(kStripBoxWidth, kStripBoxHeight) : NSMakeSize(kThumbBoxWidth, kThumbBoxHeight);
    thumbnails = _thumbnails;
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
  const NSSize box = self.boxSize;
  return NSMakeRect(
      floor((b.size.width - box.width) / 2), b.size.height - kThumbPadding - box.height, box.width, box.height
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
  const BOOL dimmed = item.minimized || item.appHidden;
  const NSRect box = [self thumbnailBox];

  const CGFloat placeholderIcon = item.space ? 48 : kIconSize;
  const CGFloat overlayIcon = item.space ? kStripIconSize : kOverlayIconSize;

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
      [icon drawInRect:iconRect
              fromRect:NSZeroRect
             operation:NSCompositingOperationSourceOver
              fraction:dimmed ? 0.45 : 1.0];
      if (item.minimized) {
        [self drawMinimizedBadgeAt:NSMakePoint(NSMaxX(iconRect) + 2, NSMinY(iconRect) - 2)];
      }
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
                    fraction:dimmed ? 0.55 : 1.0];
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
  if (item.minimized) {
    [self drawMinimizedBadgeAt:NSMakePoint(NSMaxX(imageRect) - 4, NSMinY(imageRect) + 4)];
  }
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
                      thumbnails:(BOOL)thumbnails
{
  // the strip: the leading Space items; the grid: the windows after them
  NSUInteger stripCount = 0;
  while (stripCount < items.count && items[stripCount].space) {
    ++stripCount;
  }
  const NSUInteger count = items.count - stripCount;
  const GridLayout layout = gridLayout(thumbnails);
  const CGFloat cellWidth = layout.cellWidth;
  const CGFloat cellHeight = layout.cellHeight;
  const CGFloat maxContentWidth = screenFrame.size.width * 0.9 - 2 * kPanelPadding;

  // grid: as many columns as fit on the screen (at most layout.maxColumns), rows wrap, extra rows scroll
  NSUInteger fitColumns = (NSUInteger)MAX(1.0, floor((maxContentWidth + kCellSpacing) / (cellWidth + kCellSpacing)));
  const NSUInteger columns = MAX((NSUInteger)1, MIN(MIN(count, layout.maxColumns), fitColumns));
  const NSUInteger rows = count == 0 ? 0 : (count + columns - 1) / columns;
  const CGFloat stripHeight = stripCount > 0 ? kStripCellHeight + kSectionGap : 0;
  const CGFloat usableHeight =
      screenFrame.size.height * 0.8 - 2 * kPanelPadding - kHeaderHeight - kHeaderGap - stripHeight;
  NSUInteger fitRows = (NSUInteger)MAX(1.0, floor((usableHeight + kCellSpacing) / (cellHeight + kCellSpacing)));
  const NSUInteger visibleRows = MIN(rows, MIN(kMaxVisibleRows, fitRows));
  const CGFloat gridWidth = count == 0 ? 0 : columns * cellWidth + (columns - 1) * kCellSpacing;
  const CGFloat gridHeight = rows == 0 ? 0 : rows * cellHeight + (rows - 1) * kCellSpacing;
  const CGFloat visibleGridHeight = visibleRows == 0 ? 0 : visibleRows * cellHeight + (visibleRows - 1) * kCellSpacing;

  // strip: one row, scrolls sideways if it does not fit
  const CGFloat stripWidth =
      stripCount == 0 ? 0 : stripCount * kStripCellWidth + (stripCount - 1) * kCellSpacing;
  const CGFloat visibleStripWidth = MIN(stripWidth, maxContentWidth);

  const CGFloat contentWidth = MAX(MAX(gridWidth, visibleStripWidth), kStripCellWidth);
  const CGFloat gridGap = count > 0 ? kHeaderGap : 0;
  const NSSize size = NSMakeSize(
      contentWidth + 2 * kPanelPadding, 2 * kPanelPadding + stripHeight + kHeaderHeight + gridGap + visibleGridHeight
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

  NSMutableArray<DeskflowSwitcherItemView *> *views = [NSMutableArray arrayWithCapacity:items.count];
  CGFloat top = size.height - kPanelPadding;

  if (stripCount > 0) {
    NSView *strip = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, stripWidth, kStripCellHeight)];
    for (NSUInteger i = 0; i < stripCount; ++i) {
      const CGFloat x = i * (kStripCellWidth + kCellSpacing);
      DeskflowSwitcherItemView *view =
          [[DeskflowSwitcherItemView alloc] initWithItem:items[i]
                                                    frame:NSMakeRect(x, 0, kStripCellWidth, kStripCellHeight)
                                               thumbnails:YES];
      [strip addSubview:view];
      [views addObject:view];
    }
    const NSRect frame = NSMakeRect(
        kPanelPadding + floor((contentWidth - visibleStripWidth) / 2), top - kStripCellHeight, visibleStripWidth,
        kStripCellHeight
    );
    [root addSubview:makeScroll(strip, frame, YES)];
    top -= stripHeight;
  }

  NSTextField *header = [NSTextField labelWithString:@""];
  header.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
  header.textColor = NSColor.labelColor;
  header.alignment = NSTextAlignmentCenter;
  header.lineBreakMode = NSLineBreakByTruncatingTail;
  header.maximumNumberOfLines = 1;
  header.frame = NSMakeRect(kPanelPadding, top - kHeaderHeight, contentWidth, kHeaderHeight);
  [root addSubview:header];
  top -= kHeaderHeight + gridGap;

  if (count > 0) {
    NSView *grid = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, gridWidth, gridHeight)];
    for (NSUInteger i = 0; i < count; ++i) {
      const NSUInteger row = i / columns;
      const NSUInteger column = i % columns;
      const CGFloat x = column * (cellWidth + kCellSpacing);
      const CGFloat y = gridHeight - (row + 1) * cellHeight - row * kCellSpacing;
      DeskflowSwitcherItemView *view =
          [[DeskflowSwitcherItemView alloc] initWithItem:items[stripCount + i]
                                                    frame:NSMakeRect(x, y, cellWidth, cellHeight)
                                               thumbnails:thumbnails];
      [grid addSubview:view];
      [views addObject:view];
    }
    const NSRect frame = NSMakeRect(
        kPanelPadding + floor((contentWidth - gridWidth) / 2), top - visibleGridHeight, gridWidth, visibleGridHeight
    );
    [root addSubview:makeScroll(grid, frame, NO)];
  }

  self.itemViews = views;
  self.header = header;
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
}

@end
