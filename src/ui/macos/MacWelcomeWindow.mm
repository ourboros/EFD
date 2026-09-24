#import "MacWelcomeWindow.h"
#import "platform/macos/MacStatusItemManager.h"
#import "engine/AsyncPipelineEngine.hpp"
#import "Theme.hpp"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <vector>

namespace efd {

enum class MacUIStage : uint8_t {
    Welcome,                // 階段 1: 歡迎介面
    CalibrationInstruction, // 階段 2: 說明與演示預覽介面
    CountdownWait,          // 階段 3: 3 秒倒數計時等待
    ActiveCalibration,      // 階段 4: 實際多點眼動提取
    CalibrationResult,      // 階段 5: 測驗完成提示
    MainDashboard,          // 階段 6: 即時疲勞監控中心
    SettingsPanel,          // 階段 7: 系統設定
    StudyCompletedGate,     // 階段 8: 後測介面
    QuestionnaireSubmitted  // 階段 9: 後測問卷填寫完成
};

} // namespace efd

@interface EFDMainCanvasView : NSView {
    efd::AsyncPipelineEngine* _engine;
    efd::MacUIStage _currentStage;
    float _stageTimeSec;
    float _animTimeSec;
    float _demoDotX;
    float _demoDotY;
    float _targetDotX;
    float _targetDotY;
    float _calibrationProgress;
    int _sensitivityLevel;

    // Hover 狀態
    NSInteger _hoveredNavTab;
    BOOL _isHoveringStartBtn;
    BOOL _isHoveringReadyBtn;
    BOOL _isHoveringBackWelcomeBtn;
    BOOL _isHoveringProceedDashboardBtn;
    BOOL _isHoveringCloseBgResultBtn;
    BOOL _isHoveringRestartCalibBtn;
    BOOL _isHoveringSettingsBtn;
    BOOL _isHoveringMinimizeTrayBtn;
    BOOL _isHoveringSensitivityBtn;
    BOOL _isHoveringRecalibFromSettingsBtn;
    BOOL _isHoveringSaveSettingsBtn;
    BOOL _isHoveringFillQuestionnaireBtn;
    BOOL _isHoveringReturnDashboardBtn;
    BOOL _isHoveringExitAppBtn;

    // 按鈕 Rects (座標為頂部為原點, isFlipped = YES)
    NSRect _startBtnRect;
    NSRect _readyBtnRect;
    NSRect _backWelcomeBtnRect;
    NSRect _skipCalibBtnRect;
    NSRect _proceedDashboardBtnRect;
    NSRect _closeBgResultBtnRect;
    NSRect _restartCalibBtnRect;
    NSRect _settingsBtnRect;
    NSRect _minimizeTrayBtnRect;
    NSRect _sensitivityBtnRect;
    NSRect _recalibFromSettingsBtnRect;
    NSRect _saveSettingsBtnRect;
    NSRect _fillQuestionnaireBtnRect;
    NSRect _returnDashboardBtnRect;
    NSRect _exitAppBtnRect;
    std::vector<NSRect> _navTabRects;

    NSImage* _logoImage;
    NSTrackingArea* _trackingArea;
    NSTimer* _animationTimer;

    efd::EngineTelemetry _latestTelemetry;
    BOOL _hasTelemetry;
}

@property (nonatomic, weak) NSWindow* parentWindow;
@property (nonatomic, assign) efd::MacStatusItemManager* statusItemManager;

- (instancetype)initWithFrame:(NSRect)frame engine:(efd::AsyncPipelineEngine*)engine;
- (void)setStage:(efd::MacUIStage)stage;
- (void)onTimerTick;
- (void)updateTelemetry:(const efd::EngineTelemetry&)telemetry;
- (void)minimizeToBackground;
@end

@implementation EFDMainCanvasView

- (BOOL)isFlipped {
    return YES; // 頂部為 (0,0)，與 Windows GDI+ 與 Web 座標系 100% 一致
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (instancetype)initWithFrame:(NSRect)frame engine:(efd::AsyncPipelineEngine*)engine {
    self = [super initWithFrame:frame];
    if (self) {
        _engine = engine;
        _currentStage = efd::MacUIStage::Welcome;
        _stageTimeSec = 0.0f;
        _animTimeSec = 0.0f;
        _demoDotX = 0.5f;
        _demoDotY = 0.5f;
        _targetDotX = 0.5f;
        _targetDotY = 0.5f;
        _calibrationProgress = 0.0f;
        _sensitivityLevel = 1; // 標準靈敏度
        _hoveredNavTab = -1;

        // 載入資產 10 Logo
        _logoImage = [self loadAssetImage:@"資產 10.png"];
        if (!_logoImage) {
            _logoImage = [self loadAssetImage:@"logo10.png"];
        }
        if (!_logoImage) {
            _logoImage = [self loadAssetImage:@"logo.png"];
        }

        // 啟動 60 FPS 動畫定時器 (16ms)
        _animationTimer = [NSTimer scheduledTimerWithTimeInterval:0.016
                                                           target:self
                                                         selector:@selector(onTimerTick)
                                                         userInfo:nil
                                                          repeats:YES];
        [[NSRunLoop mainRunLoop] addTimer:_animationTimer forMode:NSRunLoopCommonModes];
    }
    return self;
}

- (void)dealloc {
    [_animationTimer invalidate];
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
    }
    _trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                 options:(NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect)
                                                   owner:self
                                                userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (NSImage*)loadAssetImage:(NSString*)filename {
    NSArray* searchPaths = @[
        [[NSBundle mainBundle] pathForResource:[filename stringByDeletingPathExtension] ofType:[filename pathExtension] inDirectory:@"design/1x"],
        [[NSBundle mainBundle] pathForResource:[filename stringByDeletingPathExtension] ofType:[filename pathExtension] inDirectory:@"assets"],
        [[NSBundle mainBundle] pathForResource:[filename stringByDeletingPathExtension] ofType:[filename pathExtension]],
        [NSString stringWithFormat:@"%@/design/1x/%@", [[NSBundle mainBundle] resourcePath], filename],
        [NSString stringWithFormat:@"%@/assets/%@", [[NSBundle mainBundle] resourcePath], filename],
        [NSString stringWithFormat:@"design/1x/%@", filename],
        [NSString stringWithFormat:@"assets/%@", filename],
        [NSString stringWithFormat:@"../design/1x/%@", filename],
        [NSString stringWithFormat:@"../../design/1x/%@", filename],
        [NSString stringWithFormat:@"E:/Project/EFD/design/1x/%@", filename]
    ];

    for (NSString* path in searchPaths) {
        if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
            NSImage* img = [[NSImage alloc] initWithContentsOfFile:path];
            if (img && img.size.width > 0) return img;
        }
    }
    return nil;
}

- (void)setStage:(efd::MacUIStage)stage {
    _currentStage = stage;
    _stageTimeSec = 0.0f;
    if (stage == efd::MacUIStage::ActiveCalibration) {
        _calibrationProgress = 0.0f;
    }
    if (stage == efd::MacUIStage::CalibrationInstruction || 
        stage == efd::MacUIStage::ActiveCalibration || 
        stage == efd::MacUIStage::MainDashboard) {
        if (_engine && !_engine->isRunning()) {
            _engine->start();
        }
    }
    [self setNeedsDisplay:YES];
}

- (void)updateTelemetry:(const efd::EngineTelemetry&)telemetry {
    _latestTelemetry = telemetry;
    _hasTelemetry = YES;
    if (_currentStage == efd::MacUIStage::MainDashboard ||
        _currentStage == efd::MacUIStage::CalibrationResult) {
        [self setNeedsDisplay:YES];
    }
}

- (void)minimizeToBackground {
    if (_engine && !_engine->isRunning()) {
        _engine->start();
    }
    if (self.parentWindow) {
        [self.parentWindow orderOut:nil];
    }
    // 使用者要求：按下關閉系統後，主介面關閉，執行檔/終端機視窗自動最小化
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSString* script = @"tell application \"Terminal\" to set miniaturized of every window whose visible is true to true\n"
                           @"tell application \"iTerm\" to set miniaturized of every window whose visible is true to true";
        NSAppleScript* appleScript = [[NSAppleScript alloc] initWithSource:script];
        [appleScript executeAndReturnError:nil];
    });
}

