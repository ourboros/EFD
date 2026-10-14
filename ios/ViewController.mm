#import "ViewController.h"
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
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

@interface ViewController () {
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
@property (nonatomic, strong) UILabel *fatigueStatusLabel;
@property (nonatomic, strong) UIView *cameraAspectBox;

// 校準階段元件
@property (nonatomic, strong) UIProgressView *calibProgressView;
@property (nonatomic, strong) UILabel *calibHintLabel;
@property (nonatomic, strong) NSTimer *calibTimer;
@property (nonatomic, assign) CGFloat calibElapsedSec;

// 設定項目狀態
@property (nonatomic, assign) BOOL highSensitivityEnabled;
@property (nonatomic, assign) BOOL soundAlertEnabled;
@property (nonatomic, assign) BOOL autoStartEnabled;

// 背景計時器 (定時科研同步與門禁檢查)
@property (nonatomic, strong) NSTimer *backgroundCheckTimer;

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = COLOR_PRIMARY_MINT;

    // 初始化狀態變數
    self.highSensitivityEnabled = NO;
    self.soundAlertEnabled = YES;
    self.autoStartEnabled = NO;
    self.currentStage = EFDAppStageWelcome;

    // 建立內容容器視圖
    self.contentContainer = [[UIView alloc] initWithFrame:self.view.bounds];
    self.contentContainer.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.contentContainer.backgroundColor = [UIColor clearColor];
    [self.view addSubview:self.contentContainer];

    // 初始化 C++20 EFD 跨平台五執行緒非同步管線引擎
    _engine = new efd::AsyncPipelineEngine(efd::PlatformType::iOS);

    // 檢查是否已達到第 14 天後測門禁鎖定
    if (_engine->getStudyTracker().getStatus() == efd::StudyStatus::LockedForPostTest) {
        self.currentStage = EFDAppStageStudyGate;
    }

    // 綁定遙測數據回呼
    _engine->setTelemetryCallback([self](const efd::EngineTelemetry& t) {
        self->_latestTelemetry = t;
        self->_hasTelemetry = YES;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateTelemetryUI:t];
        });
    });

    // 綁定警報通知回呼 (疲勞超標時跳出警報與震動提示)
    _engine->setAlertCallback([self](efd::FatigueLevel level, float score, const std::string& msg) {
        (void)msg;
        if (level == efd::FatigueLevel::SevereWarning || score >= 70.0f) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self triggerFatigueAlertNotification:score];
            });
        }
    });

    // 啟動管線引擎
    _engine->start();

    // 啟動定時檢查定時器 (每 30 秒檢查一次夜間 21:00 科研同步與門禁狀態)
    self.backgroundCheckTimer = [NSTimer scheduledTimerWithTimeInterval:30.0
                                                                 target:self
                                                               selector:@selector(onBackgroundCheckTick)
                                                               userInfo:nil
                                                                repeats:YES];

    // 渲染當前階段畫面
    [self renderCurrentStage];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    self.contentContainer.frame = self.view.bounds;
}

- (void)dealloc {
    [self.backgroundCheckTimer invalidate];
    [self.calibTimer invalidate];
    if (_engine) {
        _engine->stop();
        delete _engine;
        _engine = nullptr;
    }
}

