#import "AppDelegate.h"
#import "ViewController.h"
#include "platform/PlatformLifecycleAdapter.hpp"

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    ViewController *viewController = [[ViewController alloc] init];
    self.window.rootViewController = viewController;
    [self.window makeKeyAndVisible];
    return YES;
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
    // 進入背景暫停管線
}

- (void)applicationWillEnterForeground:(UIApplication *)application {
    // 喚醒熱重啟
}

@end
