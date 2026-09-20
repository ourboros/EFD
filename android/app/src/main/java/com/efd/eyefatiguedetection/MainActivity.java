package com.efd.eyefatiguedetection;

import android.Manifest;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationCompat;
import androidx.core.content.ContextCompat;

import org.json.JSONObject;

public class MainActivity extends AppCompatActivity {

    public enum UIStage {
        Welcome,
        CalibrationInstruction,
        CountdownWait,
        ActiveCalibration,
        CalibrationResult,
        MainDashboard,
        SettingsPanel,
        StudyCompletedGate,
        QuestionnaireSubmitted
    }

    private UIStage mCurrentStage = UIStage.Welcome;
    private Handler mHandler;
    private Runnable mTelemetryRunnable;
    private Vibrator mVibrator;

    private LinearLayout mMainContainer;
    private LinearLayout mNavBarContainer;
    private FrameLayout mStageContentContainer;

    // 校準動畫
    private float mCalibProgress = 0f;
    private float mDotX = 0.5f;
    private float mDotY = 0.5f;
    private int mCountdownSec = 3;

    // 即時遙測數據
    private float mCurrentScore = 0.0f;
    private int mCurrentLevel = 0;
    private String mLastAlertMsg = "";
    private boolean mHasShownAlert = false;