// -----------------------------------------------------------------------------
// 背景定時檢查 (21:00 夜間科研同步 & 第 14 天後測門禁)
// -----------------------------------------------------------------------------
- (void)onBackgroundCheckTick {
    if (!_engine) return;

    // 觸發夜間 21:00 定時同步檢查
    _engine->checkAndTriggerNightlySync();

    // 檢查是否跨日解鎖到達第 14 天後測門禁
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
    // 清空現有子視圖
    for (UIView *subview in self.contentContainer.subviews) {
        [subview removeFromSuperview];
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
// 階段 1：歡迎首頁 (滿版薄荷綠 #1EB18A + 標題放大 20% + 白底圓角按鈕)
// -----------------------------------------------------------------------------
- (void)renderWelcomeScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;

    // 1. 頂部裝飾圓圈 (資產 Logo 意象)
    UIView *badge = [[UIView alloc] initWithFrame:CGRectMake((w - 110) / 2, h * 0.18, 110, 110)];
    badge.backgroundColor = COLOR_CARD_WHITE;
    badge.layer.cornerRadius = 55;
    badge.layer.borderColor = [COLOR_WARM_AMBER CGColor];
    badge.layer.borderWidth = 3.0;

    UIImageView *iconView = [[UIImageView alloc] initWithFrame:CGRectMake(25, 25, 60, 60)];
    iconView.image = [UIImage systemImageNamed:@"eye.circle.fill"];
    iconView.tintColor = COLOR_PRIMARY_MINT;
    [badge addSubview:iconView];
    [self.contentContainer addSubview:badge];

    // 2. 主標題 (放大 20%: 24pt -> 29pt)
    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, CGRectGetMaxY(badge.frame) + 24, w - 40, 44)];
    title.text = @"感謝協助測試EFD";
    title.textColor = COLOR_CARD_WHITE;
    title.font = [UIFont boldSystemFontOfSize:29];
    title.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:title];

    // 副標題 (金黃暖色 #F7E3AF)
    UILabel *subTitle = [[UILabel alloc] initWithFrame:CGRectMake(20, CGRectGetMaxY(title.frame) + 6, w - 40, 26)];
    subTitle.text = @"AI 驅動眼睛特徵提取與即時疲勞監控研究系統";
    subTitle.textColor = COLOR_WARM_AMBER;
    subTitle.font = [UIFont boldSystemFontOfSize:13];
    subTitle.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:subTitle];

    // 3. 進入測試說明按鈕 (白底綠字，20px 圓角)
    UIButton *enterBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    enterBtn.frame = CGRectMake(40, h - 140, w - 80, 52);
    [enterBtn setTitle:@"進入測試說明" forState:UIControlStateNormal];
    [enterBtn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
    enterBtn.backgroundColor = COLOR_CARD_WHITE;
    enterBtn.layer.cornerRadius = 20;
    enterBtn.titleLabel.font = [UIFont boldSystemFontOfSize:17];
    [enterBtn addTarget:self action:@selector(onEnterInstructionClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:enterBtn];
}

- (void)onEnterInstructionClicked {
    [self switchToStage:EFDAppStageInstruction];
}

// -----------------------------------------------------------------------------
// 階段 2：測驗指引說明介面 (3 大指導卡片 + 準備開始)
// -----------------------------------------------------------------------------
- (void)renderInstructionScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;

    // 標題 (放大 20%: 22pt -> 26pt)
    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, 54, w - 40, 36)];
    title.text = @"眼動特徵提取測驗說明";
    title.textColor = COLOR_CARD_WHITE;
    title.font = [UIFont boldSystemFontOfSize:26];
    title.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:title];

    // 指引卡片清單
    NSArray *cards = @[
        @{@"t": @"臉部正面對齊鏡頭", @"d": @"保持端正坐姿，確保前置相機清晰捕捉雙眼面部特徵。"},
        @{@"t": @"視線專注平視螢幕", @"d": @"校準開始後請專注凝視螢幕中央指示標記。"},
        @{@"t": @"保持自然眨眼狀態", @"d": @"採樣過程僅需 5 秒鐘，系統將自適應計算個人化基準 EAR。"}
    ];

    CGFloat startY = 106;
    CGFloat cardH = 80;
    CGFloat gap = 12;

    for (NSInteger i = 0; i < cards.count; ++i) {
        UIView *card = [[UIView alloc] initWithFrame:CGRectMake(24, startY + i * (cardH + gap), w - 48, cardH)];
        card.backgroundColor = [UIColor colorWithRed:20/255.0 green:140/255.0 blue:108/255.0 alpha:0.8];
        card.layer.cornerRadius = 14;

        UILabel *cTitle = [[UILabel alloc] initWithFrame:CGRectMake(16, 12, card.frame.size.width - 32, 22)];
        cTitle.text = cards[i][@"t"];
        cTitle.textColor = COLOR_WARM_AMBER;
        cTitle.font = [UIFont boldSystemFontOfSize:15];
        [card addSubview:cTitle];

        UILabel *cDesc = [[UILabel alloc] initWithFrame:CGRectMake(16, 36, card.frame.size.width - 32, 36)];
        cDesc.text = cards[i][@"d"];
        cDesc.textColor = COLOR_CARD_WHITE;
        cDesc.font = [UIFont systemFontOfSize:12];
        cDesc.numberOfLines = 2;
        [card addSubview:cDesc];

        [self.contentContainer addSubview:card];
    }

    // 底部按鈕群
    UIButton *readyBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    readyBtn.frame = CGRectMake(40, h - 145, w - 80, 50);
    [readyBtn setTitle:@"我準備好了，開始校準" forState:UIControlStateNormal];
    [readyBtn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
    readyBtn.backgroundColor = COLOR_CARD_WHITE;
    readyBtn.layer.cornerRadius = 20;
    readyBtn.titleLabel.font = [UIFont boldSystemFontOfSize:16];
    [readyBtn addTarget:self action:@selector(onStartCalibClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:readyBtn];

    UIButton *backBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    backBtn.frame = CGRectMake(40, h - 85, w - 80, 42);
    [backBtn setTitle:@"返回歡迎介面" forState:UIControlStateNormal];
    [backBtn setTitleColor:COLOR_CARD_WHITE forState:UIControlStateNormal];
    backBtn.backgroundColor = [UIColor colorWithRed:20/255.0 green:120/255.0 blue:90/255.0 alpha:0.6];
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
// 階段 3：實際眼動特徵校準 (5 秒進度條與視線採樣)
// -----------------------------------------------------------------------------
- (void)renderCalibrationScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;

    // 標題 (放大 20%: 22pt -> 26pt)
    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, 54, w - 40, 36)];
    title.text = @"眼動特徵基準校準中...";
    title.textColor = COLOR_CARD_WHITE;
    title.font = [UIFont boldSystemFontOfSize:26];
    title.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:title];

    // 動態黃點視覺目標
    UIView *dotView = [[UIView alloc] initWithFrame:CGRectMake((w - 50) / 2, h * 0.35, 50, 50)];
    dotView.backgroundColor = COLOR_WARM_AMBER;
    dotView.layer.cornerRadius = 25;
    dotView.layer.borderColor = [[COLOR_ACCENT_BLUE colorWithAlphaComponent:0.6] CGColor];
    dotView.layer.borderWidth = 4.0;
    [self.contentContainer addSubview:dotView];

    // 提示說明
    self.calibHintLabel = [[UILabel alloc] initWithFrame:CGRectMake(20, CGRectGetMaxY(dotView.frame) + 36, w - 40, 30)];
    self.calibHintLabel.text = @"請專注凝視黃色標記，保持臉部穩定...";
    self.calibHintLabel.textColor = COLOR_CARD_WHITE;
    self.calibHintLabel.font = [UIFont boldSystemFontOfSize:14];
    self.calibHintLabel.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:self.calibHintLabel];

    // 金色進度條
    self.calibProgressView = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
    self.calibProgressView.frame = CGRectMake(40, h - 160, w - 80, 10);
    self.calibProgressView.progressTintColor = COLOR_WARM_AMBER;
    self.calibProgressView.trackTintColor = [UIColor colorWithRed:20/255.0 green:110/255.0 blue:85/255.0 alpha:0.8];
    self.calibProgressView.progress = 0.0f;
    [self.contentContainer addSubview:self.calibProgressView];

    // 啟動 5 秒校準計時器
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

        // 呼叫 C++ 引擎校準
        if (_engine) {
            _engine->calibrate(3.0f);
        }

        // 校準完成，進入即時監控中心
        [self switchToStage:EFDAppStageDashboard];
    }
}

