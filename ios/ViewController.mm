#import "ViewController.h"
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <UserNotifications/UserNotifications.h>
#include <memory>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <sys/utsname.h>
#include "engine/AsyncPipelineEngine.hpp"

// 診斷模式一次匯出的畫面張數 (每隔 4 幀取一張，約 0.4 秒)
static const int kDiagnosticExportFrames = 5;
static NSString * const kDiagnosticModeDefaultsKey = @"efd_diagnostic_mode";

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

    // 最新一幀的偵測診斷 (主執行緒讀寫)
    efd::FaceLandmarker::PresenceDiagnostic _latestDiag;
    BOOL _latestFrameSynthetic;
    int _latestFrameW;
    int _latestFrameH;
    float _latestFps;

    // 校準期間的統計：總幀數、有人臉的幀數、各關卡失敗次數
    int _calibFrames;
    int _calibFaceFrames;
    std::map<std::string, int> _calibFailCounts;

    // 最近 10 秒的逐幀結果 (時間, "face" 或失敗關卡)，錯誤回報用來看「時好時壞」的比例
    std::deque<std::pair<CFTimeInterval, std::string>> _recentDetections;

    // 診斷畫面匯出 (引擎執行緒取幀，背景佇列寫檔)
    std::atomic<int> _exportFramesRemaining;
    int _exportFrameSkip;
    dispatch_queue_t _exportQueue;
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
@property (nonatomic, strong) UIView *calibDotView;
@property (nonatomic, assign) CGRect calibDotArea;
@property (nonatomic, strong) NSTimer *calibTimer;
@property (nonatomic, assign) CGFloat calibElapsedSec;

// 設定項目狀態
@property (nonatomic, assign) BOOL highSensitivityEnabled;
@property (nonatomic, assign) BOOL soundAlertEnabled;
// iOS 不允許 App 開機自動啟動，因此沒有「開機自動啟動」設定

// 診斷模式：顯示即時偵測數值，並可匯出實際畫面供開發者重現問題 (預設關閉，不影響研究資料)
@property (nonatomic, assign) BOOL diagnosticModeEnabled;
@property (nonatomic, strong) UILabel *diagLabel;
@property (nonatomic, strong) NSMutableArray<NSURL *> *exportedFiles;
@property (nonatomic, copy) NSString *exportStamp;

// 背景定時檢查 (科研同步 & 門禁檢查)
@property (nonatomic, strong) NSTimer *backgroundCheckTimer;

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = COLOR_PRIMARY_MINT;

    self.highSensitivityEnabled = NO;
    self.soundAlertEnabled = YES;
    self.diagnosticModeEnabled = [[NSUserDefaults standardUserDefaults] boolForKey:kDiagnosticModeDefaultsKey];
    _exportFramesRemaining.store(0);
    _exportFrameSkip = 0;
    _exportQueue = dispatch_queue_create("com.efd.diagnostic.export", DISPATCH_QUEUE_SERIAL);
    self.currentStage = EFDAppStageWelcome;

    // 建立全螢幕自適應容器
    self.contentContainer = [[UIView alloc] initWithFrame:self.view.bounds];
    self.contentContainer.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.contentContainer.backgroundColor = [UIColor clearColor];
    [self.view addSubview:self.contentContainer];

    // 初始化 C++20 跨平台管線引擎 (iOS 平台原生)
    _engine = new efd::AsyncPipelineEngine(efd::PlatformType::iOS);

    // App 工作目錄為唯讀，設定檔/資料庫/冷卻狀態必須存放在 Application Support，
    // 否則每次開啟都會產生新受試者、回到第 1 天且需要重新校準
    NSString *dataDir = [ViewController efdDataDirectory];
    if (dataDir) {
        _engine->setDataDirectory(std::string(dataDir.UTF8String));
    }
    self.highSensitivityEnabled = (_engine->getSensitivity() == efd::FatigueStateMachine::kSensitivityHigh);

    // 檢查是否已達第 14 天後測門禁；已完成初次校準則直接進入監控中心
    if (_engine->getStudyTracker().getStatus() == efd::StudyStatus::LockedForPostTest) {
        self.currentStage = EFDAppStageStudyGate;
    } else if (_engine->hasSavedCalibration() || _engine->getStudyTracker().isInitialSetupCompleted()) {
        self.currentStage = EFDAppStageDashboard;
    }

    // 遙測回呼 (於引擎執行緒觸發；只複製 UI 需要的欄位，並在主執行緒寫入，避免資料競爭)
    _engine->setTelemetryCallback([self](const efd::EngineTelemetry& t) {
        efd::EngineTelemetry slim;
        slim.detection.hasFace = t.detection.hasFace;
        slim.eyeMetrics = t.eyeMetrics;
        slim.complexityMetrics = t.complexityMetrics;
        slim.systemState = t.systemState;
        slim.currentThreshold = t.currentThreshold;
        slim.presenceDiagnostic = t.presenceDiagnostic;
        slim.measuredFps = t.measuredFps;
        const BOOL synthetic = t.latestFrame.isSynthetic ? YES : NO;
        const int frameW = t.latestFrame.width;
        const int frameH = t.latestFrame.height;

        // 診斷匯出：每隔 4 幀複製一張實際畫面 (只在使用者按下匯出時)
        if (self->_exportFramesRemaining.load() > 0 && t.latestFrame.isValid() && (++self->_exportFrameSkip % 4) == 0) {
            const int remaining = self->_exportFramesRemaining.fetch_sub(1);
            if (remaining > 0) {
                auto frame = std::make_shared<efd::RawFrame>(t.latestFrame);
                auto diag = std::make_shared<efd::FaceLandmarker::PresenceDiagnostic>(t.presenceDiagnostic);
                const int index = kDiagnosticExportFrames - remaining + 1;
                dispatch_async(self->_exportQueue, ^{
                    [self writeDiagnosticFrame:*frame diagnostic:*diag index:index];
                });
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            self->_latestTelemetry = slim;
            self->_hasTelemetry = YES;
            self->_latestDiag = slim.presenceDiagnostic;
            self->_latestFrameSynthetic = synthetic;
            self->_latestFrameW = frameW;
            self->_latestFrameH = frameH;
            self->_latestFps = slim.measuredFps;
            [self recordCalibrationFrame:slim];
            [self recordRecentDetection:slim];
            [self updateTelemetryUI:slim];
            [self updateDiagnosticLabel];
        });
    });

    // 警報回呼：是否提醒完全由 20/5/5 狀態機決定 (只會在 SevereWarning 時觸發)
    _engine->setAlertCallback([self](efd::FatigueLevel level, float score, const std::string& msg) {
        (void)level;
        (void)msg;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self triggerFatigueAlertNotification:score];
        });
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