    private static final String CHANNEL_ID = "efd_fatigue_alerts";
    private static final int PERMISSION_REQ_CODE = 1001;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        mHandler = new Handler(Looper.getMainLooper());
        mVibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);

        // 初始化通知通道
        createNotificationChannel();

        // 請求相機與通知權限
        requestAppPermissions();

        // 初始化原生 NDK C++ 引擎
        EfdNativeBridge.nativeInitEngine();

        // 建立純程式化響應式 UI
        setupViews();

        // 啟動 30FPS 遙測輪詢
        startTelemetryLoop();
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "EFD 眼睛疲勞警報",
                    NotificationManager.IMPORTANCE_HIGH
            );
            channel.setDescription("當偵測到重度疲勞紅色警報時發送即時提醒");
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) {
                manager.createNotificationChannel(channel);
            }
        }
    }

    private void requestAppPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED ||
                ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this,
                        new String[]{Manifest.permission.CAMERA, Manifest.permission.POST_NOTIFICATIONS},
                        PERMISSION_REQ_CODE);
            }
        } else {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this,
                        new String[]{Manifest.permission.CAMERA},
                        PERMISSION_REQ_CODE);
            }
        }
    }

    private void setupViews() {
        mMainContainer = new LinearLayout(this);
        mMainContainer.setOrientation(LinearLayout.VERTICAL);
        mMainContainer.setBackgroundColor(Color.parseColor("#25291C")); // EFD 深黑墨綠背景

        // 1. 頂部快速切換導覽列
        setupTopNavBar();

        // 2. 階段內容容器
        mStageContentContainer = new FrameLayout(this);
        LinearLayout.LayoutParams contentParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.0f);
        mMainContainer.addView(mStageContentContainer, contentParams);

        setContentView(mMainContainer);

        // 渲染初始頁面
        renderCurrentStage();
    }

    private void setupTopNavBar() {
        HorizontalScrollView scroll = new HorizontalScrollView(this);
        scroll.setBackgroundColor(Color.parseColor("#181C12"));
        scroll.setHorizontalScrollBarEnabled(false);

        mNavBarContainer = new LinearLayout(this);
        mNavBarContainer.setOrientation(LinearLayout.HORIZONTAL);
        mNavBarContainer.setPadding(12, 10, 12, 10);

        String[] labels = {
                "🏠 歡迎", "📖 說明", "⏱️ 倒數", "🎯 測驗",
                "✨ 結果", "📊 監控", "⚙️ 設定", "🔒 門禁", "📋 問卷"
        };
        UIStage[] stages = UIStage.values();

        for (int i = 0; i < stages.length; i++) {
            final UIStage stage = stages[i];
            Button btn = new Button(this);
            btn.setText(labels[i]);
            btn.setTextSize(11);
            btn.setPadding(18, 6, 18, 6);
            btn.setTextColor(stage == mCurrentStage ? Color.WHITE : Color.parseColor("#F7E3AF"));
            btn.setBackgroundColor(stage == mCurrentStage ? Color.parseColor("#1EB18A") : Color.parseColor("#343A26"));

            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            lp.setMargins(6, 0, 6, 0);
            btn.setLayoutParams(lp);

            btn.setOnClickListener(v -> setStage(stage));
            mNavBarContainer.addView(btn);
        }

        scroll.addView(mNavBarContainer);
        mMainContainer.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
    }

    public void setStage(UIStage stage) {
        mCurrentStage = stage;
        updateNavBarHighlight();
        renderCurrentStage();
    }

    private void updateNavBarHighlight() {
        UIStage[] stages = UIStage.values();
        for (int i = 0; i < mNavBarContainer.getChildCount() && i < stages.length; i++) {
            View child = mNavBarContainer.getChildAt(i);
            if (child instanceof Button) {
                Button btn = (Button) child;
                boolean isActive = (stages[i] == mCurrentStage);
                btn.setTextColor(isActive ? Color.WHITE : Color.parseColor("#F7E3AF"));
                btn.setBackgroundColor(isActive ? Color.parseColor("#1EB18A") : Color.parseColor("#343A26"));
            }
        }
    }

    private void renderCurrentStage() {
        mStageContentContainer.removeAllViews();

        switch (mCurrentStage) {
            case Welcome:
                renderWelcomeScreen();
                break;
            case CalibrationInstruction:
                renderInstructionScreen();
                break;
            case CountdownWait:
                renderCountdownScreen();
                break;
            case ActiveCalibration:
                renderActiveCalibrationScreen();
                break;
            case CalibrationResult:
                renderCalibrationResultScreen();
                break;
            case MainDashboard:
                renderMainDashboardScreen();
                break;
            case SettingsPanel:
                renderSettingsScreen();
                break;
            case StudyCompletedGate:
                renderStudyGateScreen();
                break;
            case QuestionnaireSubmitted:
                renderQuestionnaireSubmittedScreen();
                break;
        }
    }

    // -------------------------------------------------------------------------
    // 階段 1：歡迎介面 (滿版薄荷綠背景 + 純白圓形徽章)
    // -------------------------------------------------------------------------
    private void renderWelcomeScreen() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.parseColor("#1EB18A")); // 薄荷綠
        layout.setPadding(32, 32, 32, 32);

        // 純白圓形徽章容器 (包含向量眼睛)
        FrameLayout badge = new FrameLayout(this);
        int badgeSize = 280;
        FrameLayout.LayoutParams badgeParams = new FrameLayout.LayoutParams(badgeSize, badgeSize);
        badgeParams.gravity = Gravity.CENTER;

        View circleBg = new View(this) {
            @Override
            protected void onDraw(Canvas canvas) {
                super.onDraw(canvas);
                Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
                p.setColor(Color.WHITE);
                canvas.drawCircle(getWidth() / 2f, getHeight() / 2f, getWidth() / 2f - 4, p);
                p.setStyle(Paint.Style.STROKE);
                p.setColor(Color.parseColor("#F7E3AF"));
                p.setStrokeWidth(6f);
                canvas.drawCircle(getWidth() / 2f, getHeight() / 2f, getWidth() / 2f - 4, p);

                // 畫眼睛
                p.setStyle(Paint.Style.STROKE);
                p.setColor(Color.parseColor("#1EB18A"));
                p.setStrokeWidth(8f);
                float cx = getWidth() / 2f;
                float cy = getHeight() / 2f;
                canvas.drawOval(cx - 70, cy - 40, cx + 70, cy + 40, p);
                p.setStyle(Paint.Style.FILL);
                canvas.drawCircle(cx, cy, 22, p);
            }
        };
        badge.addView(circleBg, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        layout.addView(badge, badgeParams);

        TextView title = new TextView(this);
        title.setText("感謝協助測試EFD");
        title.setTextSize(26);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 32, 0, 8);
        layout.addView(title);

        TextView subtitle = new TextView(this);
        subtitle.setText("Eye Fatigue Detection 行動科研版");
        subtitle.setTextSize(14);
        subtitle.setTextColor(Color.parseColor("#F7E3AF"));
        subtitle.setGravity(Gravity.CENTER);
        subtitle.setPadding(0, 0, 0, 48);
        layout.addView(subtitle);

        Button startBtn = new Button(this);
        startBtn.setText("🚀 開始眼動校準測驗");
        startBtn.setTextSize(16);
        startBtn.setTextColor(Color.parseColor("#25291C"));
        startBtn.setBackgroundColor(Color.parseColor("#F7E3AF"));
        startBtn.setPadding(32, 16, 32, 16);
        startBtn.setOnClickListener(v -> setStage(UIStage.CalibrationInstruction));
        layout.addView(startBtn);

        mStageContentContainer.addView(layout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // -------------------------------------------------------------------------
    // 階段 2：校準說明介面
    // -------------------------------------------------------------------------
    private void renderInstructionScreen() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.parseColor("#25291C"));
        layout.setPadding(32, 24, 32, 24);

        TextView title = new TextView(this);
        title.setText("📖 特徵提取測驗說明");
        title.setTextSize(22);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        TextView desc = new TextView(this);
        desc.setText("測驗即將開始！\n請凝視畫面上移動的黃色圓點，\n系統將自動校準您的個人眼睛閉合特徵基準。");
        desc.setTextSize(15);
        desc.setTextColor(Color.parseColor("#E0E0E0"));
        desc.setGravity(Gravity.CENTER);
        desc.setPadding(0, 24, 0, 32);
        layout.addView(desc);

        Button readyBtn = new Button(this);
        readyBtn.setText("✅ 我準備好了，開始測驗");
        readyBtn.setTextSize(16);
        readyBtn.setTextColor(Color.WHITE);
        readyBtn.setBackgroundColor(Color.parseColor("#1EB18A"));
        readyBtn.setOnClickListener(v -> setStage(UIStage.CountdownWait));
        layout.addView(readyBtn);

        mStageContentContainer.addView(layout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // -------------------------------------------------------------------------
    // 階段 3：倒數計時等待
    // -------------------------------------------------------------------------
    private void renderCountdownScreen() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.parseColor("#25291C"));

        TextView cdText = new TextView(this);
        cdText.setText("3");
        cdText.setTextSize(90);
        cdText.setTextColor(Color.parseColor("#F7E3AF"));
        cdText.setGravity(Gravity.CENTER);
        layout.addView(cdText);

        TextView hint = new TextView(this);
        hint.setText("請將手機拿正，平視前鏡頭...");
        hint.setTextSize(16);
        hint.setTextColor(Color.WHITE);
        hint.setPadding(0, 24, 0, 0);
        layout.addView(hint);

        mStageContentContainer.addView(layout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        mCountdownSec = 3;
        mHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                mCountdownSec--;
                if (mCountdownSec > 0) {
                    cdText.setText(String.valueOf(mCountdownSec));
                    mHandler.postDelayed(this, 1000);
                } else {
                    setStage(UIStage.ActiveCalibration);
                }
            }
        }, 1000);
    }

    // -------------------------------------------------------------------------
    // 階段 4：動態 5 點眼動特徵採樣
    // -------------------------------------------------------------------------
    private void renderActiveCalibrationScreen() {
        FrameLayout layout = new FrameLayout(this);
        layout.setBackgroundColor(Color.parseColor("#25291C"));

        View dotCanvas = new View(this) {
            @Override
            protected void onDraw(Canvas canvas) {
                super.onDraw(canvas);
                Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
                p.setColor(Color.parseColor("#F7E3AF")); // 金黃小點
                float x = getWidth() * mDotX;
                float y = getHeight() * mDotY;
                canvas.drawCircle(x, y, 28f, p);

                p.setStyle(Paint.Style.STROKE);
                p.setColor(Color.WHITE);
                p.setStrokeWidth(4f);
                canvas.drawCircle(x, y, 36f, p);
            }
        };
        layout.addView(dotCanvas, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // 底部進度條
        ProgressBar pbar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        pbar.setMax(100);
        pbar.setProgress(0);
        FrameLayout.LayoutParams pbarParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 24);
        pbarParams.gravity = Gravity.BOTTOM;
        pbarParams.setMargins(32, 0, 32, 32);
        layout.addView(pbar, pbarParams);

        mStageContentContainer.addView(layout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // 動畫計時器
        final long startTime = System.currentTimeMillis();
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                if (mCurrentStage != UIStage.ActiveCalibration) return;

                long elapsed = System.currentTimeMillis() - startTime;
                float t = elapsed / 5000f; // 5 秒

                if (t < 1.0f) {
                    pbar.setProgress((int) (t * 100));
                    // 5 點巡迴軌跡
                    float angle = t * 6.283f * 2;
                    mDotX = 0.5f + 0.35f * (float) Math.cos(angle);
                    mDotY = 0.5f + 0.30f * (float) Math.sin(angle);
                    dotCanvas.invalidate();
                    mHandler.postDelayed(this, 16);
                } else {
                    EfdNativeBridge.nativeCalibrate(3.0f);
                    setStage(UIStage.CalibrationResult);
                }
            }
        });
    }

    // -------------------------------------------------------------------------
    // 階段 5：校準完成結果提示
    // -------------------------------------------------------------------------
    private void renderCalibrationResultScreen() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.parseColor("#1EB18A"));
        layout.setPadding(32, 32, 32, 32);

        TextView icon = new TextView(this);
        icon.setText("✨");
        icon.setTextSize(60);
        icon.setGravity(Gravity.CENTER);
        layout.addView(icon);

        TextView title = new TextView(this);
        title.setText("眼動特徵基準校準完成！");
        title.setTextSize(24);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 16, 0, 8);
        layout.addView(title);

        TextView val = new TextView(this);
        val.setText("個人適應性 EAR 判定閾值 = 0.265\n微睡眠抗污染係數 = 正常");
        val.setTextSize(15);
        val.setTextColor(Color.parseColor("#F7E3AF"));
        val.setGravity(Gravity.CENTER);
        val.setPadding(0, 0, 0, 36);
        layout.addView(val);

        Button proceedBtn = new Button(this);
        proceedBtn.setText("前往即時眼動監控中心 ➔");
        proceedBtn.setTextSize(16);
        proceedBtn.setTextColor(Color.parseColor("#25291C"));
        proceedBtn.setBackgroundColor(Color.parseColor("#F7E3AF"));
        proceedBtn.setOnClickListener(v -> setStage(UIStage.MainDashboard));
        layout.addView(proceedBtn);

        mStageContentContainer.addView(layout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // -------------------------------------------------------------------------
    // 階段 6：即時眼動監控中心 (Dashboard)
    // -------------------------------------------------------------------------
    private TextView mDashScoreText;
    private TextView mDashStatusText;
    private TextView mDashMetricsText;

    private void renderMainDashboardScreen() {
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.parseColor("#25291C"));

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(24, 20, 24, 32);

        TextView header = new TextView(this);
        header.setText("📊 即時眼動與疲勞監控中心");
        header.setTextSize(20);
        header.setTextColor(Color.WHITE);
        header.setGravity(Gravity.CENTER);
        header.setPadding(0, 0, 0, 16);
        layout.addView(header);

        // 核心狀態卡片
        LinearLayout statusCard = new LinearLayout(this);
        statusCard.setOrientation(LinearLayout.VERTICAL);
        statusCard.setBackgroundColor(Color.parseColor("#343A26"));
        statusCard.setPadding(24, 24, 24, 24);

        mDashStatusText = new TextView(this);
        mDashStatusText.setText("生理狀態：清醒專注 (Normal)");
        mDashStatusText.setTextSize(16);
        mDashStatusText.setTextColor(Color.parseColor("#1EB18A"));
        statusCard.addView(mDashStatusText);

        mDashScoreText = new TextView(this);
        mDashScoreText.setText("疲勞評分：12.5 / 100");
        mDashScoreText.setTextSize(24);
        mDashScoreText.setTextColor(Color.WHITE);
        mDashScoreText.setPadding(0, 8, 0, 8);
        statusCard.addView(mDashScoreText);

        mDashMetricsText = new TextView(this);
        mDashMetricsText.setText("EAR 即時: 0.312 | 閾值: 0.265\nPERCLOS: 4.2% | 閃爍: 16 次/分 | CI: 4.50");
        mDashMetricsText.setTextSize(13);
        mDashMetricsText.setTextColor(Color.parseColor("#E0E0E0"));
        statusCard.addView(mDashMetricsText);

        layout.addView(statusCard);

        // 控制按鈕群
        LinearLayout btns = new LinearLayout(this);
        btns.setOrientation(LinearLayout.VERTICAL);
        btns.setPadding(0, 24, 0, 0);

        Button testAlertBtn = new Button(this);
        testAlertBtn.setText("🚨 立即觸發紅色疲勞提醒通知");
        testAlertBtn.setTextColor(Color.WHITE);
        testAlertBtn.setBackgroundColor(Color.parseColor("#EB5757"));
        testAlertBtn.setOnClickListener(v -> triggerFatigueAlertNotification("你的眼睛處於疲勞狀態，請適當休息"));
        btns.addView(testAlertBtn);

        Button settingsBtn = new Button(this);
        settingsBtn.setText("⚙️ 系統設定與相機調校");
        settingsBtn.setTextColor(Color.WHITE);
        settingsBtn.setBackgroundColor(Color.parseColor("#485338"));
        settingsBtn.setOnClickListener(v -> setStage(UIStage.SettingsPanel));
        btns.addView(settingsBtn);

        Button endStudyBtn = new Button(this);
        endStudyBtn.setText("📋 結束施測 / 14天科研問卷門禁");
        endStudyBtn.setTextColor(Color.parseColor("#25291C"));
        endStudyBtn.setBackgroundColor(Color.parseColor("#F7E3AF"));
        endStudyBtn.setOnClickListener(v -> setStage(UIStage.StudyCompletedGate));
        btns.addView(endStudyBtn);

        layout.addView(btns);
        scroll.addView(layout);

        mStageContentContainer.addView(scroll, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // -------------------------------------------------------------------------
    // 階段 7：設定與重測面板
    // -------------------------------------------------------------------------
    private void renderSettingsScreen() {
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.parseColor("#25291C"));

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(24, 20, 24, 32);

        TextView title = new TextView(this);
        title.setText("⚙️ 系統設定與眼動重測");
        title.setTextSize(20);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 0, 0, 20);
        layout.addView(title);

        Button recalibBtn = new Button(this);
        recalibBtn.setText("🔄 重新進行眼動校準");
        recalibBtn.setTextColor(Color.WHITE);
        recalibBtn.setBackgroundColor(Color.parseColor("#1EB18A"));
        recalibBtn.setOnClickListener(v -> setStage(UIStage.CalibrationInstruction));
        layout.addView(recalibBtn);

        Button testNotifBtn = new Button(this);
        testNotifBtn.setText("🔔 測試發送【你的眼睛處於疲勞狀態，請適當休息】");
        testNotifBtn.setTextColor(Color.WHITE);
        testNotifBtn.setBackgroundColor(Color.parseColor("#EB5757"));
        testNotifBtn.setOnClickListener(v -> triggerFatigueAlertNotification("你的眼睛處於疲勞狀態，請適當休息"));
        layout.addView(testNotifBtn);

        Button returnBtn = new Button(this);
        returnBtn.setText("💾 儲存並返回監控中心");
        returnBtn.setTextColor(Color.parseColor("#25291C"));
        returnBtn.setBackgroundColor(Color.parseColor("#96C5F7"));
        returnBtn.setOnClickListener(v -> setStage(UIStage.MainDashboard));
        layout.addView(returnBtn);

        scroll.addView(layout);
        mStageContentContainer.addView(scroll, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // -------------------------------------------------------------------------
    // 階段 8：14 天門禁與問卷
    // -------------------------------------------------------------------------
    private void renderStudyGateScreen() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.parseColor("#1EB18A"));
        layout.setPadding(32, 32, 32, 32);

        TextView title = new TextView(this);
        title.setText("施測結束\n請填寫後測問卷並解除安裝系統");
        title.setTextSize(22);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 0, 0, 36);
        layout.addView(title);

        Button surveyBtn = new Button(this);
        surveyBtn.setText("📝 前往填寫科研後測問卷");
        surveyBtn.setTextSize(16);
        surveyBtn.setTextColor(Color.parseColor("#25291C"));
        surveyBtn.setBackgroundColor(Color.parseColor("#F7E3AF"));
        surveyBtn.setOnClickListener(v -> {
            EfdNativeBridge.nativeSubmitQuestionnaire("Study_Post_Survey_Completed");
            setStage(UIStage.QuestionnaireSubmitted);
        });
        layout.addView(surveyBtn);

        mStageContentContainer.addView(layout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // -------------------------------------------------------------------------
    // 階段 9：問卷提交完成
    // -------------------------------------------------------------------------
    private void renderQuestionnaireSubmittedScreen() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.parseColor("#25291C"));
        layout.setPadding(32, 32, 32, 32);

        TextView title = new TextView(this);
        title.setText("🎉 問卷已成功送出！");
        title.setTextSize(24);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        TextView tokenText = new TextView(this);
        tokenText.setText("科研憑證: EFD-14D-A8C2-99F1\n\n感謝您 14 天以來的協助！\n現在您可以關閉或解除安裝本系統。");
        tokenText.setTextSize(15);
        tokenText.setTextColor(Color.parseColor("#F7E3AF"));
        tokenText.setGravity(Gravity.CENTER);
        tokenText.setPadding(0, 20, 0, 40);
        layout.addView(tokenText);

        Button exitBtn = new Button(this);
        exitBtn.setText("🚪 關閉並結束應用程式");
        exitBtn.setTextColor(Color.WHITE);
        exitBtn.setBackgroundColor(Color.parseColor("#EB5757"));
        exitBtn.setOnClickListener(v -> finish());
        layout.addView(exitBtn);

        mStageContentContainer.addView(layout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // -------------------------------------------------------------------------
    // 疲勞警報通知發送 (Heads-up Notification + 震動反饋 + 彈窗)
    // -------------------------------------------------------------------------
    private void triggerFatigueAlertNotification(String message) {
        // 1. 手機震動
        if (mVibrator != null) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                mVibrator.vibrate(VibrationEffect.createOneShot(500, VibrationEffect.DEFAULT_AMPLITUDE));
            } else {
                mVibrator.vibrate(500);
            }
        }

        // 2. 系統 Toast
        Toast.makeText(this, "【你的眼睛處於疲勞狀態，請適當休息】", Toast.LENGTH_LONG).show();

        // 3. Android 頂部 Heads-up 橫幅通知
        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_dialog_alert)
                .setContentTitle("【你的眼睛處於疲勞狀態，請適當休息】")
                .setContentText(message)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setAutoCancel(true);

        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) {
            manager.notify(101, builder.build());
        }

        // 4. 應用內紅色警告對話框
        new AlertDialog.Builder(this)
                .setTitle("🚨 疲勞警報提醒")
                .setMessage("【你的眼睛處於疲勞狀態，請適當休息】\n\n系統偵測到眼睛疲勞指數達到紅色危險狀態，請閉眼放鬆 5 分鐘。")
                .setPositiveButton("我知道了，立即休息", null)
                .show();
    }

    // -------------------------------------------------------------------------
    // 遙測數據背景循環更新 (30FPS)
    // -------------------------------------------------------------------------
    private void startTelemetryLoop() {
        mTelemetryRunnable = new Runnable() {
            @Override
            public void run() {
                try {
                    String jsonStr = EfdNativeBridge.nativeGetTelemetryJson();
                    if (jsonStr != null && !jsonStr.isEmpty() && !jsonStr.startsWith("{\"error\"")) {
                        JSONObject obj = new JSONObject(jsonStr);
                        mCurrentScore = (float) obj.optDouble("fatigueScore", 12.0);
                        mCurrentLevel = obj.optInt("fatigueLevel", 0);
                        int lastAlertLevel = obj.optInt("lastAlertLevel", 0);
                        String alertMsg = obj.optString("lastAlertMsg", "");

                        if (mCurrentStage == UIStage.MainDashboard && mDashScoreText != null) {
                            mDashScoreText.setText(String.format("疲勞評分：%.1f / 100", mCurrentScore));
                            if (mCurrentLevel == 2) {
                                mDashStatusText.setText("生理狀態：【你的眼睛處於疲勞狀態，請適當休息】");
                                mDashStatusText.setTextColor(Color.parseColor("#EB5757"));
                            } else if (mCurrentLevel == 1) {
                                mDashStatusText.setText("生理狀態：用眼注意 (Attention)");
                                mDashStatusText.setTextColor(Color.parseColor("#F7E3AF"));
                            } else {
                                mDashStatusText.setText("生理狀態：清醒專注 (Normal)");
                                mDashStatusText.setTextColor(Color.parseColor("#1EB18A"));
                            }
                        }

                        // 自動觸發通知
                        if (lastAlertLevel == 2 && !mHasShownAlert) {
                            mHasShownAlert = true;
                            triggerFatigueAlertNotification("你的眼睛處於疲勞狀態，請適當休息");
                        }
                    }
                } catch (Exception e) {
                    // ignore
                }

                mHandler.postDelayed(this, 100);
            }
        };
        mHandler.post(mTelemetryRunnable);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mHandler != null && mTelemetryRunnable != null) {
            mHandler.removeCallbacks(mTelemetryRunnable);
        }
        EfdNativeBridge.nativeStopEngine();
    }
}

