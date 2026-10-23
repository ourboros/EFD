#import "AppDelegate.h"
#import "ViewController.h"
#import <BackgroundTasks/BackgroundTasks.h>
#include "platform/PlatformLifecycleAdapter.hpp"

// 需與 Info.plist 的 BGTaskSchedulerPermittedIdentifiers 一致
static NSString *const kEFDNightlySyncTaskId = @"com.efd.EyeFatigueDetection.nightlysync";

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // BGTaskScheduler 必須在啟動完成前註冊
    [[BGTaskScheduler sharedScheduler] registerForTaskWithIdentifier:kEFDNightlySyncTaskId
                                                          usingQueue:nil
                                                       launchHandler:^(__kindof BGTask *task) {
        [self handleNightlySyncTask:(BGAppRefreshTask *)task];
    }];

    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    ViewController *viewController = [[ViewController alloc] init];
    self.window.rootViewController = viewController;
    [self.window makeKeyAndVisible];
    return YES;
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
    // 管線暫停由 ViewController 處理；此處排定下一次背景同步
    [self scheduleNightlySync];
}

- (void)applicationWillEnterForeground:(UIApplication *)application {
    // 熱重啟與補傳由 ViewController 處理
}

// 排定於下一個 21:05 之後執行 (實際執行時間由 iOS 依使用習慣與電量決定，不保證準時)
- (void)scheduleNightlySync {
    NSCalendar *calendar = [NSCalendar currentCalendar];
    NSDate *now = [NSDate date];
    NSDate *target = [calendar dateBySettingHour:21 minute:5 second:0 ofDate:now options:0];
    if (!target || [target compare:now] != NSOrderedDescending) {
        target = [calendar dateByAddingUnit:NSCalendarUnitDay value:1 toDate:target ?: now options:0];
    }

    BGAppRefreshTaskRequest *request = [[BGAppRefreshTaskRequest alloc] initWithIdentifier:kEFDNightlySyncTaskId];
    request.earliestBeginDate = target;
    NSError *error = nil;
    if (![[BGTaskScheduler sharedScheduler] submitTaskRequest:request error:&error]) {
        NSLog(@"[EFD iOS] 背景同步排程失敗: %@", error.localizedDescription);
    }
}

- (void)handleNightlySyncTask:(BGAppRefreshTask *)task {
    [self scheduleNightlySync]; // 先排好下一次

    __block BOOL finished = NO;
    task.expirationHandler = ^{
        if (!finished) {
            finished = YES;
            [task setTaskCompletedWithSuccess:NO];
        }
    };

    dispatch_async(dispatch_get_main_queue(), ^{
        ViewController *vc = (ViewController *)self.window.rootViewController;
        if (![vc isKindOfClass:[ViewController class]]) {
            finished = YES;
            [task setTaskCompletedWithSuccess:NO];
            return;
        }
        [vc loadViewIfNeeded]; // 背景啟動時確保引擎已建立
        [vc runBackgroundSyncWithCompletion:^(BOOL success) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!finished) {
                    finished = YES;
                    [task setTaskCompletedWithSuccess:success];
                }
            });
        }];
    });
}

@end