+ (NSString *)efdDataDirectory {
    NSArray<NSURL *> *urls = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                                                    inDomains:NSUserDomainMask];
    if (urls.count == 0) return nil;
    NSURL *dirUrl = [urls.firstObject URLByAppendingPathComponent:@"EFD" isDirectory:YES];
    [[NSFileManager defaultManager] createDirectoryAtURL:dirUrl
                             withIntermediateDirectories:YES
                                              attributes:nil
                                                   error:nil];
    return dirUrl.path;
}

- (void)onAppDidEnterBackground {
    // iOS 不允許 App 在背景使用相機：暫停管線並保存冷卻狀態
    if (_engine) {
        _engine->pause();
    }
    // 背景期間無法偵測，改排定一次性的休息提醒；冷卻期間不提醒，延到冷卻結束
    int delaySeconds = 20 * 60;
    if (_engine) {
        efd::SystemState state = _engine->getStateMachine().getState();
        if (state.cooldownState == efd::CooldownState::InCooldown) {
            delaySeconds = std::max(delaySeconds, state.cooldownRemainingSeconds);
        }
    }
    [self scheduleFatigueReminderNotificationInSeconds:delaySeconds];
}

- (void)onAppWillEnterForeground {
    // 使用者返回 EFD，清除排程中尚未觸發的疲勞提醒
    [[UNUserNotificationCenter currentNotificationCenter] removePendingNotificationRequestsWithIdentifiers:@[@"EFD_FATIGUE_ALARM_20M"]];
    if (_engine) {
        // 背景時間會比照離座處理 (滿 5 分鐘清除冷卻)，並補傳錯過的每日資料
        _engine->resume();
        _engine->checkAndTriggerNightlySync();
    }
    if (self.currentStage == EFDAppStageDashboard) {
        [UIApplication sharedApplication].idleTimerDisabled = YES;
        [self attachCameraPreviewIfNeeded];
    }
}

// 背景同步 (BGAppRefreshTask)：觸發補傳/夜間同步並等待結果，最多約 25 秒
- (void)runBackgroundSyncWithCompletion:(void (^)(BOOL success))completion {
    efd::AsyncPipelineEngine *engine = _engine;
    if (!engine) {
        completion(NO);
        return;
    }
    const int before = engine->getStudyTracker().getLastSyncedDate();
    if (!engine->checkAndTriggerNightlySync()) {
        completion(YES); // 沒有需要上傳的資料
        return;
    }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        for (int i = 0; i < 250 && engine->getSyncWorker().isSyncing(); ++i) {
            [NSThread sleepForTimeInterval:0.1];
        }
        [NSThread sleepForTimeInterval:0.1];
        BOOL ok = engine->getStudyTracker().getLastSyncedDate() != before;
        completion(ok);
    });
}