// -----------------------------------------------------------------------------
// 階段 4：即時眼睛疲勞監控中心 (Dashboard)
// -----------------------------------------------------------------------------
- (void)renderDashboardScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;

    // 1. 頂部標題列 (文字大小放大 20%: 20pt -> 24pt)
    UILabel *headerTitle = [[UILabel alloc] initWithFrame:CGRectMake(16, 48, w - 32, 34)];
    headerTitle.text = @"EFD 即時眼睛疲勞監控中心";
    headerTitle.textColor = COLOR_CARD_WHITE;
    headerTitle.font = [UIFont boldSystemFontOfSize:24];
    headerTitle.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:headerTitle];

    // 2. 攝影機預覽框 (純白底色，嚴格依相機比例 4:3 顯示)
    CGFloat camBoxW = w - 40;
    CGFloat camBoxH = camBoxW * (3.0f / 4.0f); // 4:3 比例
    if (camBoxH > h * 0.32f) {
        camBoxH = h * 0.32f;
        camBoxW = camBoxH * (4.0f / 3.0f);
    }
    CGFloat camBoxX = (w - camBoxW) / 2;
    CGFloat camBoxY = CGRectGetMaxY(headerTitle.frame) + 12;

    self.cameraAspectBox = [[UIView alloc] initWithFrame:CGRectMake(camBoxX, camBoxY, camBoxW, camBoxH)];
    self.cameraAspectBox.backgroundColor = COLOR_CARD_WHITE;
    self.cameraAspectBox.layer.cornerRadius = 14;
    self.cameraAspectBox.clipsToBounds = YES;

    // 攝影機預覽內部狀態標籤
    UILabel *camTag = [[UILabel alloc] initWithFrame:CGRectMake(12, 10, camBoxW - 24, 20)];
    camTag.text = @"📷 前置攝影機畫面 (4:3 比例鎖定)";
    camTag.textColor = COLOR_TEXT_DARK;
    camTag.font = [UIFont boldSystemFontOfSize:11];
    [self.cameraAspectBox addSubview:camTag];

    UIImageView *faceSilhouette = [[UIImageView alloc] initWithFrame:CGRectMake((camBoxW - 60) / 2, (camBoxH - 60) / 2, 60, 60)];
    faceSilhouette.image = [UIImage systemImageNamed:@"face.dashed"];
    faceSilhouette.tintColor = [COLOR_PRIMARY_MINT colorWithAlphaComponent:0.4];
    [self.cameraAspectBox addSubview:faceSilhouette];

    [self.contentContainer addSubview:self.cameraAspectBox];

    // 3. 4 大監控指標卡片 (純白背景 #FFFFFF，深黑文字 #25291C，只顯示核心四項)
    CGFloat metricsY = CGRectGetMaxY(self.cameraAspectBox.frame) + 12;
    CGFloat cardGap = 8;
    CGFloat metricCardW = (w - 40 - cardGap) / 2;
    CGFloat metricCardH = (h - metricsY - 180) / 2;
    if (metricCardH < 56) metricCardH = 56;
    if (metricCardH > 72) metricCardH = 72;

    auto createMetricCard = ^(CGRect frame, NSString* title, UILabel** outValLabel, NSString* defaultVal, NSString* unit) {
        UIView *card = [[UIView alloc] initWithFrame:frame];
        card.backgroundColor = COLOR_CARD_WHITE;
        card.layer.cornerRadius = 10;

        UILabel *tLabel = [[UILabel alloc] initWithFrame:CGRectMake(10, 6, frame.size.width - 20, 16)];
        tLabel.text = title;
        tLabel.textColor = [COLOR_TEXT_DARK colorWithAlphaComponent:0.7];
        tLabel.font = [UIFont boldSystemFontOfSize:11];
        [card addSubview:tLabel];

        UILabel *vLabel = [[UILabel alloc] initWithFrame:CGRectMake(10, 24, frame.size.width - 20, frame.size.height - 28)];
        vLabel.text = defaultVal;
        vLabel.textColor = COLOR_TEXT_DARK;
        vLabel.font = [UIFont boldSystemFontOfSize:18];
        [card addSubview:vLabel];
        *outValLabel = vLabel;

        (void)unit;
        return card;
    };

    // 卡片 1: 雙眼 EAR
    UIView *card1 = createMetricCard(
        CGRectMake(20, metricsY, metricCardW, metricCardH),
        @"雙眼 EAR",
        &_earValueLabel,
        @"0.312",
        @"常態"
    );
    [self.contentContainer addSubview:card1];

    // 卡片 2: 閉眼比 (PERCLOS)
    UIView *card2 = createMetricCard(
        CGRectMake(20 + metricCardW + cardGap, metricsY, metricCardW, metricCardH),
        @"閉眼比 (PERCLOS)",
        &_perclosValueLabel,
        @"12.5%",
        @"%"
    );
    [self.contentContainer addSubview:card2];

    // 卡片 3: 複雜度 (MSE / CI)
    UIView *card3 = createMetricCard(
        CGRectMake(20, metricsY + metricCardH + cardGap, metricCardW, metricCardH),
        @"複雜度 (MSE / CI)",
        &_mseValueLabel,
        @"1.85",
        @"指數"
    );
    [self.contentContainer addSubview:card3];

    // 卡片 4: 綜合疲勞分數
    UIView *card4 = createMetricCard(
        CGRectMake(20 + metricCardW + cardGap, metricsY + metricCardH + cardGap, metricCardW, metricCardH),
        @"綜合疲勞分數",
        &_fatigueScoreLabel,
        @"24.0",
        @"/ 100"
    );
    [self.contentContainer addSubview:card4];

    // 4. 底部三大控制按鈕群
    CGFloat btnAreaY = metricsY + (metricCardH * 2) + (cardGap * 2) + 10;
    CGFloat btnH = 44;

    // 按鈕 1: 重新偵測疲勞數據 (白底綠字，按下一鍵回到說明介面)
    UIButton *recalibBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    recalibBtn.frame = CGRectMake(20, btnAreaY, w - 40, btnH);
    [recalibBtn setTitle:@"重新偵測疲勞數據" forState:UIControlStateNormal];
    [recalibBtn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
    recalibBtn.backgroundColor = COLOR_CARD_WHITE;
    recalibBtn.layer.cornerRadius = 16;
    recalibBtn.titleLabel.font = [UIFont boldSystemFontOfSize:15];
    [recalibBtn addTarget:self action:@selector(onRecalibrateDataClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:recalibBtn];

    // 按鈕 2: 進入設定介面 (白底綠字)
    UIButton *settingsBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    settingsBtn.frame = CGRectMake(20, btnAreaY + btnH + 8, (w - 48) / 2, btnH);
    [settingsBtn setTitle:@"系統參數設定" forState:UIControlStateNormal];
    [settingsBtn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
    settingsBtn.backgroundColor = COLOR_CARD_WHITE;
    settingsBtn.layer.cornerRadius = 16;
    settingsBtn.titleLabel.font = [UIFont boldSystemFontOfSize:14];
    [settingsBtn addTarget:self action:@selector(onOpenSettingsClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:settingsBtn];

    // 按鈕 3: 關閉系統介面 / 背景運行 (醒目珊瑚紅 #EB5757 白字)
    UIButton *closeBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    closeBtn.frame = CGRectMake(CGRectGetMaxX(settingsBtn.frame) + 8, btnAreaY + btnH + 8, (w - 48) / 2, btnH);
    [closeBtn setTitle:@"背景運行模式" forState:UIControlStateNormal];
    [closeBtn setTitleColor:COLOR_CARD_WHITE forState:UIControlStateNormal];
    closeBtn.backgroundColor = COLOR_ALERT_CORAL;
    closeBtn.layer.cornerRadius = 16;
    closeBtn.titleLabel.font = [UIFont boldSystemFontOfSize:14];
    [closeBtn addTarget:self action:@selector(onMinimizeToBackgroundClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:closeBtn];

    // 若已有遙測數據，立即刷新標籤
    if (_hasTelemetry) {
        [self updateTelemetryUI:_latestTelemetry];
    }
}

- (void)onRecalibrateDataClicked {
    // 使用者指示：按下即可回到介紹介面，重新開始檢測流程
    [self switchToStage:EFDAppStageInstruction];
}

- (void)onOpenSettingsClicked {
    [self switchToStage:EFDAppStageSettings];
}

- (void)onMinimizeToBackgroundClicked {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"背景運行中"
                                                                   message:@"EFD 守護程式已切換至背景待命模式，將持續進行眼睛疲勞守護。"
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"確定" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

// -----------------------------------------------------------------------------
// 階段 5：系統設定介面 (白底綠字卡片 + Toggle Switch + 標題放大 20%)
// -----------------------------------------------------------------------------
- (void)renderSettingsScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;

    // 標題 (放大 20%: 22pt -> 26pt)
    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, 50, w - 40, 36)];
    title.text = @"系統參數與偏好設定";
    title.textColor = COLOR_CARD_WHITE;
    title.font = [UIFont boldSystemFontOfSize:26];
    title.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:title];

    // 項目清單
    CGFloat startY = 104;
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

        [self.contentContainer addSubview:card];
    };

    // 1. 高靈敏度疲勞偵測
    createSettingRow(startY, @"高靈敏度疲勞偵測", @"縮短判定窗口 (提升至 3 分鐘快篩頻率)", self.highSensitivityEnabled, @selector(onHighSensitivityChanged:), NO);

    // 2. 疲勞警報提示音
    createSettingRow(startY + (itemH + gap), @"疲勞警報提示音", @"疲勞超標時發出柔和提示聲響", self.soundAlertEnabled, @selector(onSoundAlertChanged:), NO);

    // 3. 開機自動背景啟動
    createSettingRow(startY + (itemH + gap) * 2, @"開機自動背景啟動", @"裝置啟動後自動於背景待命守護", self.autoStartEnabled, @selector(onAutoStartChanged:), NO);

    // 4. 自動科研同步 (鎖定開啟，不能關閉)
    createSettingRow(startY + (itemH + gap) * 3, @"自動科研同步", @"系統預設恆定啟用 (每晚 21:00 自動上傳)", YES, nil, YES);

    // 返回監控中心按鈕 (白底綠字，無外框)
    UIButton *backBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    backBtn.frame = CGRectMake(40, h - 90, w - 80, 50);
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
// 階段 6：第 14 天後測門禁解鎖介面 (自動跳轉 Google 表單問卷)
// -----------------------------------------------------------------------------
- (void)renderStudyGateScreen {
    CGFloat w = self.contentContainer.bounds.size.width;
    CGFloat h = self.contentContainer.bounds.size.height;

    // 標題 (放大 20%: 22pt -> 26pt)
    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, 54, w - 40, 36)];
    title.text = @"14 天研究實驗完成 (後測介面)";
    title.textColor = COLOR_CARD_WHITE;
    title.font = [UIFont boldSystemFontOfSize:26];
    title.textAlignment = NSTextAlignmentCenter;
    [self.contentContainer addSubview:title];

    // 後測提示卡片
    UIView *card = [[UIView alloc] initWithFrame:CGRectMake(24, 110, w - 48, 160)];
    card.backgroundColor = [UIColor colorWithRed:20/255.0 green:140/255.0 blue:108/255.0 alpha:0.8];
    card.layer.cornerRadius = 16;

    UILabel *cTitle = [[UILabel alloc] initWithFrame:CGRectMake(16, 14, card.frame.size.width - 32, 24)];
    cTitle.text = @"受試者 14 天時序已安全封存";
    cTitle.textColor = COLOR_WARM_AMBER;
    cTitle.font = [UIFont boldSystemFontOfSize:16];
    [card addSubview:cTitle];

    UILabel *cDesc = [[UILabel alloc] initWithFrame:CGRectMake(16, 44, card.frame.size.width - 32, 100)];
    cDesc.text = @"雙眼特徵時序記錄已全數完成！\n本機資料庫已完成落盤簽章校驗。\n請點擊下方按鈕前往填寫後測問卷以完成整體科研流程。\n問卷網址：https://forms.gle/y5f1jTnrrtsxz65G9";
    cDesc.textColor = COLOR_CARD_WHITE;
    cDesc.font = [UIFont systemFontOfSize:13];
    cDesc.numberOfLines = 5;
    [card addSubview:cDesc];

    [self.contentContainer addSubview:card];

    // 前往問卷按鈕
    UIButton *formBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    formBtn.frame = CGRectMake(36, h - 145, w - 72, 50);
    [formBtn setTitle:@"前往填寫後測問卷 (Google 表單)" forState:UIControlStateNormal];
    [formBtn setTitleColor:COLOR_PRIMARY_MINT forState:UIControlStateNormal];
    formBtn.backgroundColor = COLOR_CARD_WHITE;
    formBtn.layer.cornerRadius = 20;
    formBtn.titleLabel.font = [UIFont boldSystemFontOfSize:16];
    [formBtn addTarget:self action:@selector(onOpenPostSurveyFormClicked) forControlEvents:UIControlEventTouchUpInside];
    [self.contentContainer addSubview:formBtn];

    // 返回監控中心按鈕
    UIButton *backBtn = [UIButton buttonWithType:UIButtonTypeSystem];
    backBtn.frame = CGRectMake(36, h - 85, w - 72, 42);
    [backBtn setTitle:@"返回監控中心" forState:UIControlStateNormal];
    [backBtn setTitleColor:COLOR_CARD_WHITE forState:UIControlStateNormal];
    backBtn.backgroundColor = [UIColor colorWithRed:20/255.0 green:120/255.0 blue:90/255.0 alpha:0.6];
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
// 即時遙測數據刷新
// -----------------------------------------------------------------------------
- (void)updateTelemetryUI:(const efd::EngineTelemetry&)t {
    if (self.currentStage != EFDAppStageDashboard) return;

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
        self.fatigueScoreLabel.text = [NSString stringWithFormat:@"%.1f", t.systemState.currentFatigueScore];
    }
}

// -----------------------------------------------------------------------------
// 疲勞超標警報震動與通知發送
// -----------------------------------------------------------------------------
- (void)triggerFatigueAlertNotification:(float)score {
    // 1. Taptic Engine 觸覺震動提醒
    UINotificationFeedbackGenerator *feedback = [[UINotificationFeedbackGenerator alloc] init];
    [feedback notificationOccurred:UINotificationFeedbackTypeWarning];

    // 2. 警報聲音提示 (若啟用)
    if (self.soundAlertEnabled) {
        AudioServicesPlaySystemSound(1005);
    }

    // 3. 畫面彈出醒目疲勞提醒
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"🚨 眼睛疲勞提醒"
                                                                   message:[NSString stringWithFormat:@"疲勞綜合評分已達 %.1f 分！\n你的眼睛處於疲勞狀態，請適當休息放鬆 5 分鐘。", score]
                                                            preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"我知道了，閉眼休息" style:UIAlertActionStyleDestructive handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

@end