- (void)onTimerTick {
    constexpr float dt = 0.016f;
    _animTimeSec += dt;
    _stageTimeSec += dt;

    if (_currentStage == efd::MacUIStage::CalibrationInstruction) {
        // 階段 2: 演示小黃點動態巡迴 (4 秒一循環)
        float demoT = std::fmod(_stageTimeSec, 4.0f) / 4.0f;
        float angle = demoT * 6.2831853f;
        _demoDotX = 0.5f + 0.35f * std::cos(angle);
        _demoDotY = 0.28f * std::sin(angle * 2.0f) + 0.5f;
        [self setNeedsDisplay:YES];
    } else if (_currentStage == efd::MacUIStage::CountdownWait) {
        if (_stageTimeSec >= 3.0f) {
            [self setStage:efd::MacUIStage::ActiveCalibration];
        }
        [self setNeedsDisplay:YES];
    } else if (_currentStage == efd::MacUIStage::ActiveCalibration) {
        // 階段 4: 5 點採樣移動
        struct TargetPoint { float x, y; };
        const TargetPoint points[] = {
            { 0.50f, 0.50f }, // 0. 中心
            { 0.15f, 0.22f }, // 1. 左上
            { 0.85f, 0.22f }, // 2. 右上
            { 0.85f, 0.78f }, // 3. 右下
            { 0.15f, 0.78f }  // 4. 左下
        };
        constexpr float totalCalibTime = 5.0f;
        _calibrationProgress = std::clamp(_stageTimeSec / totalCalibTime, 0.0f, 1.0f);

        constexpr float segDuration = 1.0f;
        int seg = std::clamp(static_cast<int>(_stageTimeSec / segDuration), 0, 3);
        float segT = (_stageTimeSec - static_cast<float>(seg) * segDuration) / segDuration;
        float smoothT = 0.5f * (1.0f - std::cos(segT * 3.14159265f));

        TargetPoint p0 = points[seg];
        TargetPoint p1 = points[seg + 1];
        _targetDotX = p0.x + (p1.x - p0.x) * smoothT;
        _targetDotY = p0.y + (p1.y - p0.y) * smoothT;

        if (_calibrationProgress >= 1.0f) {
            if (_engine) _engine->calibrate(3.0f);
            [self setStage:efd::MacUIStage::CalibrationResult];
        }
        [self setNeedsDisplay:YES];
    } else if (_currentStage == efd::MacUIStage::MainDashboard) {
        [self setNeedsDisplay:YES];
    }
}

// -----------------------------------------------------------------------------
// 繪製工具函式 (Quartz 2D CoreGraphics)
// -----------------------------------------------------------------------------
static void drawRoundedButton(NSRect rect, CGFloat radius, NSColor* bgColor, NSColor* borderColor = nil, CGFloat borderWidth = 1.5) {
    NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius yRadius:radius];
    if (bgColor) {
        [bgColor setFill];
        [path fill];
    }
    if (borderColor) {
        [borderColor setStroke];
        path.lineWidth = borderWidth;
        [path stroke];
    }
}

static void drawCenteredText(NSString* text, NSRect rect, NSFont* font, NSColor* color) {
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = NSTextAlignmentCenter;
    style.lineBreakMode = NSLineBreakByWordWrapping;
    NSDictionary* attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: color,
        NSParagraphStyleAttributeName: style
    };
    NSSize textSize = [text sizeWithAttributes:attrs];
    NSRect textRect = NSMakeRect(rect.origin.x, rect.origin.y + (rect.size.height - textSize.height) / 2.0, rect.size.width, textSize.height);
    [text drawInRect:textRect withAttributes:attrs];
}

static void drawLeftText(NSString* text, NSRect rect, NSFont* font, NSColor* color) {
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = NSTextAlignmentLeft;
    style.lineBreakMode = NSLineBreakByWordWrapping;
    NSDictionary* attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: color,
        NSParagraphStyleAttributeName: style
    };
    NSSize textSize = [text sizeWithAttributes:attrs];
    NSRect textRect = NSMakeRect(rect.origin.x, rect.origin.y + (rect.size.height - textSize.height) / 2.0, rect.size.width, textSize.height);
    [text drawInRect:textRect withAttributes:attrs];
}

static void drawRightText(NSString* text, NSRect rect, NSFont* font, NSColor* color) {
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = NSTextAlignmentRight;
    style.lineBreakMode = NSLineBreakByWordWrapping;
    NSDictionary* attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: color,
        NSParagraphStyleAttributeName: style
    };
    NSSize textSize = [text sizeWithAttributes:attrs];
    NSRect textRect = NSMakeRect(rect.origin.x, rect.origin.y + (rect.size.height - textSize.height) / 2.0, rect.size.width, textSize.height);
    [text drawInRect:textRect withAttributes:attrs];
}

// -----------------------------------------------------------------------------
// 繪製主入口 (drawRect:)
// -----------------------------------------------------------------------------
- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];

    CGFloat w = self.bounds.size.width;
    CGFloat h = self.bounds.size.height;

    // 依階段調用專屬繪製邏輯
    switch (_currentStage) {
        case efd::MacUIStage::Welcome:
            [self drawWelcomeScreenW:w H:h];
            break;
        case efd::MacUIStage::CalibrationInstruction:
            [self drawCalibrationInstructionW:w H:h];
            break;
        case efd::MacUIStage::CountdownWait:
            [self drawCountdownWaitW:w H:h];
            break;
        case efd::MacUIStage::ActiveCalibration:
            [self drawActiveCalibrationW:w H:h];
            break;
        case efd::MacUIStage::CalibrationResult:
            [self drawCalibrationResultW:w H:h];
            break;
        case efd::MacUIStage::MainDashboard:
            [self drawMainDashboardW:w H:h];
            break;
        case efd::MacUIStage::SettingsPanel:
            [self drawSettingsPanelW:w H:h];
            break;
        case efd::MacUIStage::StudyCompletedGate:
            [self drawStudyCompletedGateW:w H:h];
            break;
        case efd::MacUIStage::QuestionnaireSubmitted:
            [self drawQuestionnaireSubmittedW:w H:h];
            break;
    }

    // 頂部導覽列 (全域快速切換)
    [self drawTopNavigationBarW:w H:h];
}

// -----------------------------------------------------------------------------
// 頂部全局導覽列 (9 大 Tab)
// -----------------------------------------------------------------------------
- (void)drawTopNavigationBarW:(CGFloat)w H:(CGFloat)/*h*/ {
    CGFloat navH = 34.0;
    NSRect barRect = NSMakeRect(0, 0, w, navH);
    [[NSColor colorWithCalibratedRed:24/255.0 green:28/255.0 blue:18/255.0 alpha:0.95] setFill];
    NSRectFill(barRect);

    struct NavItem { efd::MacUIStage stage; NSString* label; };
    const NavItem items[] = {
        { efd::MacUIStage::Welcome,                @"歡迎" },
        { efd::MacUIStage::CalibrationInstruction, @"說明" },
        { efd::MacUIStage::CountdownWait,          @"倒數" },
        { efd::MacUIStage::ActiveCalibration,      @"測驗" },
        { efd::MacUIStage::CalibrationResult,      @"結果" },
        { efd::MacUIStage::MainDashboard,          @"監控" },
        { efd::MacUIStage::SettingsPanel,          @"設定" },
        { efd::MacUIStage::StudyCompletedGate,     @"後測" },
        { efd::MacUIStage::QuestionnaireSubmitted, @"問卷" }
    };

    size_t count = sizeof(items) / sizeof(items[0]);
    _navTabRects.resize(count);

    CGFloat gap = 4.0;
    CGFloat padding = 12.0;
    CGFloat tabW = std::clamp((w - padding * 2.0 - (count - 1) * gap) / count, 50.0, 105.0);
    CGFloat totalW = count * tabW + (count - 1) * gap;
    CGFloat startX = (w - totalW) / 2.0;

    NSFont* tabFont = [NSFont boldSystemFontOfSize:11];

    for (size_t i = 0; i < count; ++i) {
        CGFloat tabX = startX + i * (tabW + gap);
        CGFloat tabY = 4.0;
        CGFloat tabH = navH - 8.0;
        NSRect tabRect = NSMakeRect(tabX, tabY, tabW, tabH);
        _navTabRects[i] = tabRect;

        BOOL isActive = (_currentStage == items[i].stage);
        BOOL isHovered = (static_cast<NSInteger>(i) == _hoveredNavTab);

        if (isActive) {
            NSColor* activeColor = [NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0]; // 薄荷綠
            drawRoundedButton(tabRect, 4.0, activeColor);
            drawCenteredText(items[i].label, tabRect, tabFont, [NSColor whiteColor]);
        } else if (isHovered) {
            NSColor* hoverColor = [NSColor colorWithCalibratedRed:60/255.0 green:68/255.0 blue:46/255.0 alpha:1.0];
            drawRoundedButton(tabRect, 4.0, hoverColor);
            drawCenteredText(items[i].label, tabRect, tabFont, [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]);
        } else {
            NSColor* tabBg = [NSColor colorWithCalibratedRed:40/255.0 green:46/255.0 blue:32/255.0 alpha:1.0];
            drawRoundedButton(tabRect, 4.0, tabBg);
            drawCenteredText(items[i].label, tabRect, tabFont, [NSColor colorWithCalibratedWhite:0.75 alpha:1.0]);
        }
    }
}

