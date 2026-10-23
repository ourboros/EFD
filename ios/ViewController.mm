#import "ViewController.h"
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <UserNotifications/UserNotifications.h>
#include <memory>
#include <algorithm>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include "engine/AsyncPipelineEngine.hpp"

// -----------------------------------------------------------------------------
// EFD iOS 設計系統規範色碼 (5 大核心配色)
// -----------------------------------------------------------------------------
#define COLOR_PRIMARY_MINT   [UIColor colorWithRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0]  // #1EB18A
#define COLOR_ACCENT_BLUE    [UIColor colorWithRed:150/255.0 green:197/255.0 blue:247/255.0 alpha:1.0] // #96C5F7
#define COLOR_CARD_WHITE     [UIColor whiteColor]                                                        // #FFFFFF
#define COLOR_WARM_AMBER     [UIColor colorWithRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0] // #F7E3AF
#define COLOR_TEXT_DARK      [UIColor colorWithRed:37/255.0 green:41/255.0 blue:28/255.0 alpha:1.0]   // #25291C
#define COLOR_ALERT_CORAL    [UIColor colorWithRed:235/255.0 green:87/255.0 blue:87/255.0 alpha:1.0]  // #EB5757

typedef NS_ENUM(NSInteger, EFDAppStage) {
    EFDAppStageWelcome = 0,
    EFDAppStageInstruction,
    EFDAppStageCalibration,
    EFDAppStageDashboard,
    EFDAppStageSettings,
    EFDAppStageStudyGate
};

static UIImage* EFDGetAsset10Logo() {
    NSString *path = [[NSBundle mainBundle] pathForResource:@"logo" ofType:@"png"];
    if (!path) {
        path = [[NSBundle mainBundle] pathForResource:@"資產 10" ofType:@"png"];
    }
    if (path) {
        UIImage *img = [UIImage imageWithContentsOfFile:path];
        if (img) return img;
    }
    UIImage *namedImg = [UIImage imageNamed:@"logo"] ?: [UIImage imageNamed:@"資產 10"];
    if (namedImg) return namedImg;
    return [UIImage systemImageNamed:@"eye.circle.fill"];
}

@interface ViewController () <UNUserNotificationCenterDelegate> {
    efd::AsyncPipelineEngine* _engine;
    efd::EngineTelemetry _latestTelemetry;
    BOOL _hasTelemetry;
}

@property (nonatomic, assign) EFDAppStage currentStage;
@property (nonatomic, strong) UIView *contentContainer;

// 即時遙測儀表元件
@property (nonatomic, strong) UILabel *earValueLabel;
@property (nonatomic, strong) UILabel *perclosValueLabel;
@property (nonatomic, strong) UILabel *mseValueLabel;
@property (nonatomic, strong) UILabel *fatigueScoreLabel;
@property (nonatomic, strong) UIView *cameraAspectBox;
@property (nonatomic, strong) AVCaptureVideoPreviewLayer *cameraPreviewLayer;
@property (nonatomic, strong) UILabel *cameraStatusLabel;

// 校準階段元件
@property (nonatomic, strong) UIProgressView *calibProgressView;
@property (nonatomic, strong) UILabel *calibHintLabel;
@property (nonatomic, strong) NSTimer *calibTimer;
@property (nonatomic, assign) CGFloat calibElapsedSec;

// 設定項目狀態
@property (nonatomic, assign) BOOL highSensitivityEnabled;
@property (nonatomic, assign) BOOL soundAlertEnabled;
@property (nonatomic, assign) BOOL autoStartEnabled;

// 背景定時檢查 (科研同步 & 門禁檢查)
@property (nonatomic, strong) NSTimer *backgroundCheckTimer;

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = COLOR_PRIMARY_MINT;

    self.highSensitivityEnabled = NO;
    self.soundAlertEnabled = YES;
    self.autoStartEnabled = NO;
    self.currentStage = EFDAppStageWelcome;

    // 建立全螢幕自適應容器
    self.contentContainer = [[UIView alloc] initWithFrame:self.view.bounds];
    self.contentContainer.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.contentContainer.backgroundColor = [UIColor clearColor];
    [self.view addSubview:self.contentContainer];

    // 初始化 C++20 跨平台管線引擎 (iOS 平台原生)
    _engine = new efd::AsyncPipelineEngine(efd::PlatformType::iOS);

    // 檢查是否已達第 14 天後測門禁
    if (_engine->getStudyTracker().getStatus() == efd::StudyStatus::LockedForPostTest) {
        self.currentStage = EFDAppStageStudyGate;
    }

    // 遙測回呼
    _engine->setTelemetryCallback([self](const efd::EngineTelemetry& t) {
        self->_latestTelemetry = t;
        self->_hasTelemetry = YES;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateTelemetryUI:t];
        });
    });

    // 警報回呼 (疲勞超標警報與震動)
    _engine->setAlertCallback([self](efd::FatigueLevel level, float score, const std::string& msg) {
        (void)msg;
        if (level == efd::FatigueLevel::SevereWarning || score >= 70.0f) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self triggerFatigueAlertNotification:score];
            });
        }
    });

    // 啟動管線
    _engine->start();

    // 背景定時定點檢查 (每 30 秒執行一次夜間 21:00 科研同步與門禁檢查)
    self.backgroundCheckTimer = [NSTimer scheduledTimerWithTimeInterval:30.0
                                                                 target:self
                                                               selector:@selector(onBackgroundCheckTick)
                                                               userInfo:nil
                                                                repeats:YES];

    // 偏好 A：背景提醒與本地通知授權申請
    [self setupNotificationCenter];

    // 監聽 App 生命週期事件 (切入背景、回至前景)
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(onAppDidEnterBackground)
                                                 name:UIApplicationDidEnterBackgroundNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(onAppWillEnterForeground)
                                                 name:UIApplicationWillEnterForegroundNotification
                                               object:nil];

    // 初始渲染
    [self renderCurrentStage];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    self.contentContainer.frame = self.view.bounds;
    if (self.cameraPreviewLayer && self.cameraAspectBox) {
        self.cameraPreviewLayer.frame = self.cameraAspectBox.bounds;
    }
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self.backgroundCheckTimer invalidate];
    [self.calibTimer invalidate];
    [UIApplication sharedApplication].idleTimerDisabled = NO;
    if (_engine) {
        _engine->stop();
        delete _engine;
        _engine = nullptr;
    }
}