- (void)scheduleFatigueReminderNotificationInSeconds:(int)seconds {
    UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
    content.title = @"EFD 眼睛休息提醒";
    content.body = [NSString stringWithFormat:@"已離開 EFD 偵測畫面 %d 分鐘，若仍持續使用手機，建議閉眼休息 5 分鐘。", seconds / 60];
    content.sound = [UNNotificationSound defaultSound];

    UNTimeIntervalNotificationTrigger *trigger = [UNTimeIntervalNotificationTrigger triggerWithTimeInterval:seconds repeats:NO];
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
    self.cameraAspectBox = nil;
    self.cameraStatusLabel = nil;
    self.diagLabel = nil;
    self.calibHintLabel = nil;

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
        @{@"t": @"眼睛跟著黃點移動", @"d": @"校準開始後黃點會依序移到螢幕四個角落，請保持頭部不動、只用眼睛跟隨，避免晃動手機。"},
        @{@"t": @"保持自然眨眼狀態", @"d": @"採樣過程約 5 秒鐘，系統將自適應計算個人化基準 EAR 閉眼門檻。"}
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
// 階段 3：實際眼動特徵校準 (黃點 5 點巡迴，5 秒動態採樣，不顯示鏡頭畫面)
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

    // 黃點移動區域 (不顯示鏡頭畫面)：與 Windows 版相同，依 中心→左上→右上→右下→左下→中心 巡迴
    CGFloat progressY = h - bottomInset - 80;
    CGFloat hintH = 44;
    CGFloat diagH = self.diagnosticModeEnabled ? 70 : 0;
    CGFloat areaTop = CGRectGetMaxY(title.frame) + 24;
    CGFloat areaBottom = progressY - hintH - diagH - 24;
    self.calibDotArea = CGRectMake(24, areaTop, w - 48, std::max<CGFloat>(120, areaBottom - areaTop));

    CGFloat dotSize = 52.0f;
    self.calibDotView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, dotSize, dotSize)];
    self.calibDotView.backgroundColor = COLOR_WARM_AMBER;
    self.calibDotView.layer.cornerRadius = dotSize / 2.0f;
    self.calibDotView.layer.borderColor = [[COLOR_ACCENT_BLUE colorWithAlphaComponent:0.8] CGColor];
    self.calibDotView.layer.borderWidth = 5.0f;
    self.calibDotView.center = CGPointMake(CGRectGetMidX(self.calibDotArea), CGRectGetMidY(self.calibDotArea));
    [self.contentContainer addSubview:self.calibDotView];

    self.calibHintLabel = [[UILabel alloc] initWithFrame:CGRectMake(20, CGRectGetMaxY(self.calibDotArea) + 12, w - 40, hintH)];
    self.calibHintLabel.text = @"請保持臉部不動，只用眼睛跟著黃點移動...";
    self.calibHintLabel.textColor = COLOR_CARD_WHITE;
    self.calibHintLabel.font = [UIFont boldSystemFontOfSize:14];
    self.calibHintLabel.textAlignment = NSTextAlignmentCenter;
    self.calibHintLabel.numberOfLines = 2;
    [self.contentContainer addSubview:self.calibHintLabel];

    if (self.diagnosticModeEnabled) {
        self.diagLabel = [self makeDiagnosticLabelWithFrame:CGRectMake(20, CGRectGetMaxY(self.calibHintLabel.frame) + 4, w - 40, progressY - CGRectGetMaxY(self.calibHintLabel.frame) - 12)];
        [self.contentContainer addSubview:self.diagLabel];
    }

    _calibFrames = 0;
    _calibFaceFrames = 0;
    _calibFailCounts.clear();

    self.calibProgressView = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
    self.calibProgressView.frame = CGRectMake(40, h - bottomInset - 80, w - 80, 12);
    self.calibProgressView.progressTintColor = COLOR_WARM_AMBER;
    self.calibProgressView.trackTintColor = [UIColor colorWithRed:20/255.0 green:110/255.0 blue:85/255.0 alpha:0.8];
    self.calibProgressView.progress = 0.0f;
    [self.contentContainer addSubview:self.calibProgressView];

    self.calibElapsedSec = 0.0f;
    if (_engine) {
        _engine->beginCalibration();
    }
    [self.calibTimer invalidate];
    self.calibTimer = [NSTimer scheduledTimerWithTimeInterval:0.05
                                                       target:self
                                                     selector:@selector(onCalibTick)
                                                     userInfo:nil
                                                      repeats:YES];
}