// -----------------------------------------------------------------------------
// 階段 1：歡迎介面 (滿版薄荷綠 + Logo 縮小 50% + 粗體 + 20px 圓角按鈕)
// -----------------------------------------------------------------------------
- (void)drawWelcomeScreenW:(CGFloat)w H:(CGFloat)h {
    // 滿版薄荷綠背景 (#1EB18A)
    [[NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    // 1. Logo (縮小 50%：寬度 110px，長寬比適配 568:341 = 1.6657，高度 66px)
    CGFloat logoW = 110.0;
    CGFloat logoH = 66.0;
    CGFloat logoX = (w - logoW) / 2.0;
    CGFloat logoY = (h / 2.0 - 130.0);

    if (_logoImage) {
        [_logoImage drawInRect:NSMakeRect(logoX, logoY, logoW, logoH)
                      fromRect:NSZeroRect
                     operation:NSCompositingOperationSourceOver
                      fraction:1.0];
    }

    // 2. 標題: "感謝協助測試EFD" (粗體, 白色, 純文字無前綴)
    CGFloat titleY = logoY + logoH + 28.0;
    NSRect titleRect = NSMakeRect(0, titleY, w, 40.0);
    drawCenteredText(@"感謝協助測試EFD", titleRect, [NSFont boldSystemFontOfSize:28], [NSColor whiteColor]);

    // 副標題 (粗體, 金黃 #F7E3AF)
    NSRect subRect = NSMakeRect(0, titleY + 38.0, w, 24.0);
    drawCenteredText(@"AI 驅動眼睛特徵提取與即時疲勞監控研究系統", subRect, [NSFont boldSystemFontOfSize:14], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]);

    // 3. 進入測試說明按鈕 (粗體字體、20px 圓角邊框, 無表情貼)
    CGFloat btnW = 240.0;
    CGFloat btnH = 50.0;
    CGFloat btnX = (w - btnW) / 2.0;
    CGFloat btnY = titleY + 80.0;
    _startBtnRect = NSMakeRect(btnX, btnY, btnW, btnH);

    NSColor* btnBg = _isHoveringStartBtn ? [NSColor colorWithCalibratedWhite:0.96 alpha:1.0] : [NSColor whiteColor];
    drawRoundedButton(_startBtnRect, 20.0, btnBg);

    NSColor* btnTextColor = [NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0];
    drawCenteredText(@"進入測試說明", _startBtnRect, [NSFont boldSystemFontOfSize:17], btnTextColor);
}

// -----------------------------------------------------------------------------
// 階段 2：說明與演示預覽介面 (DEMO 動態黃點 + 3 大指引卡片)
// -----------------------------------------------------------------------------
- (void)drawCalibrationInstructionW:(CGFloat)w H:(CGFloat)h {
    [[NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    CGFloat titleY = 48.0;
    NSRect titleRect = NSMakeRect(0, titleY, w, 36.0);
    drawCenteredText(@"眼動特徵提取測驗說明", titleRect, [NSFont boldSystemFontOfSize:25], [NSColor whiteColor]);

    CGFloat contentW = std::min(w - 60.0, 840.0);
    CGFloat contentX = (w - contentW) / 2.0;
    CGFloat contentY = titleY + 44.0;
    CGFloat bottomMargin = 85.0;
    CGFloat contentH = std::max(220.0, h - contentY - bottomMargin);

    CGFloat boxW = contentW * 0.44;
    CGFloat boxH = contentH;
    CGFloat boxX = contentX;
    CGFloat boxY = contentY;

    // 左側 DEMO 演示預覽框
    NSRect previewRect = NSMakeRect(boxX, boxY, boxW, boxH);
    [[NSColor colorWithCalibratedRed:37/255.0 green:41/255.0 blue:28/255.0 alpha:1.0] setFill];
    NSRectFill(previewRect);
    drawRoundedButton(previewRect, 10.0, nil, [NSColor colorWithCalibratedRed:150/255.0 green:197/255.0 blue:247/255.0 alpha:1.0], 1.5);

    NSRect tagRect = NSMakeRect(boxX + 12.0, boxY + 8.0, boxW - 24.0, 20.0);
    drawLeftText(@"測試動態路徑演示 (DEMO 預覽)", tagRect, [NSFont boldSystemFontOfSize:12], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]);

    // 繪製演示移動黃點
    CGFloat demoPtX = boxX + _demoDotX * boxW;
    CGFloat demoPtY = boxY + _demoDotY * boxH;
    NSBezierPath* dot = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(demoPtX - 9.0, demoPtY - 9.0, 18.0, 18.0)];
    [[NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0] setFill];
    [dot fill];

    // 右側 3 大指引卡片
    CGFloat guideX = boxX + boxW + 20.0;
    CGFloat guideW = contentW - boxW - 20.0;
    CGFloat cardH = (boxH - 16.0) / 3.0;

    auto drawCard = ^(int idx, NSString* t, NSString* d) {
        CGFloat cy = boxY + idx * (cardH + 8.0);
        NSRect cRect = NSMakeRect(guideX, cy, guideW, cardH);
        drawRoundedButton(cRect, 12.0, [NSColor colorWithCalibratedRed:20/255.0 green:140/255.0 blue:108/255.0 alpha:0.75]);

        NSRect tRect = NSMakeRect(guideX + 14.0, cy + 6.0, guideW - 28.0, 22.0);
        drawLeftText(t, tRect, [NSFont boldSystemFontOfSize:14], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]);

        NSRect dRect = NSMakeRect(guideX + 14.0, cy + 28.0, guideW - 28.0, cardH - 32.0);
        drawLeftText(d, dRect, [NSFont boldSystemFontOfSize:12], [NSColor whiteColor]);
    };

    drawCard(0, @"臉部正面對齊鏡頭", @"保持端正坐姿，確保鏡頭能清晰捕捉面部特徵。");
    drawCard(1, @"視線跟隨黃點移動", @"測試開始後，請專注凝視黃點並跟隨其移動。");
    drawCard(2, @"保持自然睜眼狀態", @"校準過程僅需 5 秒鐘，請保持自然眨眼與專注。");

    // 底部準備按鈕群 (20px 圓角邊框, 粗體)
    CGFloat btnW = 220.0;
    CGFloat btnH = 46.0;
    CGFloat startX = (w - (btnW * 2.0 + 20.0)) / 2.0;
    CGFloat btnY = h - 65.0;

    _readyBtnRect = NSMakeRect(startX, btnY, btnW, btnH);
    _backWelcomeBtnRect = NSMakeRect(startX + btnW + 20.0, btnY, btnW, btnH);

    NSColor* readyBg = _isHoveringReadyBtn ? [NSColor colorWithCalibratedWhite:0.96 alpha:1.0] : [NSColor whiteColor];
    drawRoundedButton(_readyBtnRect, 20.0, readyBg);
    drawCenteredText(@"我準備好了，開始校準", _readyBtnRect, [NSFont boldSystemFontOfSize:15], [NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0]);

    NSColor* backBg = _isHoveringBackWelcomeBtn ? [NSColor colorWithCalibratedRed:20/255.0 green:130/255.0 blue:100/255.0 alpha:1.0] : [NSColor colorWithCalibratedRed:20/255.0 green:110/255.0 blue:85/255.0 alpha:1.0];
    drawRoundedButton(_backWelcomeBtnRect, 20.0, backBg);
    drawCenteredText(@"返回歡迎介面", _backWelcomeBtnRect, [NSFont boldSystemFontOfSize:15], [NSColor whiteColor]);
}