// -----------------------------------------------------------------------------
// 背景偏好 A：本地通知與鬧鐘式定時排程守護
// -----------------------------------------------------------------------------
- (void)setupNotificationCenter {
    UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
    center.delegate = self;
    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound | UNAuthorizationOptionBadge)
                          completionHandler:^(BOOL granted, NSError * _Nullable error) {
        if (granted) {
            NSLog(@"[EFD iOS] 使用者已授權本地通知權限");
        }
    }];
}

- (void)onAppDidEnterBackground {
    // 偏好 A：當使用者切到其他 App 或鎖定螢幕時，相機自動安全掛起
    // 向系統排定 20 分鐘與 30 分鐘鬧鐘式本地推播提醒
    [self scheduleFatigueReminderNotificationInMinutes:20];
}

- (void)onAppWillEnterForeground {
    // 使用者返回 EFD，清除排程中尚未觸發的疲勞提醒
    [[UNUserNotificationCenter currentNotificationCenter] removePendingNotificationRequestsWithIdentifiers:@[@"EFD_FATIGUE_ALARM_20M"]];
    if (self.currentStage == EFDAppStageDashboard) {
        [UIApplication sharedApplication].idleTimerDisabled = YES;
        [self attachCameraPreviewIfNeeded];
    }
}

- (void)scheduleFatigueReminderNotificationInMinutes:(int)minutes {
    UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
    content.title = @"🚨 EFD 眼睛健康鬧鐘提醒";
    content.body = [NSString stringWithFormat:@"您已持續使用手機達 %d 分鐘！EFD 建議您閉眼休息 5 分鐘，放鬆眼部睫狀肌。", minutes];
    content.sound = [UNNotificationSound defaultSound];

    UNTimeIntervalNotificationTrigger *trigger = [UNTimeIntervalNotificationTrigger triggerWithTimeInterval:(minutes * 60) repeats:NO];
    UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:@"EFD_FATIGUE_ALARM_20M" content:content trigger:trigger];

    [[UNUserNotificationCenter currentNotificationCenter] addNotificationRequest:request withCompletionHandler:nil];
}

// 前景時通知依然呈現橫幅與聲音
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionSound);
}

// -----------------------------------------------------------------------------
// 背景定時檢查 (21:00 夜間科研同步 & 第 14 天後測門禁)
// -----------------------------------------------------------------------------
- (void)onBackgroundCheckTick {
    if (!_engine) return;

    _engine->checkAndTriggerNightlySync();

    if (_engine->getStudyTracker().getStatus() == efd::StudyStatus::LockedForPostTest &&
        self.currentStage != EFDAppStageStudyGate) {
        self.currentStage = EFDAppStageStudyGate;
        [self renderCurrentStage];
    }
}

// -----------------------------------------------------------------------------
// 畫面切換主邏輯
// -----------------------------------------------------------------------------
- (void)switchToStage:(EFDAppStage)stage {
    self.currentStage = stage;
    [self renderCurrentStage];
}

- (void)renderCurrentStage {
    for (UIView *subview in self.contentContainer.subviews) {
        [subview removeFromSuperview];
    }
    self.cameraPreviewLayer = nil;

    // 偏好 A：僅在 Dashboard 監控階段保持螢幕常亮，其餘畫面恢復系統自動休眠
    if (self.currentStage == EFDAppStageDashboard) {
        [UIApplication sharedApplication].idleTimerDisabled = YES;
    } else {
        [UIApplication sharedApplication].idleTimerDisabled = NO;
    }

    switch (self.currentStage) {
        case EFDAppStageWelcome:
            [self renderWelcomeScreen];
            break;
        case EFDAppStageInstruction:
            [self renderInstructionScreen];
            break;
        case EFDAppStageCalibration:
            [self renderCalibrationScreen];
            break;
        case EFDAppStageDashboard:
            [self renderDashboardScreen];
            break;
        case EFDAppStageSettings:
            [self renderSettingsScreen];
            break;
        case EFDAppStageStudyGate:
            [self renderStudyGateScreen];
            break;
    }
}