- (void)onCalibTick {
    self.calibElapsedSec += 0.05f;
    // 與 Windows 版相同：5 點巡迴，每段 1 秒，共 5 秒 (緩入緩出)
    constexpr float totalSec = 5.0f;
    float progress = std::clamp(static_cast<float>(self.calibElapsedSec / totalSec), 0.0f, 1.0f);
    self.calibProgressView.progress = progress;

    static const CGPoint kCalibPath[] = {
        {0.50, 0.50}, {0.15, 0.22}, {0.85, 0.22}, {0.85, 0.78}, {0.15, 0.78}, {0.50, 0.50}
    };
    const float elapsed = std::min(static_cast<float>(self.calibElapsedSec), totalSec - 0.001f);
    const int seg = std::clamp(static_cast<int>(elapsed), 0, 4);
    const float segT = elapsed - static_cast<float>(seg);
    const CGFloat smoothT = 0.5 * (1.0 - std::cos(segT * M_PI));
    const CGPoint p0 = kCalibPath[seg];
    const CGPoint p1 = kCalibPath[seg + 1];
    const CGRect area = self.calibDotArea;
    self.calibDotView.center = CGPointMake(area.origin.x + (p0.x + (p1.x - p0.x) * smoothT) * area.size.width,
                                           area.origin.y + (p0.y + (p1.y - p0.y) * smoothT) * area.size.height);

    if (progress >= 1.0f) {
        [self.calibTimer invalidate];
        self.calibTimer = nil;

        // 只採用校準期間有偵測到人臉的幀；不足 60% 視為失敗，不覆蓋原本的基準
        BOOL ok = _engine ? _engine->finishCalibration() : NO;
        if (ok) {
            [self switchToStage:EFDAppStageDashboard];
            return;
        }

        [self presentCalibrationFailureAlert];
    }
}

// 校準期間逐幀統計 (主執行緒)，失敗時用來說明主要原因
- (void)recordCalibrationFrame:(const efd::EngineTelemetry&)t {
    if (self.currentStage != EFDAppStageCalibration || !self.calibTimer) return;
    _calibFrames++;
    if (t.detection.hasFace) {
        _calibFaceFrames++;
        self.calibHintLabel.text = @"已偵測到臉部，請保持穩定...";
    } else {
        std::string stage = _latestFrameSynthetic ? "camera" : t.presenceDiagnostic.failStage;
        if (stage.empty()) stage = "tracking";
        _calibFailCounts[stage]++;
        self.calibHintLabel.text = [NSString stringWithFormat:@"尚未偵測到臉部：%@", [self reasonTextForStage:stage]];
    }
}

- (void)recordRecentDetection:(const efd::EngineTelemetry&)t {
    const CFTimeInterval now = [NSProcessInfo processInfo].systemUptime;
    std::string outcome = "face";
    if (!t.detection.hasFace) {
        outcome = _latestFrameSynthetic ? "camera" : t.presenceDiagnostic.failStage;
        if (outcome.empty()) outcome = "tracking";
    }
    _recentDetections.emplace_back(now, outcome);
    while (!_recentDetections.empty() && now - _recentDetections.front().first > 10.0) {
        _recentDetections.pop_front();
    }
}

// 最近 10 秒有偵測到臉的比例與各關卡失敗次數
- (NSString *)recentDetectionSummary {
    if (_recentDetections.empty()) return @"無資料";
    int faces = 0;
    std::map<std::string, int> fails;
    for (const auto& item : _recentDetections) {
        if (item.second == "face") faces++;
        else fails[item.second]++;
    }
    NSMutableString *text = [NSMutableString stringWithFormat:@"有臉 %.0f%% (%d/%d 幀)",
                             100.0 * faces / _recentDetections.size(), faces, static_cast<int>(_recentDetections.size())];
    if (!fails.empty()) {
        [text appendString:@" / 失敗:"];
        for (const auto& kv : fails) {
            [text appendFormat:@" %s=%d", kv.first.c_str(), kv.second];
        }
    }
    return text;
}

// 各關卡失敗原因的白話說明
- (NSString *)reasonTextForStage:(const std::string&)stage {
    if (stage == "camera")   return @"相機沒有畫面，請確認已允許相機權限";
    if (stage == "skin")     return @"畫面中找不到足夠的臉部膚色，可能太暗或臉不在畫面中";
    if (stage == "size")     return @"臉太大或太小，請把手機拿到約一個手臂的距離";
    if (stage == "aspect")   return @"臉部形狀不完整，請讓整張臉出現在框內";
    if (stage == "fill")     return @"臉部區域不連續，可能有遮擋或強烈陰影";
    if (stage == "eyes")     return @"找不到眼睛特徵，請正視鏡頭、避免反光或太暗";
    if (stage == "symmetry") return @"臉偏向一側，請正對鏡頭";
    return @"臉部追蹤中斷，請保持臉部穩定";
}

