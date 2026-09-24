#import "MacStatusItemManager.h"
#import <Cocoa/Cocoa.h>
#import <UserNotifications/UserNotifications.h>
#include <iostream>

namespace efd {

class MacStatusItemManager::Impl {
public:
    NSStatusItem* statusItem = nil;
    ActionCallback actionCallback;
    BOOL initialized = NO;

    ~Impl() {
        cleanup();
    }

    void cleanup() {
        if (statusItem) {
            [[NSStatusBar systemStatusBar] removeStatusItem:statusItem];
            statusItem = nil;
        }
        initialized = NO;
    }
};

} // namespace efd

@interface EFDStatusMenuTarget : NSObject
@property (nonatomic, assign) efd::MacStatusItemManager::ActionCallback* callbackPtr;
- (void)onShowMainWindow:(id)sender;
- (void)onToggleFloating:(id)sender;
- (void)onRecalibrate:(id)sender;
- (void)onEndStudyGate:(id)sender;
- (void)onExitApp:(id)sender;
@end

@implementation EFDStatusMenuTarget
- (void)onShowMainWindow:(id)sender {
    if (self.callbackPtr && *(self.callbackPtr)) {
        (*(self.callbackPtr))(efd::MacMenuAction::ShowMainWindow);
    }
}
- (void)onToggleFloating:(id)sender {
    if (self.callbackPtr && *(self.callbackPtr)) {
        (*(self.callbackPtr))(efd::MacMenuAction::ToggleFloatingIndicator);
    }
}
- (void)onRecalibrate:(id)sender {
    if (self.callbackPtr && *(self.callbackPtr)) {
        (*(self.callbackPtr))(efd::MacMenuAction::Recalibrate);
    }
}
- (void)onEndStudyGate:(id)sender {
    if (self.callbackPtr && *(self.callbackPtr)) {
        (*(self.callbackPtr))(efd::MacMenuAction::EndStudyGate);
    }
}
- (void)onExitApp:(id)sender {
    if (self.callbackPtr && *(self.callbackPtr)) {
        (*(self.callbackPtr))(efd::MacMenuAction::ExitApp);
    } else {
        [NSApp terminate:nil];
    }
}
@end

static EFDStatusMenuTarget* s_menuTarget = nil;

namespace efd {

static NSImage* createStatusDotIcon(NSColor* dotColor) {
    NSSize size = NSMakeSize(18, 18);
    NSImage* image = [[NSImage alloc] initWithSize:size];
    [image lockFocus];
    
    // 繪製背景底圈
    NSBezierPath* outerCircle = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(2, 2, 14, 14)];
    [[NSColor colorWithCalibratedWhite:0.2 alpha:0.3] setStroke];
    outerCircle.lineWidth = 1.0;
    [outerCircle stroke];

    // 繪製狀態圓點
    NSBezierPath* innerCircle = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(4, 4, 10, 10)];
    [dotColor setFill];
    [innerCircle fill];

    [image unlockFocus];
    return image;
}

MacStatusItemManager::MacStatusItemManager()
    : m_impl(std::make_unique<Impl>()) {
    if (!s_menuTarget) {
        s_menuTarget = [[EFDStatusMenuTarget alloc] init];
    }
    s_menuTarget.callbackPtr = &m_impl->actionCallback;
}

MacStatusItemManager::~MacStatusItemManager() = default;