// -----------------------------------------------------------------------------
// 階段 1：歡迎首頁 (資產 10 Logo + RWD 安全區域對齊)
// -----------------------------------------------------------------------------
- (void)renderWelcomeScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;
    UIEdgeInsets safe = self.view.safeAreaInsets;

    CGFloat contentTop = safe.top > 0 ? safe.top : 44;
    CGFloat contentBottom = safe.bottom > 0 ? safe.bottom : 20;
    CGFloat usableHeight = h - contentTop - contentBottom;

    // 1. 頂部圓形徽章 (直徑 120pt，內嵌官方「資產 10」Logo)
    CGFloat badgeSize = 120.0f;
    CGFloat badgeY = contentTop + usableHeight * 0.16f;
    UIView *badge = [[UIView alloc] initWithFrame:CGRectMake((w - badgeSize) / 2, badgeY, badgeSize, badgeSize)];
    badge.backgroundColor = COLOR_CARD_WHITE;
    badge.layer.cornerRadius = badgeSize / 2.0f;
    badge.layer.borderColor = [COLOR_WARM_AMBER CGColor];
    badge.layer.borderWidth = 3.5f;
    badge.layer.shadowColor = [[UIColor blackColor] CGColor];
    badge.layer.shadowOpacity = 0.12f;
    badge.layer.shadowOffset = CGSizeMake(0, 4);
    badge.layer.shadowRadius = 8;

    UIImageView *iconView = [[UIImageView alloc] initWithFrame:CGRectMake(16, 16, badgeSize - 32, badgeSize - 32)];
    iconView.image = EFDGetAsset10Logo();
    iconView.contentMode = UIViewContentModeScaleAspectFit;
    iconView.clipsToBounds = YES;
    [badge addSubview:iconView];
    [self.contentContainer addSubview:badge];

    // 2. 主標題 (放大 20% 醒目白字)
    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, CGRectGetMaxY(badge.frame) + 28, w - 40, 44)];
    title.text = @"感謝協助測試 EFD";
    title.textColor = COLOR_CARD_WHITE;
    title.font = [UIFont boldSystemFontOfSize:28];
    title.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:title];

    // 副標題 (暖色淡黃 #F7E3AF)
    UILabel *subTitle = [[UILabel alloc] initWithFrame:CGRectMake(20, CGRectGetMaxY(title.frame) + 6, w - 40, 24)];
    subTitle.text = @"AI 驅動眼睛特徵提取與即時疲勞監控研究系統";
    subTitle.textColor = COLOR_WARM_AMBER;
    subTitle.font = [UIFont boldSystemFontOfSize:14];
    subTitle.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:subTitle];

    // 3. 進入測試說明按鈕 (白底綠字，貼齊底部安全區域上方，絕不切邊)
    CGFloat btnH = 54;
    CGFloat btnY = h - contentBottom - btnH - 24;
    UIButton *enterBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    enterBtn.frame = CGRectMake(36, btnY, w - 72, btnH);
    [enterBtn setTitle:@"進入測試說明" forState:UIControlStateNormal];
    [enterBtn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
    enterBtn.backgroundColor = COLOR_CARD_WHITE;
    enterBtn.layer.cornerRadius = 22;
    enterBtn.layer.shadowColor = [[UIColor blackColor] CGColor];
    enterBtn.layer.shadowOpacity = 0.10f;
    enterBtn.layer.shadowOffset = CGSizeMake(0, 3);
    enterBtn.layer.shadowRadius = 6;
    enterBtn.titleLabel.font = [UIFont boldSystemFontOfSize:18];
    [enterBtn addTarget:self action:@selector(onEnterInstructionClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:enterBtn];
}

- (void)onEnterInstructionClicked {
    [self switchToStage:EFDAppStageInstruction];
}

// -----------------------------------------------------------------------------
// 階段 2：測驗指引說明介面 (全 RWD 滾動視圖 + 獨立固定底部按鈕)
// -----------------------------------------------------------------------------
- (void)renderInstructionScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;
    UIEdgeInsets safe = self.view.safeAreaInsets;

    CGFloat topY = (safe.top > 0 ? safe.top : 44) + 12;
    CGFloat bottomInset = safe.bottom > 0 ? safe.bottom : 20;

    // 標題
    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, topY, w - 40, 36)];
    title.text = @"眼動特徵提取測驗說明";
    title.textColor = COLOR_CARD_WHITE;
    title.font = [UIFont boldSystemFontOfSize:25];
    title.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:title];

    // 底部按鈕區高度計算
    CGFloat btnH1 = 48;
    CGFloat btnH2 = 42;
    CGFloat bottomAreaH = btnH1 + btnH2 + 16 + bottomInset;

    // 中間可滾動內容容器 (UIScrollView 徹底杜絕卡片與按鈕重疊)
    CGFloat scrollY = CGRectGetMaxY(title.frame) + 14;
    CGFloat scrollH = h - scrollY - bottomAreaH;
    UIScrollView *scrollView = [[UIScrollView alloc] initWithFrame:CGRectMake(0, scrollY, w, scrollH)];
    scrollView.showsVerticalScrollIndicator = YES;
    scrollView.alwaysBounceVertical = YES;

    NSArray *cards = @[
        @{@"t": @"臉部正面對齊鏡頭", @"d": @"保持端正坐姿，確保 iPhone 前置原深感/FaceTime 鏡頭能清晰捕捉雙眼特徵。"},
        @{@"t": @"視線專注平視螢幕", @"d": @"校準開始後請專注凝視螢幕中央指示標記，避免大幅度晃動手機。"},
        @{@"t": @"保持自然眨眼狀態", @"d": @"採樣過程僅需 3.5 秒鐘，系統將自適應計算個人化基準 EAR 閉眼門檻。"}
    ];

    CGFloat curY = 0;
    CGFloat cardH = 82;
    CGFloat gap = 12;

    for (NSInteger i = 0; i < cards.count; ++i) {
        UIView *card = [[UIView alloc] initWithFrame:CGRectMake(20, curY, w - 40, cardH)];
        card.backgroundColor = [UIColor colorWithRed:20/255.0 green:140/255.0 blue:108/255.0 alpha:0.85];
        card.layer.cornerRadius = 14;

        UILabel *cTitle = [[UILabel alloc] initWithFrame:CGRectMake(16, 12, card.frame.size.width - 32, 22)];
        cTitle.text = cards[i][@"t"];
        cTitle.textColor = COLOR_WARM_AMBER;
        cTitle.font = [UIFont boldSystemFontOfSize:15];
        [card addSubview:cTitle];

        UILabel *cDesc = [[UILabel alloc] initWithFrame:CGRectMake(16, 36, card.frame.size.width - 32, 38)];
        cDesc.text = cards[i][@"d"];
        cDesc.textColor = COLOR_CARD_WHITE;
        cDesc.font = [UIFont systemFontOfSize:12];
        cDesc.numberOfLines = 2;
        [card addSubview:cDesc];

        [scrollView addSubview:card];
        curY += (cardH + gap);
    }

    scrollView.contentSize = CGSizeMake(w, curY + 8);
    [self.contentContainer addSubview:scrollView];

    // 底部按鈕 1：我準備好了，開始校準
    UIButton *readyBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    readyBtn.frame = CGRectMake(36, h - bottomAreaH + 4, w - 72, btnH1);
    [readyBtn setTitle:@"我準備好了，開始校準" forState:UIControlStateNormal];
    [readyBtn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
    readyBtn.backgroundColor = COLOR_CARD_WHITE;
    readyBtn.layer.cornerRadius = 20;
    readyBtn.titleLabel.font = [UIFont boldSystemFontOfSize:16];
    [readyBtn addTarget:self action:@selector(onStartCalibClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:readyBtn];

    // 底部按鈕 2：返回歡迎介面
    UIButton *backBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    backBtn.frame = CGRectMake(36, CGRectGetMaxY(readyBtn.frame) + 8, w - 72, btnH2);
    [backBtn setTitle:@"返回歡迎介面" forState:UIControlStateNormal];
    [backBtn setTitleColor:COLOR_CARD_WHITE forState:UIControlStateNormal];
    backBtn.backgroundColor = [UIColor colorWithRed:20/255.0 green:120/255.0 blue:90/255.0 alpha:0.65];
    backBtn.layer.cornerRadius = 18;
    backBtn.titleLabel.font = [UIFont boldSystemFontOfSize:14];
    [backBtn addTarget:self action:@selector(onBackToWelcomeClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:backBtn];
}

- (void)onStartCalibClicked {
    [self switchToStage:EFDAppStageCalibration];
}

- (void)onBackToWelcomeClicked {
    [self switchToStage:EFDAppStageWelcome];
}

// -----------------------------------------------------------------------------
// 階段 3：實際眼動特徵校準 (3.5 秒動態採樣)
// -----------------------------------------------------------------------------
- (void)renderCalibrationScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;
    UIEdgeInsets safe = self.view.safeAreaInsets;

    CGFloat topY = (safe.top > 0 ? safe.top : 44) + 16;
    CGFloat bottomInset = safe.bottom > 0 ? safe.bottom : 20;

    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, topY, w - 40, 36)];
    title.text = @"眼動特徵基準校準中...";
    title.textColor = COLOR_CARD_WHITE;
    title.font = [UIFont boldSystemFontOfSize:25];
    title.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:title];

    // 動態黃色凝視點
    CGFloat dotSize = 60.0f;
    UIView *dotView = [[UIView alloc] initWithFrame:CGRectMake((w - dotSize) / 2, h * 0.38f, dotSize, dotSize)];
    dotView.backgroundColor = COLOR_WARM_AMBER;
    dotView.layer.cornerRadius = dotSize / 2.0f;
    dotView.layer.borderColor = [[COLOR_ACCENT_BLUE colorWithAlphaComponent:0.8] CGColor];
    dotView.layer.borderWidth = 5.0f;
    [self.contentContainer addSubview:dotView];

    self.calibHintLabel = [[UILabel alloc] initWithFrame:CGRectMake(20, CGRectGetMaxY(dotView.frame) + 32, w - 40, 30)];
    self.calibHintLabel.text = @"請專注凝視黃色標記，保持臉部穩定...";
    self.calibHintLabel.textColor = COLOR_CARD_WHITE;
    self.calibHintLabel.font = [UIFont boldSystemFontOfSize:15];
    self.calibHintLabel.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:self.calibHintLabel];

    self.calibProgressView = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
    self.calibProgressView.frame = CGRectMake(40, h - bottomInset - 80, w - 80, 12);
    self.calibProgressView.progressTintColor = COLOR_WARM_AMBER;
    self.calibProgressView.trackTintColor = [UIColor colorWithRed:20/255.0 green:110/255.0 blue:85/255.0 alpha:0.8];
    self.calibProgressView.progress = 0.0f;
    [self.contentContainer addSubview:self.calibProgressView];

    self.calibElapsedSec = 0.0f;
    [self.calibTimer invalidate];
    self.calibTimer = [NSTimer scheduledTimerWithTimeInterval:0.05
                                                       target:self
                                                     selector:@selector(onCalibTick)
                                                     userInfo:nil
                                                      repeats:YES];
}