- (std::string)dominantCalibrationFailStage {
    std::string best;
    int bestCount = 0;
    for (const auto& kv : _calibFailCounts) {
        if (kv.second > bestCount) {
            best = kv.first;
            bestCount = kv.second;
        }
    }
    return best;
}

- (void)presentCalibrationFailureAlert {
    float ratio = _engine ? _engine->getLastCalibrationValidRatio() : 0.0f;
    std::string stage = [self dominantCalibrationFailStage];
    NSString *reason = stage.empty() ? @"校準期間沒有收到相機畫面" : [self reasonTextForStage:stage];
    NSString *message = [NSString stringWithFormat:@"%@\n\n有偵測到臉的畫面：%.0f%%（需要 60%% 以上）\n\n若一直失敗，請按「複製錯誤資訊」貼給研究人員。",
                         reason, ratio * 100.0f];

    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"校準未完成"
                                                                   message:message
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"重新校準" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        [self switchToStage:EFDAppStageCalibration];
    }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"複製錯誤資訊" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        [UIPasteboard generalPasteboard].string = [self diagnosticReportText];
        // 複製後回到同一個選擇，讓使用者決定重新校準或略過
        [self presentCalibrationFailureAlert];
    }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"略過，先使用預設基準" style:UIAlertActionStyleCancel handler:^(UIAlertAction *action) {
        // 記錄已走過初次設定，下次開啟不再強制從歡迎頁開始
        if (self->_engine) {
            self->_engine->getStudyTracker().setInitialSetupCompleted(true);
        }
        [self switchToStage:EFDAppStageDashboard];
    }]];
    [self presentViewController:alert animated:YES completion:nil];
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
    // 右上角「回報問題」：偵測異常時不必切到設定頁就能複製錯誤資訊
    UIButton *reportBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    reportBtn.frame = CGRectMake(camBoxW - 82, 6, 76, 24);
    [reportBtn setTitle:@"回報問題" forState:UIControlStateNormal];
    [reportBtn setTitleColor:COLOR_CARD_WHITE forState:UIControlStateNormal];
    reportBtn.titleLabel.font = [UIFont boldSystemFontOfSize:12];
    reportBtn.backgroundColor = [COLOR_ALERT_CORAL colorWithAlphaComponent:0.9];
    reportBtn.layer.cornerRadius = 12;
    [reportBtn addTarget:self action:@selector(onCopyDiagnosticReportClicked) forControlEvents:UIControlEventTouchUpInside];

    self.cameraStatusLabel = [[UILabel alloc] initWithFrame:CGRectMake(10, 8, camBoxW - 100, 20)];
    self.cameraStatusLabel.text = @"📷 前置攝影機畫面 (4:3 比例鎖定)";
    self.cameraStatusLabel.textColor = COLOR_TEXT_DARK;
    self.cameraStatusLabel.font = [UIFont boldSystemFontOfSize:11];
    self.cameraStatusLabel.backgroundColor = [[UIColor whiteColor] colorWithAlphaComponent:0.75];
    self.cameraStatusLabel.layer.cornerRadius = 4;
    self.cameraStatusLabel.clipsToBounds = YES;
    [self.cameraAspectBox addSubview:self.cameraStatusLabel];
    [self.cameraAspectBox addSubview:reportBtn];

    if (self.diagnosticModeEnabled) {
        CGFloat diagH = std::min<CGFloat>(96, camBoxH - 36);
        self.diagLabel = [self makeDiagnosticLabelWithFrame:CGRectMake(6, camBoxH - diagH - 6, camBoxW - 12, diagH)];
        [self.cameraAspectBox addSubview:self.diagLabel];
    }

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
        // 若相機仍在啟動中，0.5 秒後重試掛載 (監控頁與校準頁都有預覽框)
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            if (self.cameraAspectBox && !self.cameraPreviewLayer) {
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
    // 實際的提醒排程在 onAppDidEnterBackground 進行 (依冷卻狀態決定時間)
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"切換至背景後的限制"
                                                                   message:@"iOS 不允許 App 在背景使用相機，離開 EFD 畫面後會暫停疲勞偵測。\n\n系統會排定一次休息提醒 (20 分鐘後；若正處於冷卻期則延到冷卻結束)。每日資料會在下次開啟或系統允許的背景時段自動補傳。"
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

    [scrollView addSubview:createSettingRow(curY, @"高靈敏度提醒", @"≥58 分持續 5 秒即提醒 (標準為 ≥65 分持續 10 秒)", self.highSensitivityEnabled, @selector(onHighSensitivityChanged:), NO)];
    curY += (itemH + gap);

    [scrollView addSubview:createSettingRow(curY, @"疲勞警報提示音", @"疲勞超標時發出柔和提示聲響與震動", self.soundAlertEnabled, @selector(onSoundAlertChanged:), NO)];
    curY += (itemH + gap);


    [scrollView addSubview:createSettingRow(curY, @"自動科研同步", @"每晚 21:00 後上傳；錯過時於下次開啟自動補傳", YES, nil, YES)];
    curY += (itemH + gap);

    [scrollView addSubview:createSettingRow(curY, @"診斷模式", @"顯示偵測數值，可匯出畫面給研究人員除錯", self.diagnosticModeEnabled, @selector(onDiagnosticModeChanged:), NO)];
    curY += (itemH + gap);

    // 錯誤回報：任何時候都可複製診斷資訊 (純文字，不含影像)
    auto createActionButton = ^(CGFloat y, NSString *titleText, SEL action, BOOL enabled) {
        UIButton *btn = [UIButton buttonWithType:UIButtonTypeSystem];
        btn.frame = CGRectMake(20, y, w - 40, 44);
        [btn setTitle:titleText forState:UIControlStateNormal];
        [btn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
        [btn setTitleColor:[COLOR_TEXT_DARK colorWithAlphaComponent:0.35] forState:UIControlStateDisabled];
        btn.backgroundColor = [COLOR_CARD_WHITE colorWithAlphaComponent:enabled ? 1.0 : 0.6];
        btn.layer.cornerRadius = 14;
        btn.titleLabel.font = [UIFont boldSystemFontOfSize:15];
        btn.enabled = enabled;
        [btn addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
        return btn;
    };
    [scrollView addSubview:createActionButton(curY, @"複製錯誤資訊（貼給研究人員）", @selector(onCopyDiagnosticReportClicked), YES)];
    curY += 44 + gap;
    [scrollView addSubview:createActionButton(curY, self.diagnosticModeEnabled ? @"匯出診斷畫面（含臉部影像）" : @"匯出診斷畫面（需先開啟診斷模式）",
                                              @selector(onExportDiagnosticFramesClicked), self.diagnosticModeEnabled)];
    curY += 44 + gap;

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
    if (_engine) {
        // 只調整提醒門檻與持續秒數，20/5/5 冷卻規則不變；設定由 C++ 保存
        _engine->setSensitivity(sw.on ? efd::FatigueStateMachine::kSensitivityHigh
                                      : efd::FatigueStateMachine::kSensitivityStandard);
    }
}

- (void)onSoundAlertChanged:(UISwitch*)sw {
    self.soundAlertEnabled = sw.on;
}

- (void)onDiagnosticModeChanged:(UISwitch*)sw {
    self.diagnosticModeEnabled = sw.on;
    [[NSUserDefaults standardUserDefaults] setBool:sw.on forKey:kDiagnosticModeDefaultsKey];
    // 重新繪製設定頁，更新「匯出診斷畫面」按鈕狀態
    [self switchToStage:EFDAppStageSettings];
}

- (void)onCopyDiagnosticReportClicked {
    [UIPasteboard generalPasteboard].string = [self diagnosticReportText];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"已複製錯誤資訊"
                                                                   message:@"請貼到 LINE、Email 或訊息中傳給研究人員。內容只有文字數值，不含影像。"
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"好" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

// -----------------------------------------------------------------------------
// 診斷模式：即時數值、錯誤回報文字、畫面匯出
// -----------------------------------------------------------------------------
- (UILabel *)makeDiagnosticLabelWithFrame:(CGRect)frame {
    UILabel *label = [[UILabel alloc] initWithFrame:frame];
    label.numberOfLines = 0;
    label.font = [UIFont monospacedSystemFontOfSize:10 weight:UIFontWeightRegular];
    label.textColor = COLOR_CARD_WHITE;
    label.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.55];
    label.layer.cornerRadius = 8;
    label.clipsToBounds = YES;
    label.text = @" 診斷資料等待中...";
    return label;
}

// 目前偵測狀態的精簡數值 (診斷標籤與錯誤回報共用)
- (NSString *)diagnosticSummaryText {
    if (!_hasTelemetry) return @"尚未收到相機畫面";
    const auto& d = _latestDiag;
    NSString *result = (d.rawPersonInFrame && d.rawFacingCamera) ? @"有人臉" : @"沒有人臉";
    NSString *stage = d.failStage.empty() ? @"-" : [NSString stringWithUTF8String:d.failStage.c_str()];
    return [NSString stringWithFormat:
            @"相機:%@ %dx%d %.0ffps 結果:%@ 卡在:%@\n"
            @"膚色區 %d,%d %dx%d 臉框 %d,%d %dx%d 膚色亮度 %d\n"
            @"左眼 暗%.2f 反%.0f 緣%.1f | 右眼 暗%.2f 反%.0f 緣%.1f\n"
            @"長寬比 %.2f 填充 %.2f 對稱 %.2f",
            _latestFrameSynthetic ? @"模擬" : @"實體", _latestFrameW, _latestFrameH, _latestFps, result, stage,
            d.skinBoxX, d.skinBoxY, d.skinBoxW, d.skinBoxH, d.faceBoxX, d.faceBoxY, d.faceBoxW, d.faceBoxH, d.avgSkinLum,
            d.eyeDarkFraction[0], d.eyeContrast[0], d.eyeGradient[0], d.eyeDarkFraction[1], d.eyeContrast[1], d.eyeGradient[1],
            d.aspectRatio, d.fillDensity, d.symmetryRatio];
}

- (void)updateDiagnosticLabel {
    if (!self.diagLabel) return;
    self.diagLabel.text = [self diagnosticSummaryText];
}

// 錯誤回報：可貼給研究人員的純文字 (不含影像)
- (NSString *)diagnosticReportText {
    struct utsname systemInfo;
    uname(&systemInfo);
    NSString *model = [NSString stringWithUTF8String:systemInfo.machine];
    NSString *appVersion = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"?";
    NSString *subject = _engine ? [NSString stringWithUTF8String:_engine->getStudyTracker().getSubjectUuid().c_str()] : @"-";
    int day = _engine ? _engine->getStudyTracker().getCurrentDay() : 0;
    float ratio = _engine ? _engine->getLastCalibrationValidRatio() : 0.0f;
    BOOL calibrated = _engine ? _engine->hasSavedCalibration() : NO;

    NSMutableString *counts = [NSMutableString string];
    for (const auto& kv : _calibFailCounts) {
        [counts appendFormat:@"%s=%d ", kv.first.c_str(), kv.second];
    }

    NSDateFormatter *fmt = [[NSDateFormatter alloc] init];
    fmt.dateFormat = @"yyyy-MM-dd HH:mm:ss";

    return [NSString stringWithFormat:
            @"[EFD 錯誤回報]\n"
            @"時間: %@\n裝置: %@ / iOS %@ / App %@\n"
            @"受試者: %@ 第 %d 天 / 已校準: %@\n"
            @"最近一次校準: 有臉畫面 %.0f%% (%d/%d 幀) / 失敗統計: %@\n"
            @"相機權限: %@\n"
            @"最近 10 秒: %@\n"
            @"--- 目前偵測 ---\n%@",
            [fmt stringFromDate:[NSDate date]], model, [UIDevice currentDevice].systemVersion, appVersion,
            subject, day, calibrated ? @"是" : @"否",
            ratio * 100.0f, _calibFaceFrames, _calibFrames, counts.length > 0 ? counts : @"-",
            [self cameraAuthorizationText],
            [self recentDetectionSummary],
            [self diagnosticSummaryText]];
}

- (NSString *)cameraAuthorizationText {
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
        case AVAuthorizationStatusAuthorized:    return @"已允許";
        case AVAuthorizationStatusDenied:        return @"已拒絕";
        case AVAuthorizationStatusRestricted:    return @"受限制";
        case AVAuthorizationStatusNotDetermined: return @"尚未詢問";
    }
    return @"未知";
}

- (NSURL *)diagnosticsDirectory {
    NSURL *docs = [[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask] firstObject];
    NSURL *dir = [docs URLByAppendingPathComponent:@"EFD_Diagnostics" isDirectory:YES];
    [[NSFileManager defaultManager] createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:nil];
    return dir;
}

- (void)onExportDiagnosticFramesClicked {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"匯出診斷畫面"
                                                                   message:@"將連續擷取 5 張前鏡頭畫面（含你的臉部影像）與偵測數值，只存在這支手機的「檔案」App > EFD 資料夾，由你決定是否傳給研究人員。\n\n請按「開始」後把臉對準前鏡頭，約 3 秒後擷取。"
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"取消" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"開始" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        NSDateFormatter *fmt = [[NSDateFormatter alloc] init];
        fmt.dateFormat = @"yyyyMMdd_HHmmss";
        self.exportStamp = [fmt stringFromDate:[NSDate date]];
        self.exportedFiles = [NSMutableArray array];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            self->_exportFrameSkip = 0;
            self->_exportFramesRemaining.store(kDiagnosticExportFrames);
        });
    }]];
    [self presentViewController:alert animated:YES completion:nil];
}