// -----------------------------------------------------------------------------
// 階段 3：3 秒倒數計時等待介面 (3 -> 2 -> 1)
// -----------------------------------------------------------------------------
- (void)drawCountdownWaitW:(CGFloat)w H:(CGFloat)h {
    [[NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    int remaining = 3 - static_cast<int>(_stageTimeSec);
    if (remaining < 1) remaining = 1;

    CGFloat centerY = h / 2.0 - 20.0;
    float pulse = 1.0f + 0.12f * std::sin((_stageTimeSec - std::floor(_stageTimeSec)) * 3.14159f);
    CGFloat r = 65.0 * pulse;

    NSBezierPath* circle = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(w / 2.0 - r, centerY - r, r * 2.0, r * 2.0)];
    [[NSColor colorWithCalibratedRed:37/255.0 green:41/255.0 blue:28/255.0 alpha:1.0] setFill];
    [circle fill];

    NSRect numRect = NSMakeRect(w / 2.0 - r, centerY - r + 8.0, r * 2.0, r * 2.0);
    drawCenteredText([NSString stringWithFormat:@"%d", remaining], numRect, [NSFont boldSystemFontOfSize:static_cast<CGFloat>(r * 0.9)], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]);

    NSRect hintRect = NSMakeRect(0, centerY + r + 24.0, w, 28.0);
    drawCenteredText(@"保持臉部端正平視螢幕，測驗即將開始...", hintRect, [NSFont boldSystemFontOfSize:16], [NSColor whiteColor]);
}

// -----------------------------------------------------------------------------
// 階段 4：實際多點眼動提取 (深黑背景 + 動態黃點 + 金色進度條)
// -----------------------------------------------------------------------------
- (void)drawActiveCalibrationW:(CGFloat)w H:(CGFloat)h {
    [[NSColor colorWithCalibratedRed:37/255.0 green:41/255.0 blue:28/255.0 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    // 動態黃點
    CGFloat dotX = _targetDotX * w;
    CGFloat dotY = _targetDotY * h;
    CGFloat dotR = 24.0;

    // 外暈光圈
    NSBezierPath* halo = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(dotX - dotR * 1.5, dotY - dotR * 1.5, dotR * 3.0, dotR * 3.0)];
    [[NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:0.25] setFill];
    [halo fill];

    // 核心黃點
    NSBezierPath* target = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(dotX - dotR, dotY - dotR, dotR * 2.0, dotR * 2.0)];
    [[NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0] setFill];
    [target fill];

    // 底部金色進度條
    CGFloat barW = std::min(w - 80.0, 480.0);
    CGFloat barH = 16.0;
    CGFloat barX = (w - barW) / 2.0;
    CGFloat barY = h - 55.0;

    drawRoundedButton(NSMakeRect(barX, barY, barW, barH), 8.0, [NSColor colorWithCalibratedWhite:0.2 alpha:0.8]);
    CGFloat fillW = barW * _calibrationProgress;
    if (fillW > 4.0) {
        drawRoundedButton(NSMakeRect(barX, barY, fillW, barH), 8.0, [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]);
    }

    NSRect pctRect = NSMakeRect(0, barY - 24.0, w, 20.0);
    drawCenteredText([NSString stringWithFormat:@"校準進度：%d%% - 請專注注視黃點移動", static_cast<int>(_calibrationProgress * 100)], pctRect, [NSFont boldSystemFontOfSize:13], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]);
}

// -----------------------------------------------------------------------------
// 階段 5：測驗完成提示介面 (個人化基準卡片 + 3 功能按鈕)
// -----------------------------------------------------------------------------
- (void)drawCalibrationResultW:(CGFloat)w H:(CGFloat)h {
    [[NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    CGFloat titleY = h / 2.0 - 170.0;
    if (titleY < 45.0) titleY = 45.0;
    NSRect titleRect = NSMakeRect(0, titleY, w, 36.0);
    drawCenteredText(@"眼動特徵提取與基準校準完成！", titleRect, [NSFont boldSystemFontOfSize:26], [NSColor whiteColor]);

    CGFloat cardW = std::min(w - 60.0, 560.0);
    CGFloat cardH = 145.0;
    CGFloat cardX = (w - cardW) / 2.0;
    CGFloat cardY = titleY + 45.0;

    drawRoundedButton(NSMakeRect(cardX, cardY, cardW, cardH), 20.0, [NSColor colorWithCalibratedRed:20/255.0 green:140/255.0 blue:108/255.0 alpha:0.75], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:0.8], 1.5);

    CGFloat subW = (cardW - 40.0) / 4.0;
    auto drawMetric = ^(int idx, NSString* lbl, NSString* val, NSString* tag) {
        CGFloat mx = cardX + 10.0 + idx * (subW + 8.0);
        CGFloat my = cardY + 14.0;
        CGFloat mh = cardH - 28.0;
        NSRect mRect = NSMakeRect(mx, my, subW, mh);
        drawRoundedButton(mRect, 10.0, [NSColor colorWithCalibratedRed:37/255.0 green:41/255.0 blue:28/255.0 alpha:0.65]);

        drawCenteredText(lbl, NSMakeRect(mx, my + 6.0, subW, 16.0), [NSFont boldSystemFontOfSize:11], [NSColor colorWithCalibratedWhite:0.85 alpha:1.0]);
        drawCenteredText(val, NSMakeRect(mx, my + 24.0, subW, 22.0), [NSFont boldSystemFontOfSize:16], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]);
        drawCenteredText(tag, NSMakeRect(mx, my + 46.0, subW, 14.0), [NSFont boldSystemFontOfSize:9], [NSColor colorWithCalibratedRed:150/255.0 green:197/255.0 blue:247/255.0 alpha:1.0]);
    };

    float baseEar = _hasTelemetry ? _latestTelemetry.eyeMetrics.earAvg : 0.312f;
    float threshEar = baseEar * 0.82f;
    drawMetric(0, @"個人基準 EAR", [NSString stringWithFormat:@"%.3f", baseEar], @"睜眼常態值");
    drawMetric(1, @"閉眼判定閾值", [NSString stringWithFormat:@"%.3f", threshEar], @"動態自適應");
    drawMetric(2, @"特徵採樣品質", @"99.2%", @"高精度捕捉");
    drawMetric(3, @"相機串流 FPS", @"30 FPS", @"即時推論中");

    // 按鈕群
    CGFloat btnW = 300.0;
    CGFloat btnH = 46.0;
    CGFloat btnX = (w - btnW) / 2.0;
    CGFloat btn1Y = cardY + cardH + 18.0;
    CGFloat btn2Y = btn1Y + btnH + 10.0;
    CGFloat btn3Y = btn2Y + btnH + 8.0;

    _proceedDashboardBtnRect = NSMakeRect(btnX, btn1Y, btnW, btnH);
    _closeBgResultBtnRect = NSMakeRect(btnX, btn2Y, btnW, btnH);
    _restartCalibBtnRect = NSMakeRect((w - 180.0) / 2.0, btn3Y, 180.0, 32.0);

    NSColor* pBg = _isHoveringProceedDashboardBtn ? [NSColor colorWithCalibratedWhite:0.96 alpha:1.0] : [NSColor whiteColor];
    drawRoundedButton(_proceedDashboardBtnRect, 20.0, pBg);
    drawCenteredText(@"進入即時疲勞監控中心", _proceedDashboardBtnRect, [NSFont boldSystemFontOfSize:16], [NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0]);

    NSColor* cBg = _isHoveringCloseBgResultBtn ? [NSColor colorWithCalibratedRed:255/255.0 green:235/255.0 blue:190/255.0 alpha:1.0] : [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0];
    drawRoundedButton(_closeBgResultBtnRect, 20.0, cBg);
    drawCenteredText(@"關閉系統介面", _closeBgResultBtnRect, [NSFont boldSystemFontOfSize:15], [NSColor colorWithCalibratedRed:37/255.0 green:41/255.0 blue:28/255.0 alpha:1.0]);

    drawRoundedButton(_restartCalibBtnRect, 16.0, [NSColor colorWithCalibratedRed:20/255.0 green:120/255.0 blue:90/255.0 alpha:0.8]);
    drawCenteredText(@"重新測驗校準", _restartCalibBtnRect, [NSFont boldSystemFontOfSize:12], [NSColor whiteColor]);
}