- (void)onCalibTick {
    self.calibElapsedSec += 0.05f;
    constexpr float totalSec = 3.5f;
    float progress = std::clamp(static_cast<float>(self.calibElapsedSec / totalSec), 0.0f, 1.0f);
    self.calibProgressView.progress = progress;

    if (progress >= 1.0f) {
        [self.calibTimer invalidate];
        self.calibTimer = nil;

        if (_engine) {
            _engine->calibrate(3.0f);
        }

        [self switchToStage:EFDAppStageDashboard];
    }
}

// -----------------------------------------------------------------------------
// 階段 4：即時眼睛疲勞監控中心 (4:3 實體相機串流 + 遙測數據 + 控制按鈕)
// -----------------------------------------------------------------------------
- (void)renderDashboardScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;
    UIEdgeInsets safe = self.view.safeAreaInsets;

    CGFloat topY = (safe.top > 0 ? safe.top : 44) + 8;
    CGFloat bottomInset = safe.bottom > 0 ? safe.bottom : 20;

    // 1. 頂部標題列
    UILabel *headerTitle = [[UILabel alloc] initWithFrame:CGRectMake(16, topY, w - 32, 32)];
    headerTitle.text = @"EFD 即時眼睛疲勞監控中心";
    headerTitle.textColor = COLOR_CARD_WHITE;
    headerTitle.font = [UIFont boldSystemFontOfSize:22];
    headerTitle.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:headerTitle];

    // 2. 攝影機預覽框 (嚴格鎖定 4:3 比例，純白底色，圓角 14pt)
    CGFloat camBoxW = w - 40;
    CGFloat camBoxH = camBoxW * (3.0f / 4.0f);
    CGFloat maxCamH = (h - topY - bottomInset) * 0.35f;
    if (camBoxH > maxCamH) {
        camBoxH = maxCamH;
        camBoxW = camBoxH * (4.0f / 3.0f);
    }
    CGFloat camBoxX = (w - camBoxW) / 2.0f;
    CGFloat camBoxY = CGRectGetMaxY(headerTitle.frame) + 10;

    self.cameraAspectBox = [[UIView alloc] initWithFrame:CGRectMake(camBoxX, camBoxY, camBoxW, camBoxH)];
    self.cameraAspectBox.backgroundColor = [UIColor colorWithRed:235/255.0 green:245/255.0 blue:240/255.0 alpha:1.0];
    self.cameraAspectBox.layer.cornerRadius = 14;
    self.cameraAspectBox.clipsToBounds = YES;
    self.cameraAspectBox.layer.borderColor = [[UIColor whiteColor] CGColor];
    self.cameraAspectBox.layer.borderWidth = 2.0f;
    [self.contentContainer addSubview:self.cameraAspectBox];

    // 攝影機內部狀態標籤
    self.cameraStatusLabel = [[UILabel alloc] initWithFrame:CGRectMake(10, 8, camBoxW - 20, 20)];
    self.cameraStatusLabel.text = @"📷 前置攝影機畫面 (4:3 比例鎖定)";
    self.cameraStatusLabel.textColor = COLOR_TEXT_DARK;
    self.cameraStatusLabel.font = [UIFont boldSystemFontOfSize:11];
    self.cameraStatusLabel.backgroundColor = [[UIColor whiteColor] colorWithAlphaComponent:0.75];
    self.cameraStatusLabel.layer.cornerRadius = 4;
    self.cameraStatusLabel.clipsToBounds = YES;
    [self.cameraAspectBox addSubview:self.cameraStatusLabel];

    // 嘗試掛載真實 AVFoundation 相機串流圖層
    [self attachCameraPreviewIfNeeded];

    // 3. 4 大監控指標卡片 (2x2 響應式網格)
    CGFloat metricsY = CGRectGetMaxY(self.cameraAspectBox.frame) + 10;
    CGFloat cardGap = 8;
    CGFloat metricCardW = (w - 40 - cardGap) / 2.0f;
    CGFloat metricCardH = 62.0f;

    auto createMetricCard = ^(CGRect frame, NSString* title, UILabel** outValLabel, NSString* defaultVal) {
        UIView *card = [[UIView alloc] initWithFrame:frame];
        card.backgroundColor = COLOR_CARD_WHITE;
        card.layer.cornerRadius = 12;
        card.layer.shadowColor = [[UIColor blackColor] CGColor];
        card.layer.shadowOpacity = 0.08f;
        card.layer.shadowOffset = CGSizeMake(0, 2);
        card.layer.shadowRadius = 4;

        UILabel *tLabel = [[UILabel alloc] initWithFrame:CGRectMake(10, 6, frame.size.width - 20, 16)];
        tLabel.text = title;
        tLabel.textColor = [COLOR_TEXT_DARK colorWithAlphaComponent:0.7];
        tLabel.font = [UIFont boldSystemFontOfSize:11];
        [card addSubview:tLabel];

        UILabel *vLabel = [[UILabel alloc] initWithFrame:CGRectMake(10, 22, frame.size.width - 20, frame.size.height - 26)];
        vLabel.text = defaultVal;
        vLabel.textColor = COLOR_TEXT_DARK;
        vLabel.font = [UIFont boldSystemFontOfSize:18];
        [card addSubview:vLabel];
        *outValLabel = vLabel;
        return card;
    };

    [self.contentContainer addSubview:createMetricCard(
        CGRectMake(20, metricsY, metricCardW, metricCardH),
        @"雙眼 EAR", &_earValueLabel, @"0.000"
    )];
    [self.contentContainer addSubview:createMetricCard(
        CGRectMake(20 + metricCardW + cardGap, metricsY, metricCardW, metricCardH),
        @"閉眼比 (PERCLOS)", &_perclosValueLabel, @"0.0%"
    )];
    [self.contentContainer addSubview:createMetricCard(
        CGRectMake(20, metricsY + metricCardH + cardGap, metricCardW, metricCardH),
        @"複雜度 (MSE / CI)", &_mseValueLabel, @"0.00"
    )];
    [self.contentContainer addSubview:createMetricCard(
        CGRectMake(20 + metricCardW + cardGap, metricsY + metricCardH + cardGap, metricCardW, metricCardH),
        @"綜合疲勞分數", &_fatigueScoreLabel, @"0.0"
    )];

    // 4. 底部控制按鈕 (貼齊安全區域上方)
    CGFloat btnAreaY = metricsY + (metricCardH * 2) + cardGap + 12;
    CGFloat btnH1 = 44;
    CGFloat btnH2 = 42;

    UIButton *recalibBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    recalibBtn.frame = CGRectMake(20, btnAreaY, w - 40, btnH1);
    [recalibBtn setTitle:@"重新偵測疲勞數據" forState:UIControlStateNormal];
    [recalibBtn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
    recalibBtn.backgroundColor = COLOR_CARD_WHITE;
    recalibBtn.layer.cornerRadius = 16;
    recalibBtn.titleLabel.font = [UIFont boldSystemFontOfSize:15];
    [recalibBtn addTarget:self action:@selector(onRecalibrateDataClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:recalibBtn];

    CGFloat subBtnW = (w - 48) / 2.0f;
    UIButton *settingsBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    settingsBtn.frame = CGRectMake(20, CGRectGetMaxY(recalibBtn.frame) + 8, subBtnW, btnH2);
    [settingsBtn setTitle:@"系統參數設定" forState:UIControlStateNormal];
    [settingsBtn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
    settingsBtn.backgroundColor = COLOR_CARD_WHITE;
    settingsBtn.layer.cornerRadius = 15;
    settingsBtn.titleLabel.font = [UIFont boldSystemFontOfSize:14];
    [settingsBtn addTarget:self action:@selector(onOpenSettingsClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:settingsBtn];

    UIButton *bgBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    bgBtn.frame = CGRectMake(CGRectGetMaxX(settingsBtn.frame) + 8, CGRectGetMaxY(recalibBtn.frame) + 8, subBtnW, btnH2);
    [bgBtn setTitle:@"背景運行模式" forState:UIControlStateNormal];
    [bgBtn setTitleColor:COLOR_CARD_WHITE forState:UIControlStateNormal];
    bgBtn.backgroundColor = COLOR_ALERT_CORAL;
    bgBtn.layer.cornerRadius = 15;
    bgBtn.titleLabel.font = [UIFont boldSystemFontOfSize:14];
    [bgBtn addTarget:self action:@selector(onMinimizeToBackgroundClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:bgBtn];

    if (_hasTelemetry) {
        [self updateTelemetryUI:_latestTelemetry];
    }
}

// 掛載實體相機 AVCaptureVideoPreviewLayer
- (void)attachCameraPreviewIfNeeded {
    if (!self.cameraAspectBox || !_engine) return;

    void* sessionHandle = _engine->getNativeCameraHandle();
    if (sessionHandle) {
        AVCaptureSession *session = (__bridge AVCaptureSession*)sessionHandle;
        if (!self.cameraPreviewLayer) {
            self.cameraPreviewLayer = [AVCaptureVideoPreviewLayer layerWithSession:session];
            self.cameraPreviewLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;
            self.cameraPreviewLayer.frame = self.cameraAspectBox.bounds;
            [self.cameraAspectBox.layer insertSublayer:self.cameraPreviewLayer atIndex:0];
            self.cameraStatusLabel.text = @"📷 前置攝影機 (實體鏡頭即時串流運作中)";
        }
    } else {
        // 若相機仍在啟動中，安排 0.5 秒後重試一次掛載
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            if (self.currentStage == EFDAppStageDashboard && !self.cameraPreviewLayer) {
                [self attachCameraPreviewIfNeeded];
            }
        });
    }
}

- (void)onRecalibrateDataClicked {
    [self switchToStage:EFDAppStageInstruction];
}

- (void)onOpenSettingsClicked {
    [self switchToStage:EFDAppStageSettings];
}

- (void)onMinimizeToBackgroundClicked {
    // 偏好 A：背景運行模式彈窗說明並啟動本地鬧鐘守護
    [self scheduleFatigueReminderNotificationInMinutes:20];

    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"守護中：背景定時提醒已啟動"
                                                                   message:@"受限於 iOS 隱私安全政策，切換至其他 App 時相機將安全休眠以節省電力。\n\n系統已為您排定「用眼超時鬧鐘提醒」（20 分鐘後響鈴提醒閉眼休息 5 分鐘）。"
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"確定" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

// -----------------------------------------------------------------------------
// 階段 5：系統設定介面 (RWD 滾動列表 + 獨立底部按鈕)
// -----------------------------------------------------------------------------
- (void)renderSettingsScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;
    UIEdgeInsets safe = self.view.safeAreaInsets;

    CGFloat topY = (safe.top > 0 ? safe.top : 44) + 12;
    CGFloat bottomInset = safe.bottom > 0 ? safe.bottom : 20;

    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, topY, w - 40, 36)];
    title.text = @"系統參數與偏好設定";
    title.textColor = COLOR_CARD_WHITE;
    title.font = [UIFont boldSystemFontOfSize:25];
    title.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:title];

    CGFloat btnH = 50;
    CGFloat bottomAreaH = btnH + 16 + bottomInset;

    CGFloat scrollY = CGRectGetMaxY(title.frame) + 12;
    CGFloat scrollH = h - scrollY - bottomAreaH;

    UIScrollView *scrollView = [[UIScrollView alloc] initWithFrame:CGRectMake(0, scrollY, w, scrollH)];
    scrollView.showsVerticalScrollIndicator = YES;
    scrollView.alwaysBounceVertical = YES;

    CGFloat curY = 0;
    CGFloat itemH = 68;
    CGFloat gap = 10;

    auto createSettingRow = ^(CGFloat y, NSString* t, NSString* d, BOOL initialVal, SEL action, BOOL locked) {
        UIView *card = [[UIView alloc] initWithFrame:CGRectMake(20, y, w - 40, itemH)];
        card.backgroundColor = COLOR_CARD_WHITE;
        card.layer.cornerRadius = 14;

        UILabel *tLabel = [[UILabel alloc] initWithFrame:CGRectMake(16, 12, card.frame.size.width - 86, 22)];
        tLabel.text = t;
        tLabel.textColor = COLOR_PRIMARY_MINT;
        tLabel.font = [UIFont boldSystemFontOfSize:15];
        [card addSubview:tLabel];

        UILabel *dLabel = [[UILabel alloc] initWithFrame:CGRectMake(16, 34, card.frame.size.width - 86, 24)];
        dLabel.text = d;
        dLabel.textColor = [COLOR_TEXT_DARK colorWithAlphaComponent:0.75];
        dLabel.font = [UIFont systemFontOfSize:11];
        [card addSubview:dLabel];

        UISwitch *toggle = [[UISwitch alloc] init];
        toggle.frame = CGRectMake(card.frame.size.width - 66, 18, 51, 31);
        toggle.onTintColor = COLOR_PRIMARY_MINT;
        toggle.on = initialVal;
        if (locked) {
            toggle.userInteractionEnabled = NO;
        } else if (action) {
            [toggle addTarget:self action:action forControlEvents:UIControlEventValueChanged];
        }
        [card addSubview:toggle];
        return card;
    };

    [scrollView addSubview:createSettingRow(curY, @"高靈敏度疲勞偵測", @"縮短判定窗口 (提升至 3 分鐘快篩頻率)", self.highSensitivityEnabled, @selector(onHighSensitivityChanged:), NO)];
    curY += (itemH + gap);

    [scrollView addSubview:createSettingRow(curY, @"疲勞警報提示音", @"疲勞超標時發出柔和提示聲響與震動", self.soundAlertEnabled, @selector(onSoundAlertChanged:), NO)];
    curY += (itemH + gap);

    [scrollView addSubview:createSettingRow(curY, @"開機自動背景啟動", @"裝置啟動後自動於背景待命守護", self.autoStartEnabled, @selector(onAutoStartChanged:), NO)];
    curY += (itemH + gap);

    [scrollView addSubview:createSettingRow(curY, @"自動科研同步", @"系統預設恆定啟用 (每晚 21:00 自動上傳)", YES, nil, YES)];
    curY += (itemH + gap);

    scrollView.contentSize = CGSizeMake(w, curY + 8);
    [self.contentContainer addSubview:scrollView];

    UIButton *backBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    backBtn.frame = CGRectMake(36, h - bottomAreaH + 4, w - 72, btnH);
    [backBtn setTitle:@"返回監控中心" forState:UIControlStateNormal];
    [backBtn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
    backBtn.backgroundColor = COLOR_CARD_WHITE;
    backBtn.layer.cornerRadius = 20;
    backBtn.titleLabel.font = [UIFont boldSystemFontOfSize:16];
    [backBtn addTarget:self action:@selector(onReturnToDashboardClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:backBtn];
}

- (void)onHighSensitivityChanged:(UISwitch*)sw {
    self.highSensitivityEnabled = sw.on;
}

- (void)onSoundAlertChanged:(UISwitch*)sw {
    self.soundAlertEnabled = sw.on;
}

- (void)onAutoStartChanged:(UISwitch*)sw {
    self.autoStartEnabled = sw.on;
}

- (void)onReturnToDashboardClicked {
    [self switchToStage:EFDAppStageDashboard];
}

// -----------------------------------------------------------------------------
// 階段 6：第 14 天後測門禁解鎖介面 (全 RWD 支援)
// -----------------------------------------------------------------------------
- (void)renderStudyGateScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;
    UIEdgeInsets safe = self.view.safeAreaInsets;

    CGFloat topY = (safe.top > 0 ? safe.top : 44) + 12;
    CGFloat bottomInset = safe.bottom > 0 ? safe.bottom : 20;

    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, topY, w - 40, 36)];
    title.text = @"14 天研究實驗完成 (後測介面)";
    title.textColor = COLOR_CARD_WHITE;
    title.font = [UIFont boldSystemFontOfSize:25];
    title.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:title];

    UIView *card = [[UIView alloc] initWithFrame:CGRectMake(24, CGRectGetMaxY(title.frame) + 16, w - 48, 170)];
    card.backgroundColor = [UIColor colorWithRed:20/255.0 green:140/255.0 blue:108/255.0 alpha:0.85];
    card.layer.cornerRadius = 16;

    UILabel *cTitle = [[UILabel alloc] initWithFrame:CGRectMake(16, 14, card.frame.size.width - 32, 24)];
    cTitle.text = @"受試者 14 天時序已安全封存";
    cTitle.textColor = COLOR_WARM_AMBER;
    cTitle.font = [UIFont boldSystemFontOfSize:16];
    [card addSubview:cTitle];

    UILabel *cDesc = [[UILabel alloc] initWithFrame:CGRectMake(16, 42, card.frame.size.width - 32, 115)];
    cDesc.text = @"雙眼特徵時序記錄已全數完成！\n本機資料庫已完成落盤簽章校驗。\n請點擊下方按鈕前往填寫後測問卷以完成整體科研流程。\n問卷網址：https://forms.gle/y5f1jTnrrtsxz65G9";
    cDesc.textColor = COLOR_CARD_WHITE;
    cDesc.font = [UIFont systemFontOfSize:13];
    cDesc.numberOfLines = 5;
    [card addSubview:cDesc];
    [self.contentContainer addSubview:card];

    CGFloat btnH1 = 48;
    CGFloat btnH2 = 42;
    CGFloat bottomAreaH = btnH1 + btnH2 + 16 + bottomInset;

    UIButton *formBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    formBtn.frame = CGRectMake(36, h - bottomAreaH + 4, w - 72, btnH1);
    [formBtn setTitle:@"前往填寫後測問卷 (Google 表單)" forState:UIControlStateNormal];
    [formBtn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
    formBtn.backgroundColor = COLOR_CARD_WHITE;
    formBtn.layer.cornerRadius = 20;
    formBtn.titleLabel.font = [UIFont boldSystemFontOfSize:16];
    [formBtn addTarget:self action:@selector(onOpenPostSurveyFormClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:formBtn];

    UIButton *backBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    backBtn.frame = CGRectMake(36, CGRectGetMaxY(formBtn.frame) + 8, w - 72, btnH2);
    [backBtn setTitle:@"返回監控中心" forState:UIControlStateNormal];
    [backBtn setTitleColor:COLOR_CARD_WHITE forState:UIControlStateNormal];
    backBtn.backgroundColor = [UIColor colorWithRed:20/255.0 green:120/255.0 blue:90/255.0 alpha:0.65];
    backBtn.layer.cornerRadius = 18;
    backBtn.titleLabel.font = [UIFont boldSystemFontOfSize:14];
    [backBtn addTarget:self action:@selector(onReturnToDashboardClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:backBtn];
}

- (void)onOpenPostSurveyFormClicked {
    NSURL *url = [NSURL URLWithString:@"https://forms.gle/y5f1jTnrrtsxz65G9"];
    if ([[UIApplication sharedApplication] canOpenURL:url]) {
        [[UIApplication sharedApplication] openURL:url options:@{} completionHandler:^(BOOL success) {
            if (self->_engine) {
                self->_engine->getStudyTracker().submitQuestionnaire("Study_Post_Survey_Completed_iOS");
            }
        }];
    }
}

// -----------------------------------------------------------------------------
// 即時遙測數據刷新 (當使用者離座或無人臉時，明確標記離座暫停並停止疲勞累積)
// -----------------------------------------------------------------------------
- (void)updateTelemetryUI:(const efd::EngineTelemetry&)t {
    if (self.currentStage != EFDAppStageDashboard) return;

    if (!t.detection.hasFace) {
        if (self.cameraStatusLabel) {
            self.cameraStatusLabel.text = @"⚠️ 偵測暫停：未在畫面中偵測到人臉 (已暫停疲勞計算)";
            self.cameraStatusLabel.textColor = COLOR_ALERT_CORAL;
        }
        if (self.earValueLabel) {
            self.earValueLabel.text = @"--";
        }
        if (self.perclosValueLabel) {
            self.perclosValueLabel.text = @"離座暫停";
        }
        if (self.mseValueLabel) {
            self.mseValueLabel.text = @"--";
        }
        if (self.fatigueScoreLabel) {
            self.fatigueScoreLabel.text = @"暫停中";
        }
        return;
    }

    if (self.cameraStatusLabel) {
        self.cameraStatusLabel.text = @"📷 前置攝影機 (人臉鎖定中・疲勞分析正常運作)";
        self.cameraStatusLabel.textColor = COLOR_TEXT_DARK;
    }
    if (self.earValueLabel) {
        self.earValueLabel.text = [NSString stringWithFormat:@"%.3f", t.eyeMetrics.earAvg];
    }
    if (self.perclosValueLabel) {
        self.perclosValueLabel.text = [NSString stringWithFormat:@"%.1f%%", t.eyeMetrics.perclos * 100.0f];
    }
    if (self.mseValueLabel) {
        self.mseValueLabel.text = [NSString stringWithFormat:@"%.2f", t.complexityMetrics.complexityIndex];
    }
    if (self.fatigueScoreLabel) {
        self.fatigueScoreLabel.text = [NSString stringWithFormat:@"%.1f", t.systemState.smoothedFatigueScore];
    }
}

// -----------------------------------------------------------------------------
// 疲勞超標警報震動與通知發送 (使用者離座時全面靜音抑制)
// -----------------------------------------------------------------------------
- (void)triggerFatigueAlertNotification:(float)score {
    // 若無人在鏡頭前，抑制警報避免誤報打擾
    if (_hasTelemetry && !_latestTelemetry.detection.hasFace) {
        return;
    }

    UINotificationFeedbackGenerator *feedback = [[UINotificationFeedbackGenerator alloc] init];
    [feedback notificationOccurred:UINotificationFeedbackTypeWarning];

    if (self.soundAlertEnabled) {
        AudioServicesPlaySystemSound(1005);
    }

    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"🚨 眼睛疲勞提醒"
                                                                   message:[NSString stringWithFormat:@"疲勞綜合評分已達 %.1f 分！\n你的眼睛處於疲勞狀態，請適當休息放鬆 5 分鐘。", score]
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"我知道了，閉眼休息" style:UIAlertActionStyleDestructive handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

@end