// 背景佇列：寫出一張畫面 (PPM 供重播工具、PNG 供檢視) 與其偵測數值 (JSON)
- (void)writeDiagnosticFrame:(const efd::RawFrame&)frame
                  diagnostic:(const efd::FaceLandmarker::PresenceDiagnostic&)d
                       index:(int)index {
    NSURL *dir = [self diagnosticsDirectory];
    NSString *base = [NSString stringWithFormat:@"%@_%02d", self.exportStamp ?: @"efd", index];
    const int w = frame.width, h = frame.height;
    const bool isBgr = frame.format == efd::PixelFormat::BGR888;

    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0; i + 2 < rgb.size() && i + 2 < frame.data.size(); i += 3) {
        rgb[i]     = isBgr ? frame.data[i + 2] : frame.data[i];
        rgb[i + 1] = frame.data[i + 1];
        rgb[i + 2] = isBgr ? frame.data[i] : frame.data[i + 2];
    }

    NSMutableData *ppm = [NSMutableData dataWithData:[[NSString stringWithFormat:@"P6\n%d %d\n255\n", w, h] dataUsingEncoding:NSASCIIStringEncoding]];
    [ppm appendBytes:rgb.data() length:rgb.size()];
    NSURL *ppmURL = [dir URLByAppendingPathComponent:[base stringByAppendingString:@".ppm"]];
    [ppm writeToURL:ppmURL atomically:YES];

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CFDataRef cfData = CFDataCreate(NULL, rgb.data(), static_cast<CFIndex>(rgb.size()));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(cfData);
    CGImageRef cgImage = CGImageCreate(w, h, 8, 24, w * 3, space, kCGImageAlphaNone | kCGBitmapByteOrderDefault,
                                       provider, NULL, false, kCGRenderingIntentDefault);
    NSURL *pngURL = [dir URLByAppendingPathComponent:[base stringByAppendingString:@".png"]];
    if (cgImage) {
        [UIImagePNGRepresentation([UIImage imageWithCGImage:cgImage]) writeToURL:pngURL atomically:YES];
        CGImageRelease(cgImage);
    }
    CGDataProviderRelease(provider);
    CFRelease(cfData);
    CGColorSpaceRelease(space);

    NSDictionary *json = @{
        @"frame": @{@"width": @(w), @"height": @(h), @"synthetic": @(frame.isSynthetic)},
        @"result": @{@"personInFrame": @(d.rawPersonInFrame), @"facingCamera": @(d.rawFacingCamera),
                     @"failStage": [NSString stringWithUTF8String:d.failStage.c_str()],
                     @"reason": [NSString stringWithUTF8String:d.unconfirmedReason.c_str()] ?: @""},
        @"skinBox": @[@(d.skinBoxX), @(d.skinBoxY), @(d.skinBoxW), @(d.skinBoxH)],
        @"faceBox": @[@(d.faceBoxX), @(d.faceBoxY), @(d.faceBoxW), @(d.faceBoxH)],
        @"skinPixels": @(d.skinPixels), @"avgSkinLum": @(d.avgSkinLum),
        @"aspectRatio": @(d.aspectRatio), @"fillDensity": @(d.fillDensity), @"symmetry": @(d.symmetryRatio),
        @"eyes": @{@"centerY": @(d.eyeCenterY),
                   @"left": @{@"x": @(d.eyeCenterX[0]), @"dark": @(d.eyeDarkFraction[0]), @"contrast": @(d.eyeContrast[0]), @"gradient": @(d.eyeGradient[0])},
                   @"right": @{@"x": @(d.eyeCenterX[1]), @"dark": @(d.eyeDarkFraction[1]), @"contrast": @(d.eyeContrast[1]), @"gradient": @(d.eyeGradient[1])}}
    };
    NSData *jsonData = [NSJSONSerialization dataWithJSONObject:json options:NSJSONWritingPrettyPrinted error:nil];
    NSURL *jsonURL = [dir URLByAppendingPathComponent:[base stringByAppendingString:@".json"]];
    [jsonData writeToURL:jsonURL atomically:YES];

    dispatch_async(dispatch_get_main_queue(), ^{
        [self.exportedFiles addObjectsFromArray:@[pngURL, ppmURL, jsonURL]];
        if (index == kDiagnosticExportFrames) {
            [self finishDiagnosticExport];
        }
    });
}

- (void)finishDiagnosticExport {
    // 一併存一份錯誤回報文字，方便研究人員對照
    NSURL *reportURL = [[self diagnosticsDirectory] URLByAppendingPathComponent:
                        [NSString stringWithFormat:@"%@_report.txt", self.exportStamp ?: @"efd"]];
    [[self diagnosticReportText] writeToURL:reportURL atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [self.exportedFiles addObject:reportURL];

    UIActivityViewController *share = [[UIActivityViewController alloc] initWithActivityItems:self.exportedFiles applicationActivities:nil];
    share.popoverPresentationController.sourceView = self.view;
    share.popoverPresentationController.sourceRect = CGRectMake(self.view.bounds.size.width / 2, self.view.bounds.size.height / 2, 1, 1);
    [self presentViewController:share animated:YES completion:nil];
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
