#import <UIKit/UIKit.h>

@interface ViewController : UIViewController

// 背景同步 (BGAppRefreshTask) 入口：觸發補傳/夜間同步並回報是否成功
- (void)runBackgroundSyncWithCompletion:(void (^)(BOOL success))completion;

@end