bool MacStatusItemManager::initialize(const std::string& tooltip) {
    if (m_impl->initialized) return true;

    m_impl->statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    if (!m_impl->statusItem) return false;

    NSStatusBarButton* button = m_impl->statusItem.button;
    if (button) {
        button.image = createStatusDotIcon([NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0]);
        button.toolTip = [NSString stringWithUTF8String:tooltip.c_str()];
    }

    // 建立選單
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"EFD Menu"];
    
    NSMenuItem* itemTitle = [[NSMenuItem alloc] initWithTitle:@"EFD 眼睛疲勞監測系統" action:nil keyEquivalent:@""];
    [itemTitle setEnabled:NO];
    [menu addItem:itemTitle];
    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* itemShow = [[NSMenuItem alloc] initWithTitle:@"顯示主視窗" action:@selector(onShowMainWindow:) keyEquivalent:@"o"];
    itemShow.target = s_menuTarget;
    [menu addItem:itemShow];

    NSMenuItem* itemHUD = [[NSMenuItem alloc] initWithTitle:@"切換懸浮監控視窗" action:@selector(onToggleFloating:) keyEquivalent:@"h"];
    itemHUD.target = s_menuTarget;
    [menu addItem:itemHUD];

    NSMenuItem* itemRecalib = [[NSMenuItem alloc] initWithTitle:@"重新校準眼動基準" action:@selector(onRecalibrate:) keyEquivalent:@"r"];
    itemRecalib.target = s_menuTarget;
    [menu addItem:itemRecalib];

    NSMenuItem* itemGate = [[NSMenuItem alloc] initWithTitle:@"後測介面與問卷" action:@selector(onEndStudyGate:) keyEquivalent:@"g"];
    itemGate.target = s_menuTarget;
    [menu addItem:itemGate];

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* itemExit = [[NSMenuItem alloc] initWithTitle:@"結束 EFD" action:@selector(onExitApp:) keyEquivalent:@"q"];
    itemExit.target = s_menuTarget;
    [menu addItem:itemExit];

    m_impl->statusItem.menu = menu;
    m_impl->initialized = YES;
    return true;
}

void MacStatusItemManager::updateStatus(FatigueLevel level, float fatigueScore, const std::string& statusMsg) {
    if (!m_impl->statusItem || !m_impl->statusItem.button) return;

    NSColor* dotColor = [NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0]; // 綠色正常
    if (level == FatigueLevel::Attention) {
        dotColor = [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]; // 金黃注意
    } else if (level == FatigueLevel::SevereWarning || fatigueScore >= 70.0f) {
        dotColor = [NSColor colorWithCalibratedRed:235/255.0 green:87/255.0 blue:87/255.0 alpha:1.0]; // 紅色嚴重警告
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        self->m_impl->statusItem.button.image = createStatusDotIcon(dotColor);
        NSString* tip = [NSString stringWithFormat:@"EFD 疲勞分數: %.1f | %@", fatigueScore, [NSString stringWithUTF8String:statusMsg.c_str()]];
        self->m_impl->statusItem.button.toolTip = tip;
    });
}

void MacStatusItemManager::showNotification(const std::string& title, const std::string& message, FatigueLevel level) {
    (void)level;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (@available(macOS 10.14, *)) {
            UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
            [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                                  completionHandler:^(BOOL granted, NSError * _Nullable error) {
                if (granted) {
                    UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
                    content.title = [NSString stringWithUTF8String:title.c_str()];
                    content.body = [NSString stringWithUTF8String:message.c_str()];
                    content.sound = [UNNotificationSound defaultSound];

                    UNNotificationRequest* request = [UNNotificationRequest requestWithIdentifier:@"EFD_Fatigue_Alert"
                                                                                          content:content
                                                                                          trigger:nil];
                    [center addNotificationRequest:request withCompletionHandler:nil];
                }
            }];
        } else {
            NSUserNotification* notification = [[NSUserNotification alloc] init];
            notification.title = [NSString stringWithUTF8String:title.c_str()];
            notification.informativeText = [NSString stringWithUTF8String:message.c_str()];
            notification.soundName = NSUserNotificationDefaultSoundName;
            [[NSUserNotificationCenter defaultUserNotificationCenter] deliverNotification:notification];
        }
    });
}

void MacStatusItemManager::setActionCallback(ActionCallback callback) {
    m_impl->actionCallback = callback;
    if (s_menuTarget) {
        s_menuTarget.callbackPtr = &m_impl->actionCallback;
    }
}

void MacStatusItemManager::removeStatusItem() {
    m_impl->cleanup();
}

bool MacStatusItemManager::isInitialized() const {
    return m_impl->initialized;
}

} // namespace efd

