#import "ViewController.h"
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include "engine/AsyncPipelineEngine.hpp"

@interface ViewController ()
@property (nonatomic, assign) std::unique_ptr<efd::AsyncPipelineEngine>* engine;
@property (nonatomic, strong) UILabel *titleLabel;
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UILabel *scoreLabel;
@property (nonatomic, strong) UIButton *startBtn;
@property (nonatomic, strong) UIButton *alertBtn;
@property (nonatomic, strong) UIView *badgeView;
@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.145 green:0.161 blue:0.110 alpha:1.0]; // #25291C

    // 初始化 C++20 EFD 引擎
    self.engine = new std::unique_ptr<efd::AsyncPipelineEngine>(std::make_unique<efd::AsyncPipelineEngine>(efd::PlatformType::iOS));
    (*self.engine)->start();

    // 建立 UI 視圖
    [self setupUI];
}

- (void)setupUI {
    CGFloat w = self.view.bounds.size.width;
    CGFloat h = self.view.bounds.size.height;

    // 1. 歡迎徽章
    self.badgeView = [[UIView alloc] initWithFrame:CGRectMake((w - 140) / 2, 100, 140, 140)];
    self.badgeView.backgroundColor = [UIColor whiteColor];
    self.badgeView.layer.cornerRadius = 70;
    self.badgeView.layer.borderColor = [[UIColor colorWithRed:0.969 green:0.890 blue:0.686 alpha:1.0] CGColor];
    self.badgeView.layer.borderWidth = 3.0;
    [self.view addSubview:self.badgeView];

    // 2. 標題
    self.titleLabel = [[UILabel alloc] initWithFrame:CGRectMake(20, 260, w - 40, 40)];
    self.titleLabel.text = @"感謝協助測試EFD";
    self.titleLabel.textColor = [UIColor whiteColor];
    self.titleLabel.font = [UIFont boldSystemFontOfSize:24];
    self.titleLabel.textAlignment = NSTextAlignmentCenter;
    [self.view addSubview:self.titleLabel];

    // 3. 生理狀態與疲勞評分
    self.statusLabel = [[UILabel alloc] initWithFrame:CGRectMake(20, 320, w - 40, 30)];
    self.statusLabel.text = @"生理狀態：清醒專注 (Normal)";
    self.statusLabel.textColor = [UIColor colorWithRed:0.118 green:0.694 blue:0.541 alpha:1.0]; // 薄荷綠
    self.statusLabel.font = [UIFont systemFontOfSize:16 weight:UIFontWeightMedium];
    self.statusLabel.textAlignment = NSTextAlignmentCenter;
    [self.view addSubview:self.statusLabel];

    self.scoreLabel = [[UILabel alloc] initWithFrame:CGRectMake(20, 360, w - 40, 40)];
    self.scoreLabel.text = @"疲勞評分：12.5 / 100";
    self.scoreLabel.textColor = [UIColor whiteColor];
    self.scoreLabel.font = [UIFont boldSystemFontOfSize:28];
    self.scoreLabel.textAlignment = NSTextAlignmentCenter;
    [self.view addSubview:self.scoreLabel];

    // 4. 按鈕群 (粗體字體、20px 圓角邊框, 純文字)
    self.startBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    self.startBtn.frame = CGRectMake(40, h - 180, w - 80, 50);
    [self.startBtn setTitle:@"開始眼動特徵校準" forState:UIControlStateNormal];
    [self.startBtn setTitleColor:[UIColor colorWithRed:0.145 green:0.161 blue:0.110 alpha:1.0] forState:UIControlStateNormal];
    self.startBtn.backgroundColor = [UIColor colorWithRed:0.969 green:0.890 blue:0.686 alpha:1.0];
    self.startBtn.layer.cornerRadius = 20;
    self.startBtn.titleLabel.font = [UIFont boldSystemFontOfSize:16];
    [self.startBtn addTarget:self action:@selector(onStartCalib) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:self.startBtn];

    self.alertBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    self.alertBtn.frame = CGRectMake(40, h - 115, w - 80, 50);
    [self.alertBtn setTitle:@"警告通知發送" forState:UIControlStateNormal];
    [self.alertBtn setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    self.alertBtn.backgroundColor = [UIColor colorWithRed:0.922 green:0.341 blue:0.341 alpha:1.0];
    self.alertBtn.layer.cornerRadius = 20;
    self.alertBtn.titleLabel.font = [UIFont boldSystemFontOfSize:15];
    [self.alertBtn addTarget:self action:@selector(onTriggerAlert) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:self.alertBtn];
}

- (void)onStartCalib {
    if (self.engine && *self.engine) {
        (*self.engine)->calibrate(3.0f);
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"校準完成"
                                                                       message:@"個人 EAR 判定閾值基準已自動校準！"
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"確定" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];
    }
}

- (void)onTriggerAlert {
    // 觸發 iOS 觸覺震動 (Taptic Engine)
    UINotificationFeedbackGenerator *feedback = [[UINotificationFeedbackGenerator alloc] init];
    [feedback notificationOccurred:UINotificationFeedbackTypeWarning];

    // 彈出紅色警報
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"🚨 疲勞警報提醒"
                                                                   message:@"【你的眼睛處於疲勞狀態，請適當休息】\n\n系統偵測到眼動疲勞指數已達紅色危險狀態，請閉眼放鬆 5 分鐘。"
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"我知道了，立即休息" style:UIAlertActionStyleDestructive handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

- (void)dealloc {
    if (self.engine) {
        if (*self.engine) (*self.engine)->stop();
        delete self.engine;
    }
}

@end