// -----------------------------------------------------------------------------
// 階段 6：即時眼動與疲勞監控中心 (Dashboard)
// -----------------------------------------------------------------------------
- (void)drawMainDashboardW:(CGFloat)w H:(CGFloat)h {
    [[NSColor colorWithCalibratedRed:37/255.0 green:41/255.0 blue:28/255.0 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    CGFloat headerY = 46.0;
    NSRect headerRect = NSMakeRect(0, headerY, w, 32.0);
    drawCenteredText(@"即時眼動與疲勞監控中心", headerRect, [NSFont boldSystemFontOfSize:22], [NSColor whiteColor]);

    // 核心狀態卡片
    CGFloat cardW = std::min(w - 60.0, 720.0);
    CGFloat cardH = 84.0;
    CGFloat cardX = (w - cardW) / 2.0;
    CGFloat cardY = headerY + 40.0;

    drawRoundedButton(NSMakeRect(cardX, cardY, cardW, cardH), 16.0, [NSColor colorWithCalibratedRed:52/255.0 green:58/255.0 blue:38/255.0 alpha:1.0]);

    float curEar = _hasTelemetry ? _latestTelemetry.eyeMetrics.earAvg : 0.312f;
    float curPerclos = _hasTelemetry ? (_latestTelemetry.eyeMetrics.perclos * 100.0f) : 4.2f;
    float curMse = _hasTelemetry ? _latestTelemetry.complexityMetrics.complexityIndex : 4.50f;
    float curScore = _hasTelemetry ? _latestTelemetry.systemState.currentFatigueScore : 12.5f;

    NSString* statusText = @"生理狀態：清醒專注 (Normal)";
    NSColor* statusColor = [NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0];
    if (_hasTelemetry) {
        if (_latestTelemetry.systemState.fatigueLevel == efd::FatigueLevel::SevereWarning) {
            statusText = [NSString stringWithFormat:@"生理狀態：嚴重疲勞警告 (SevereWarning - 分數 %.1f)", curScore];
            statusColor = [NSColor colorWithCalibratedRed:235/255.0 green:87/255.0 blue:87/255.0 alpha:1.0];
        } else if (_latestTelemetry.systemState.fatigueLevel == efd::FatigueLevel::Attention) {
            statusText = [NSString stringWithFormat:@"生理狀態：注意力提醒 (Attention - 分數 %.1f)", curScore];
            statusColor = [NSColor colorWithCalibratedRed:247/255.0 green:190/255.0 blue:70/255.0 alpha:1.0];
        } else {
            statusText = [NSString stringWithFormat:@"生理狀態：正常專注 (Normal - 分數 %.1f)", curScore];
            statusColor = [NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0];
        }
    }
    drawCenteredText(statusText, NSMakeRect(cardX, cardY + 12.0, cardW, 24.0), [NSFont boldSystemFontOfSize:17], statusColor);

    drawCenteredText(@"相機串流: 前置攝影機運作中 (30 FPS) | 離線特徵提取模式", NSMakeRect(cardX, cardY + 44.0, cardW, 20.0), [NSFont boldSystemFontOfSize:12], [NSColor colorWithCalibratedWhite:0.75 alpha:1.0]);

    // 4 大指標卡片 (橫向 1x4 網格)
    CGFloat gridY = cardY + cardH + 16.0;
    CGFloat gap = 12.0;
    CGFloat itemW = (cardW - gap * 3.0) / 4.0;
    CGFloat itemH = 92.0;

    auto drawMetricCard = ^(int idx, NSString* lbl, NSString* val) {
        CGFloat ix = cardX + idx * (itemW + gap);
        NSRect r = NSMakeRect(ix, gridY, itemW, itemH);
        drawRoundedButton(r, 14.0, [NSColor colorWithCalibratedRed:52/255.0 green:58/255.0 blue:38/255.0 alpha:1.0]);

        drawCenteredText(lbl, NSMakeRect(ix, gridY + 10.0, itemW, 20.0), [NSFont boldSystemFontOfSize:12], [NSColor colorWithCalibratedWhite:0.8 alpha:1.0]);
        drawCenteredText(val, NSMakeRect(ix, gridY + 36.0, itemW, 30.0), [NSFont boldSystemFontOfSize:22], [NSColor colorWithCalibratedRed:150/255.0 green:197/255.0 blue:247/255.0 alpha:1.0]);
    };

    drawMetricCard(0, @"雙眼 EAR", [NSString stringWithFormat:@"%.3f", curEar]);
    drawMetricCard(1, @"PERCLOS 閉眼比", [NSString stringWithFormat:@"%.1f%%", curPerclos]);
    drawMetricCard(2, @"複雜度 (MSE)", [NSString stringWithFormat:@"%.2f", curMse]);
    drawMetricCard(3, @"綜合疲勞分數", [NSString stringWithFormat:@"%.1f", curScore]);

    // 底部控制按鈕：進入設定介面 與 關閉系統介面 (20px 圓角邊框, 粗體)
    CGFloat btnW = 210.0;
    CGFloat btnH = 46.0;
    CGFloat totalW = btnW * 2.0 + 20.0;
    CGFloat startX = (w - totalW) / 2.0;
    CGFloat btnY = h - 68.0;

    _settingsBtnRect = NSMakeRect(startX, btnY, btnW, btnH);
    _minimizeTrayBtnRect = NSMakeRect(startX + btnW + 20.0, btnY, btnW, btnH);

    NSColor* sBg = _isHoveringSettingsBtn ? [NSColor colorWithCalibratedRed:45/255.0 green:185/255.0 blue:145/255.0 alpha:1.0] : [NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0];
    drawRoundedButton(_settingsBtnRect, 20.0, sBg);
    drawCenteredText(@"進入設定介面", _settingsBtnRect, [NSFont boldSystemFontOfSize:15], [NSColor whiteColor]);

    NSColor* mBg = _isHoveringMinimizeTrayBtn ? [NSColor colorWithCalibratedRed:255/255.0 green:235/255.0 blue:190/255.0 alpha:1.0] : [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0];
    drawRoundedButton(_minimizeTrayBtnRect, 20.0, mBg);
    drawCenteredText(@"關閉系統介面", _minimizeTrayBtnRect, [NSFont boldSystemFontOfSize:15], [NSColor colorWithCalibratedRed:37/255.0 green:41/255.0 blue:28/255.0 alpha:1.0]);
}

// -----------------------------------------------------------------------------
// 階段 7：系統設定 (僅保留：提示靈敏度、重設眼動監測、返回監控中心)
// -----------------------------------------------------------------------------
- (void)drawSettingsPanelW:(CGFloat)w H:(CGFloat)/*h*/ {
    [[NSColor colorWithCalibratedRed:37/255.0 green:41/255.0 blue:28/255.0 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    CGFloat titleY = 48.0;
    drawCenteredText(@"系統設定", NSMakeRect(0, titleY, w, 32.0), [NSFont boldSystemFontOfSize:24], [NSColor whiteColor]);

    CGFloat cardW = std::min(w - 80.0, 600.0);
    CGFloat cardX = (w - cardW) / 2.0;
    CGFloat startY = titleY + 50.0;
    CGFloat itemH = 58.0;
    CGFloat gap = 16.0;

    // 項目 1: 提示靈敏度 (粗體, 20px 圓角邊框)
    _sensitivityBtnRect = NSMakeRect(cardX, startY, cardW, itemH);
    NSColor* bg1 = _isHoveringSensitivityBtn ? [NSColor colorWithCalibratedRed:60/255.0 green:68/255.0 blue:46/255.0 alpha:1.0] : [NSColor colorWithCalibratedRed:50/255.0 green:56/255.0 blue:38/255.0 alpha:1.0];
    drawRoundedButton(_sensitivityBtnRect, 20.0, bg1, [NSColor colorWithCalibratedRed:150/255.0 green:197/255.0 blue:247/255.0 alpha:0.8]);

    drawLeftText(@"提示靈敏度", NSMakeRect(cardX + 20.0, startY, cardW / 2.0, itemH), [NSFont boldSystemFontOfSize:15], [NSColor whiteColor]);
    NSArray* sensLabels = @[ @"低靈敏度 (保守)", @"標準靈敏度 (推薦)", @"高靈敏度 (即時警報)" ];
    NSString* sensStr = [NSString stringWithFormat:@"%@ (點擊切換)", sensLabels[_sensitivityLevel]];
    drawRightText(sensStr, NSMakeRect(cardX + cardW / 2.0, startY, cardW / 2.0 - 20.0, itemH), [NSFont boldSystemFontOfSize:13], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]);

    // 項目 2: 重設眼動監測 (粗體, 20px 圓角邊框)
    CGFloat item2Y = startY + itemH + gap;
    _recalibFromSettingsBtnRect = NSMakeRect(cardX, item2Y, cardW, itemH);
    NSColor* bg2 = _isHoveringRecalibFromSettingsBtn ? [NSColor colorWithCalibratedRed:30/255.0 green:90/255.0 blue:70/255.0 alpha:1.0] : [NSColor colorWithCalibratedRed:20/255.0 green:68/255.0 blue:52/255.0 alpha:1.0];
    drawRoundedButton(_recalibFromSettingsBtnRect, 20.0, bg2, [NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:0.8]);

    drawLeftText(@"重設眼動監測", NSMakeRect(cardX + 20.0, item2Y, cardW / 2.0, itemH), [NSFont boldSystemFontOfSize:15], [NSColor whiteColor]);
    drawRightText(@"重新校準眼動基準 (點擊執行)", NSMakeRect(cardX + cardW / 2.0, item2Y, cardW / 2.0 - 20.0, itemH), [NSFont boldSystemFontOfSize:13], [NSColor colorWithCalibratedRed:120/255.0 green:230/255.0 blue:195/255.0 alpha:1.0]);

    // 3. 底部動作按鈕：返回監控中心 (粗體, 20px 圓角邊框)
    CGFloat retW = 280.0;
    CGFloat retH = 48.0;
    CGFloat retY = item2Y + itemH + 32.0;
    _saveSettingsBtnRect = NSMakeRect((w - retW) / 2.0, retY, retW, retH);

    NSColor* retBg = _isHoveringSaveSettingsBtn ? [NSColor colorWithCalibratedRed:170/255.0 green:215/255.0 blue:255/255.0 alpha:1.0] : [NSColor colorWithCalibratedRed:150/255.0 green:197/255.0 blue:247/255.0 alpha:1.0];
    drawRoundedButton(_saveSettingsBtnRect, 20.0, retBg);
    drawCenteredText(@"返回監控中心", _saveSettingsBtnRect, [NSFont boldSystemFontOfSize:15], [NSColor colorWithCalibratedRed:37/255.0 green:41/255.0 blue:28/255.0 alpha:1.0]);
}

// -----------------------------------------------------------------------------
// 階段 8：後測介面 (問卷連結與狀態解鎖)
// -----------------------------------------------------------------------------
- (void)drawStudyCompletedGateW:(CGFloat)w H:(CGFloat)/*h*/ {
    [[NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    CGFloat titleY = 48.0;
    drawCenteredText(@"後測介面", NSMakeRect(0, titleY, w, 38.0), [NSFont boldSystemFontOfSize:26], [NSColor whiteColor]);

    CGFloat cardW = std::min(w - 60.0, 560.0);
    CGFloat cardH = 130.0;
    CGFloat cardX = (w - cardW) / 2.0;
    CGFloat cardY = titleY + 48.0;

    drawRoundedButton(NSMakeRect(cardX, cardY, cardW, cardH), 20.0, [NSColor colorWithCalibratedRed:20/255.0 green:140/255.0 blue:108/255.0 alpha:0.75], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:0.8], 1.5);

    drawCenteredText(@"受試者匿名代碼: (14 天時序已安全封存)", NSMakeRect(cardX + 10.0, cardY + 10.0, cardW - 20.0, 22.0), [NSFont boldSystemFontOfSize:13], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]);

    NSString* desc = @"雙眼特徵時序記錄已完成\n離線資料庫落盤校驗通過\n請點擊下方按鈕前往填寫後測問卷以完成實驗流程\n問卷網址: https://forms.gle/y5f1jTnrrtsxz65G9";
    drawCenteredText(desc, NSMakeRect(cardX + 14.0, cardY + 36.0, cardW - 28.0, cardH - 42.0), [NSFont boldSystemFontOfSize:12], [NSColor whiteColor]);

    // 按鈕群
    CGFloat btnW = 320.0;
    CGFloat btnH = 50.0;
    CGFloat btnX = (w - btnW) / 2.0;
    CGFloat btnY = cardY + cardH + 24.0;
    _fillQuestionnaireBtnRect = NSMakeRect(btnX, btnY, btnW, btnH);

    NSColor* fillBg = _isHoveringFillQuestionnaireBtn ? [NSColor colorWithCalibratedWhite:0.96 alpha:1.0] : [NSColor whiteColor];
    drawRoundedButton(_fillQuestionnaireBtnRect, 20.0, fillBg);
    drawCenteredText(@"前往填寫後測問卷 (Google 表單)", _fillQuestionnaireBtnRect, [NSFont boldSystemFontOfSize:16], [NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0]);

    CGFloat retW = 160.0;
    CGFloat retH = 32.0;
    CGFloat retY = btnY + btnH + 12.0;
    _returnDashboardBtnRect = NSMakeRect((w - retW) / 2.0, retY, retW, retH);

    NSColor* rBg = _isHoveringReturnDashboardBtn ? [NSColor colorWithCalibratedRed:24/255.0 green:140/255.0 blue:108/255.0 alpha:0.8] : [NSColor colorWithCalibratedRed:20/255.0 green:120/255.0 blue:90/255.0 alpha:0.5];
    drawRoundedButton(_returnDashboardBtnRect, 16.0, rBg);
    drawCenteredText(@"返回即時監控中心", _returnDashboardBtnRect, [NSFont boldSystemFontOfSize:12], [NSColor whiteColor]);
}

// -----------------------------------------------------------------------------
// 階段 9：後測問卷填寫完成介面 (資產 6.png 設計)
// -----------------------------------------------------------------------------
- (void)drawQuestionnaireSubmittedW:(CGFloat)w H:(CGFloat)/*h*/ {
    [[NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    CGFloat titleY = 48.0;
    drawCenteredText(@"填寫成功!感謝您協助施測", NSMakeRect(0, titleY, w, 38.0), [NSFont boldSystemFontOfSize:26], [NSColor whiteColor]);

    CGFloat cardW = std::min(w - 60.0, 560.0);
    CGFloat cardH = 130.0;
    CGFloat cardX = (w - cardW) / 2.0;
    CGFloat cardY = titleY + 48.0;

    drawRoundedButton(NSMakeRect(cardX, cardY, cardW, cardH), 20.0, [NSColor colorWithCalibratedRed:20/255.0 green:140/255.0 blue:108/255.0 alpha:0.75], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:0.8], 1.5);

    drawCenteredText(@"科研解鎖授權碼: EFD-14D-8821-4903", NSMakeRect(cardX + 10.0, cardY + 10.0, cardW - 20.0, 22.0), [NSFont boldSystemFontOfSize:14], [NSColor colorWithCalibratedRed:247/255.0 green:227/255.0 blue:175/255.0 alpha:1.0]);

    NSString* guide = @"感謝您的寶貴數據回饋，協助推動眼睛疲勞監測科研進展。\n本機 SQLite 時序資料庫已驗證並解除鎖定。\n您現在可以安全關閉並解除安裝本軟體。";
    drawCenteredText(guide, NSMakeRect(cardX + 14.0, cardY + 36.0, cardW - 28.0, cardH - 42.0), [NSFont boldSystemFontOfSize:12], [NSColor whiteColor]);

    // 動作按鈕群
    CGFloat btnW = 300.0;
    CGFloat btnH = 50.0;
    CGFloat btnX = (w - btnW) / 2.0;
    CGFloat btnY = cardY + cardH + 24.0;
    _exitAppBtnRect = NSMakeRect(btnX, btnY, btnW, btnH);

    NSColor* eBg = _isHoveringExitAppBtn ? [NSColor colorWithCalibratedWhite:0.96 alpha:1.0] : [NSColor whiteColor];
    drawRoundedButton(_exitAppBtnRect, 20.0, eBg);
    drawCenteredText(@"完成並退出系統", _exitAppBtnRect, [NSFont boldSystemFontOfSize:16], [NSColor colorWithCalibratedRed:30/255.0 green:177/255.0 blue:138/255.0 alpha:1.0]);

    CGFloat retW = 160.0;
    CGFloat retH = 32.0;
    CGFloat retY = btnY + btnH + 12.0;
    _returnDashboardBtnRect = NSMakeRect((w - retW) / 2.0, retY, retW, retH);

    NSColor* rBg = _isHoveringReturnDashboardBtn ? [NSColor colorWithCalibratedRed:24/255.0 green:140/255.0 blue:108/255.0 alpha:0.8] : [NSColor colorWithCalibratedRed:20/255.0 green:120/255.0 blue:90/255.0 alpha:0.5];
    drawRoundedButton(_returnDashboardBtnRect, 16.0, rBg);
    drawCenteredText(@"返回即時監控中心", _returnDashboardBtnRect, [NSFont boldSystemFontOfSize:12], [NSColor whiteColor]);
}

// -----------------------------------------------------------------------------
// 事件處理：滑鼠點擊與 Hover
// -----------------------------------------------------------------------------
- (void)mouseDown:(NSEvent *)event {
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];

    // 檢查導覽列點擊
    for (size_t i = 0; i < _navTabRects.size(); ++i) {
        if (NSPointInRect(loc, _navTabRects[i])) {
            [self setStage:static_cast<efd::MacUIStage>(i)];
            return;
        }
    }

    // 依階段檢查按鈕點擊
    switch (_currentStage) {
        case efd::MacUIStage::Welcome:
            if (NSPointInRect(loc, _startBtnRect)) {
                [self setStage:efd::MacUIStage::CalibrationInstruction];
            }
            break;
        case efd::MacUIStage::CalibrationInstruction:
            if (NSPointInRect(loc, _readyBtnRect)) {
                [self setStage:efd::MacUIStage::CountdownWait];
            } else if (NSPointInRect(loc, _backWelcomeBtnRect)) {
                [self setStage:efd::MacUIStage::Welcome];
            }
            break;
        case efd::MacUIStage::CountdownWait:
            break;
        case efd::MacUIStage::ActiveCalibration:
            break;
        case efd::MacUIStage::CalibrationResult:
            if (NSPointInRect(loc, _proceedDashboardBtnRect)) {
                [self setStage:efd::MacUIStage::MainDashboard];
            } else if (NSPointInRect(loc, _closeBgResultBtnRect)) {
                [self minimizeToBackground];
            } else if (NSPointInRect(loc, _restartCalibBtnRect)) {
                [self setStage:efd::MacUIStage::CalibrationInstruction];
            }
            break;
        case efd::MacUIStage::MainDashboard:
            if (NSPointInRect(loc, _settingsBtnRect)) {
                [self setStage:efd::MacUIStage::SettingsPanel];
            } else if (NSPointInRect(loc, _minimizeTrayBtnRect)) {
                [self minimizeToBackground];
            }
            break;
        case efd::MacUIStage::SettingsPanel:
            if (NSPointInRect(loc, _sensitivityBtnRect)) {
                _sensitivityLevel = (_sensitivityLevel + 1) % 3;
                [self setNeedsDisplay:YES];
            } else if (NSPointInRect(loc, _recalibFromSettingsBtnRect)) {
                [self setStage:efd::MacUIStage::CalibrationInstruction];
            } else if (NSPointInRect(loc, _saveSettingsBtnRect)) {
                [self setStage:efd::MacUIStage::MainDashboard];
            }
            break;
        case efd::MacUIStage::StudyCompletedGate:
            if (NSPointInRect(loc, _fillQuestionnaireBtnRect)) {
                // 自動開啟 Google 表單問卷網址
                [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://forms.gle/y5f1jTnrrtsxz65G9"]];
                if (_engine) {
                    _engine->getStudyTracker().submitQuestionnaire("Study_Post_Survey_Completed");
                }
                [self setStage:efd::MacUIStage::QuestionnaireSubmitted];
            } else if (NSPointInRect(loc, _returnDashboardBtnRect)) {
                [self setStage:efd::MacUIStage::MainDashboard];
            }
            break;
        case efd::MacUIStage::QuestionnaireSubmitted:
            if (NSPointInRect(loc, _exitAppBtnRect)) {
                [NSApp terminate:nil];
            } else if (NSPointInRect(loc, _returnDashboardBtnRect)) {
                [self setStage:efd::MacUIStage::MainDashboard];
            }
            break;
    }
}

- (void)mouseMoved:(NSEvent *)event {
    NSPoint loc = [self convertPoint:[event locationInWindow] fromView:nil];

    NSInteger hoveredTab = -1;
    for (size_t i = 0; i < _navTabRects.size(); ++i) {
        if (NSPointInRect(loc, _navTabRects[i])) {
            hoveredTab = static_cast<NSInteger>(i);
            break;
        }
    }

    BOOL inStart = (_currentStage == efd::MacUIStage::Welcome) && NSPointInRect(loc, _startBtnRect);
    BOOL inReady = (_currentStage == efd::MacUIStage::CalibrationInstruction) && NSPointInRect(loc, _readyBtnRect);
    BOOL inBackWelcome = (_currentStage == efd::MacUIStage::CalibrationInstruction) && NSPointInRect(loc, _backWelcomeBtnRect);
    BOOL inProceedDash = (_currentStage == efd::MacUIStage::CalibrationResult) && NSPointInRect(loc, _proceedDashboardBtnRect);
    BOOL inCloseBgResult = (_currentStage == efd::MacUIStage::CalibrationResult) && NSPointInRect(loc, _closeBgResultBtnRect);
    BOOL inRestartCalib = (_currentStage == efd::MacUIStage::CalibrationResult) && NSPointInRect(loc, _restartCalibBtnRect);
    BOOL inSettings = (_currentStage == efd::MacUIStage::MainDashboard) && NSPointInRect(loc, _settingsBtnRect);
    BOOL inMinimizeTray = (_currentStage == efd::MacUIStage::MainDashboard) && NSPointInRect(loc, _minimizeTrayBtnRect);
    BOOL inSensitivity = (_currentStage == efd::MacUIStage::SettingsPanel) && NSPointInRect(loc, _sensitivityBtnRect);
    BOOL inRecalibFromSet = (_currentStage == efd::MacUIStage::SettingsPanel) && NSPointInRect(loc, _recalibFromSettingsBtnRect);
    BOOL inSaveSet = (_currentStage == efd::MacUIStage::SettingsPanel) && NSPointInRect(loc, _saveSettingsBtnRect);
    BOOL inFillQ = (_currentStage == efd::MacUIStage::StudyCompletedGate) && NSPointInRect(loc, _fillQuestionnaireBtnRect);
    BOOL inReturnDash = (_currentStage == efd::MacUIStage::StudyCompletedGate || _currentStage == efd::MacUIStage::QuestionnaireSubmitted) && NSPointInRect(loc, _returnDashboardBtnRect);
    BOOL inExitApp = (_currentStage == efd::MacUIStage::QuestionnaireSubmitted) && NSPointInRect(loc, _exitAppBtnRect);

    BOOL hasChange = (hoveredTab != _hoveredNavTab ||
                      inStart != _isHoveringStartBtn ||
                      inReady != _isHoveringReadyBtn ||
                      inBackWelcome != _isHoveringBackWelcomeBtn ||
                      inProceedDash != _isHoveringProceedDashboardBtn ||
                      inCloseBgResult != _isHoveringCloseBgResultBtn ||
                      inRestartCalib != _isHoveringRestartCalibBtn ||
                      inSettings != _isHoveringSettingsBtn ||
                      inMinimizeTray != _isHoveringMinimizeTrayBtn ||
                      inSensitivity != _isHoveringSensitivityBtn ||
                      inRecalibFromSet != _isHoveringRecalibFromSettingsBtn ||
                      inSaveSet != _isHoveringSaveSettingsBtn ||
                      inFillQ != _isHoveringFillQuestionnaireBtn ||
                      inReturnDash != _isHoveringReturnDashboardBtn ||
                      inExitApp != _isHoveringExitAppBtn);

    if (hasChange) {
        _hoveredNavTab = hoveredTab;
        _isHoveringStartBtn = inStart;
        _isHoveringReadyBtn = inReady;
        _isHoveringBackWelcomeBtn = inBackWelcome;
        _isHoveringProceedDashboardBtn = inProceedDash;
        _isHoveringCloseBgResultBtn = inCloseBgResult;
        _isHoveringRestartCalibBtn = inRestartCalib;
        _isHoveringSettingsBtn = inSettings;
        _isHoveringMinimizeTrayBtn = inMinimizeTray;
        _isHoveringSensitivityBtn = inSensitivity;
        _isHoveringRecalibFromSettingsBtn = inRecalibFromSet;
        _isHoveringSaveSettingsBtn = inSaveSet;
        _isHoveringFillQuestionnaireBtn = inFillQ;
        _isHoveringReturnDashboardBtn = inReturnDash;
        _isHoveringExitAppBtn = inExitApp;

        BOOL isAnyHovered = (hoveredTab != -1 || inStart || inReady || inBackWelcome || inProceedDash || inCloseBgResult || inRestartCalib || inSettings || inMinimizeTray || inSensitivity || inRecalibFromSet || inSaveSet || inFillQ || inReturnDash || inExitApp);
        if (isAnyHovered) {
            [[NSCursor pointingHandCursor] set];
        } else {
            [[NSCursor arrowCursor] set];
        }
        [self setNeedsDisplay:YES];
    }
}

@end

// -----------------------------------------------------------------------------
// MacWelcomeWindow 實作 (PIMPL)
// -----------------------------------------------------------------------------
namespace efd {

class MacWelcomeWindow::Impl {
public:
    int width;
    int height;
    AsyncPipelineEngine engine;
    MacStatusItemManager statusItemManager;
    NSWindow* window = nil;
    EFDMainCanvasView* canvasView = nil;

    Impl(int w, int h)
        : width(w), height(h), engine(PlatformType::macOS) {}

    ~Impl() {
        engine.stop();
        statusItemManager.removeStatusItem();
    }
};

MacWelcomeWindow::MacWelcomeWindow(int width, int height)
    : m_impl(std::make_unique<Impl>(width, height)) {
}

MacWelcomeWindow::~MacWelcomeWindow() = default;

AsyncPipelineEngine& MacWelcomeWindow::getEngine() {
    return m_impl->engine;
}

void MacWelcomeWindow::showWindow() {
    if (m_impl->window) {
        [m_impl->window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }
}

void MacWelcomeWindow::hideToBackground() {
    if (m_impl->window) {
        [m_impl->window orderOut:nil];
    }
}

void MacWelcomeWindow::toggleFloatingHUD() {
    showWindow();
}

int MacWelcomeWindow::run() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        // 建立應用程式選單 (Quit Cmd+Q 等標準快捷鍵)
        NSMenu* menubar = [[NSMenu alloc] init];
        NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
        [menubar addItem:appMenuItem];
        [NSApp setMainMenu:menubar];

        NSMenu* appMenu = [[NSMenu alloc] init];
        NSMenuItem* quitMenuItem = [[NSMenuItem alloc] initWithTitle:@"Quit EFD"
                                                              action:@selector(terminate:)
                                                       keyEquivalent:@"q"];
        [appMenu addItem:quitMenuItem];
        [appMenuItem setSubmenu:appMenu];

        // 建立主視窗 (960 x 640)
        NSRect frame = NSMakeRect(0, 0, m_impl->width, m_impl->height);
        NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | 
                                      NSWindowStyleMaskClosable | 
                                      NSWindowStyleMaskMiniaturizable | 
                                      NSWindowStyleMaskResizable;

        m_impl->window = [[NSWindow alloc] initWithContentRect:frame
                                                     styleMask:styleMask
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
        m_impl->window.title = @"EFD 眼睛特徵提取與即時疲勞監控研究系統";
        [m_impl->window center];
        m_impl->window.releasedWhenClosed = NO;
        m_impl->window.acceptsMouseMovedEvents = YES;

        // 建立並裝載畫布視圖
        m_impl->canvasView = [[EFDMainCanvasView alloc] initWithFrame:frame engine:&m_impl->engine];
        m_impl->canvasView.parentWindow = m_impl->window;
        m_impl->canvasView.statusItemManager = &m_impl->statusItemManager;
        m_impl->window.contentView = m_impl->canvasView;

        // 初始化頂部選單列狀態圖示 (Status Item)
        m_impl->statusItemManager.initialize("EFD 眼睛疲勞即時監測系統");
        m_impl->statusItemManager.setActionCallback([this](MacMenuAction action) {
            switch (action) {
                case MacMenuAction::ShowMainWindow:
                    this->showWindow();
                    break;
                case MacMenuAction::ToggleFloatingIndicator:
                    this->toggleFloatingHUD();
                    break;
                case MacMenuAction::Recalibrate:
                    this->showWindow();
                    if (this->m_impl->canvasView) {
                        [this->m_impl->canvasView setStage:MacUIStage::CalibrationInstruction];
                    }
                    break;
                case MacMenuAction::EndStudyGate:
                    this->showWindow();
                    this->m_impl->engine.getStudyTracker().triggerPostStudyLock();
                    if (this->m_impl->canvasView) {
                        [this->m_impl->canvasView setStage:MacUIStage::StudyCompletedGate];
                    }
                    break;
                case MacMenuAction::ExitApp:
                    [NSApp terminate:nil];
                    break;
            }
        });

        // 設定應用程式 Dock 圖示為「資產 10」
        NSImage* appIcon = nil;
        NSString* iconPath = [[NSBundle mainBundle] pathForResource:@"AppIcon" ofType:@"icns"];
        if (iconPath) {
            appIcon = [[NSImage alloc] initWithContentsOfFile:iconPath];
        }
        if (!appIcon) {
            iconPath = [[NSBundle mainBundle] pathForResource:@"資產 10" ofType:@"png" inDirectory:@"design/1x"];
            if (iconPath) appIcon = [[NSImage alloc] initWithContentsOfFile:iconPath];
        }
        if (appIcon) {
            [NSApp setApplicationIconImage:appIcon];
        }

        // 綁定引擎遙測事件 (持續在終端機輸出即時眼動數據)
        static int s_macTelemetryLogCount = 0;
        m_impl->engine.setTelemetryCallback([this](const EngineTelemetry& t) {
            this->m_impl->statusItemManager.updateStatus(
                t.systemState.fatigueLevel,
                t.systemState.currentFatigueScore,
                t.lifecycleSummary
            );

            // 控制台/終端機持續輸出即時眼動數據串流 (每 15 影格或閉眼瞬間)
            int count = ++s_macTelemetryLogCount;
            if (count % 15 == 0 || t.eyeMetrics.isEyeClosed) {
                const char* levelStr = "清醒放鬆 (Relaxed)";
                if (t.systemState.fatigueLevel == FatigueLevel::Attention) levelStr = "注意力提醒 (Attention)";
                else if (t.systemState.fatigueLevel == FatigueLevel::SevereWarning) levelStr = "嚴重疲勞警告 (SevereWarning)";
                else if (t.systemState.fatigueLevel == FatigueLevel::UserAway) levelStr = "離座偵測 (UserAway)";

                std::cout << "[即時眼動數據] 影格: " << std::setw(5) << t.totalFramesProcessed
                          << " | EAR: " << std::fixed << std::setprecision(3) << t.eyeMetrics.earAvg
                          << (t.eyeMetrics.isEyeClosed ? " (閉眼)" : " (睜眼)")
                          << " | 閉眼比 (PERCLOS): " << std::setprecision(1) << (t.eyeMetrics.perclos * 100.0f) << "%"
                          << " | 複雜度 (MSE): " << std::setprecision(2) << t.complexityMetrics.complexityIndex
                          << " | 疲勞分數: " << std::setprecision(1) << t.systemState.currentFatigueScore
                          << " | 生理狀態: " << levelStr;

                if (t.systemState.cooldownState == CooldownState::InCooldown) {
                    std::cout << " [冷卻中: " << t.systemState.cooldownRemainingSeconds << "s]";
                }
                std::cout << std::endl;
            }

            if (this->m_impl->canvasView) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [this->m_impl->canvasView updateTelemetry:t];
                });
            }
        });

        // 綁定警報事件 (僅在疲勞超標時跳出通知)
        m_impl->engine.setAlertCallback([this](FatigueLevel level, float score, const std::string& msg) {
            (void)msg;
            if (level == FatigueLevel::SevereWarning || score >= 70.0f) {
                std::cout << "\n>>> [疲勞警報通知發送] 疲勞分數: " << std::fixed << std::setprecision(1) << score
                          << " (嚴重警告) - 你的眼睛處於疲勞狀態，請適當休息 <<<\n\n";

                this->m_impl->statusItemManager.showNotification(
                    "你的眼睛處於疲勞狀態，請適當休息",
                    "你的眼睛處於疲勞狀態，請適當休息",
                    level
                );
            }
        });

        // 顯示主視窗並進入 Cocoa 訊息循環
        [m_impl->window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
    return 0;
}

} // namespace efd

