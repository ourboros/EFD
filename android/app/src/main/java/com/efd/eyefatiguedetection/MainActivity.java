package com.efd.eyefatiguedetection;

import android.Manifest;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
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
import android.util.Log;
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

@android.annotation.SuppressLint({"NewApi", "MissingPermission", "DiscouragedApi"})
public class MainActivity extends AppCompatActivity {

    private static final String TAG = "EFD_MainActivity";

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
    private float mDotX = 0.5f;
    private float mDotY = 0.5f;
    private int mCountdownSec = 3;

    // 即時遙測數據
    private float mCurrentScore = 0.0f;
    private int mCurrentLevel = 0;
    private float mLastThreshold = 0.0f;
    private boolean mLastCalibrationOk = false;
    private boolean mIsResumed = false;
    private EfdScreenStateReceiver mScreenStateReceiver;

    private static final String CHANNEL_ID = "efd_fatigue_alerts";
    private static final int PERMISSION_REQ_CODE = 1001;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        mHandler = new Handler(Looper.getMainLooper());

        try {
            mVibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        } catch (Throwable t) {
            Log.w(TAG, "Vibrator service not available: " + t.getMessage());
        }

        // 1. 建立純程式化響應式 UI
        setupViews();

        // 2. 初始化通知通道 (防禦性調用)
        try {
            createNotificationChannel();
        } catch (Throwable t) {
            Log.w(TAG, "Failed to create notification channel: " + t.getMessage());
        }

        // 3. 偵測引擎由 EfdForegroundService 擁有並初始化 (Activity 被劃掉時引擎不受影響)
        //    取得相機權限後才啟動服務：Android 14+ 未授權即啟動相機類型前景服務會閃退
        // 4. 延遲 500ms 請求權限，避免阻礙主視窗渲染啟動
        mHandler.postDelayed(this::requestAppPermissions, 500);

        // 5. 註冊螢幕鎖屏與解鎖廣播監聽器
        try {
            mScreenStateReceiver = new EfdScreenStateReceiver();
            mScreenStateReceiver.register(this);
        } catch (Throwable t) {
            Log.w(TAG, "Failed to register screen state receiver: " + t.getMessage());
        }

        // 5.5 已有相機權限時立即啟動前景監控服務 (否則等權限授予後再啟動)
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            startForegroundMonitoringService();
        }

        // 6. 啟動遙測更新輪詢 (onResume 會重新啟動，onPause 暫停)
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

    private boolean isActivityDestroyed() {
        if (isFinishing()) return true;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
            return isDestroyed();
        }
        return false;
    }

    private void requestAppPermissions() {
        if (isActivityDestroyed()) return;

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED ||
                    ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                    ActivityCompat.requestPermissions(this,
                            new String[]{Manifest.permission.CAMERA, Manifest.permission.POST_NOTIFICATIONS},
                            PERMISSION_REQ_CODE);
                } else {
                    startForegroundMonitoringService();
                }
            } else {
                if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
                    ActivityCompat.requestPermissions(this,
                            new String[]{Manifest.permission.CAMERA},
                            PERMISSION_REQ_CODE);
                } else {
                    startForegroundMonitoringService();
                }
            }
        } catch (Throwable t) {
            Log.w(TAG, "Permission request failed: " + t.getMessage());
        }
    }

    private void startForegroundMonitoringService() {
        try {
            // 使用者自行開啟 App：恢復偵測 (清除先前從通知「停止偵測」的狀態)
            EfdForegroundService.setStoppedByUser(this, false);
            Intent serviceIntent = new Intent(this, EfdForegroundService.class);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(serviceIntent);
            } else {
                startService(serviceIntent);
            }
            Log.i(TAG, "前台疲勞監控服務 EfdForegroundService 已成功啟動");
        } catch (Throwable t) {
            Log.w(TAG, "Failed to start foreground service: " + t.getMessage());
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == PERMISSION_REQ_CODE) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                Log.i(TAG, "相機權限授權成功，立即開啟前台監控服務與鏡頭串流");
                startForegroundMonitoringService();
            } else {
                Log.w(TAG, "相機權限遭拒絕，無法開啟前置鏡頭");
                Toast.makeText(this, "請給予相機權限以進行疲勞檢測", Toast.LENGTH_LONG).show();
            }
            // 通知權限被拒絕時，背景疲勞提醒會被系統丟棄，立即說明
            if (!EfdNotificationHudManager.canPostFatigueAlerts(this)) {
                mNotificationCheckShown = true;
                showNotificationsDisabledDialog();
            }
        }
    }

    @SuppressWarnings("deprecation")
    private void styleButton(Button btn, String bgColorHex, String textColorHex) {
        if (btn == null) return;
        btn.setTypeface(null, android.graphics.Typeface.BOLD);
        btn.setTextColor(Color.parseColor(textColorHex));
        android.graphics.drawable.GradientDrawable shape = new android.graphics.drawable.GradientDrawable();
        shape.setShape(android.graphics.drawable.GradientDrawable.RECTANGLE);
        shape.setColor(Color.parseColor(bgColorHex));
        float density = getResources().getDisplayMetrics().density;
        shape.setCornerRadius(20f * density); // 20px 圓角邊框
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN) {
            btn.setBackground(shape);
        } else {
            btn.setBackgroundDrawable(shape);
        }
    }

    private void setupViews() {
        mMainContainer = new LinearLayout(this);
        mMainContainer.setOrientation(LinearLayout.VERTICAL);
        mMainContainer.setBackgroundColor(Color.parseColor("#25291C")); // EFD 深黑墨綠背景

        // 階段內容容器 (不繪製頂部導覽列，純靠介面流程按鈕切換)
        mStageContentContainer = new FrameLayout(this);
        LinearLayout.LayoutParams contentParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.0f);
        mMainContainer.addView(mStageContentContainer, contentParams);

        setContentView(mMainContainer);

        // 讀取是否曾進行過監測，若已監測過則直接進入即時監控中心
        SharedPreferences prefs = getSharedPreferences("efd_prefs", MODE_PRIVATE);
        if (prefs.getBoolean("has_calibrated", false)) {
            mCurrentStage = UIStage.MainDashboard;
        } else {
            mCurrentStage = UIStage.Welcome;
        }

        // 渲染初始頁面
        renderCurrentStage();
    }

    public void setStage(UIStage stage) {
        mCurrentStage = stage;
        if (stage == UIStage.MainDashboard || stage == UIStage.SettingsPanel) {
            getSharedPreferences("efd_prefs", MODE_PRIVATE)
                    .edit().putBoolean("has_calibrated", true).apply();
        }
        updateNavBarHighlight();
        renderCurrentStage();
        if (stage == UIStage.MainDashboard) {
            requestIgnoreBatteryOptimizationsOnce();
            checkNotificationsEnabledOnce();
        }
    }

    /**
     * 小米、OPPO、vivo、華為等廠牌會在 App 被劃掉後強制關閉背景服務；
     * 說明後開啟系統的電池最佳化清單，由使用者自行將 EFD 設為不受限制 (只詢問一次)。
     * 不使用 ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS：它需要敏感權限，會讓 Play Protect 視為可疑。
     */
    private void requestIgnoreBatteryOptimizationsOnce() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return;
        SharedPreferences prefs = getSharedPreferences("efd_prefs", MODE_PRIVATE);
        if (prefs.getBoolean("asked_battery_optimization", false)) return;
        prefs.edit().putBoolean("asked_battery_optimization", true).apply();
        try {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm == null || pm.isIgnoringBatteryOptimizations(getPackageName())) return;
        } catch (Throwable t) {
            return;
        }
        new AlertDialog.Builder(this)
                .setTitle("讓背景偵測不被系統中斷")
                .setMessage("部分手機會為了省電關閉背景執行的 App，導致把 EFD 劃掉後就不再提醒。\n\n"
                        + "接下來會開啟「電池最佳化」清單，請找到「EFD 疲勞檢測」並設為「不最佳化」或「不受限制」。")
                .setPositiveButton("前往設定", (d, w) -> openBatteryOptimizationSettings())
                .setNegativeButton("稍後", null)
                .show();
    }

    private void openBatteryOptimizationSettings() {
        try {
            startActivity(new Intent(android.provider.Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS));
        } catch (Throwable t) {
            openAppDetailsSettings();
        }
    }

    private void setupTopNavBar() {
        HorizontalScrollView scroll = new HorizontalScrollView(this);
        scroll.setBackgroundColor(Color.parseColor("#181C12"));
        scroll.setHorizontalScrollBarEnabled(false);

        mNavBarContainer = new LinearLayout(this);
        mNavBarContainer.setOrientation(LinearLayout.HORIZONTAL);
        mNavBarContainer.setPadding(12, 10, 12, 10);

        String[] labels = {
                "歡迎", "說明", "倒數", "測驗",
                "結果", "監控", "設定", "後測", "問卷"
        };
        UIStage[] stages = UIStage.values();

        for (int i = 0; i < stages.length; i++) {
            final UIStage stage = stages[i];
            Button btn = new Button(this);
            btn.setText(labels[i]);
            btn.setTextSize(11);
            btn.setPadding(18, 6, 18, 6);
            boolean isActive = (stage == mCurrentStage);
            styleButton(btn, isActive ? "#1EB18A" : "#343A26", isActive ? "#FFFFFF" : "#F7E3AF");

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

    private void updateNavBarHighlight() {
        if (mNavBarContainer == null) return;
        UIStage[] stages = UIStage.values();
        for (int i = 0; i < mNavBarContainer.getChildCount() && i < stages.length; i++) {
            View child = mNavBarContainer.getChildAt(i);
            if (child instanceof Button) {
                Button btn = (Button) child;
                boolean isActive = (stages[i] == mCurrentStage);
                styleButton(btn, isActive ? "#1EB18A" : "#343A26", isActive ? "#FFFFFF" : "#F7E3AF");
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

        // 載入資產 10 (資產 10.png) 圖片 - 縮小比例，透明無邊框
        android.widget.ImageView logoImage = new android.widget.ImageView(this);
        int logoResId = getResources().getIdentifier("logo10", "drawable", getPackageName());
        if (logoResId != 0) {
            logoImage.setImageResource(logoResId);
        }
        logoImage.setAdjustViewBounds(true);
        logoImage.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);

        int logoWidth = 90; // 縮小 50% 適合手機螢幕置中的 Logo 寬度
        LinearLayout.LayoutParams logoParams = new LinearLayout.LayoutParams(
                logoWidth, ViewGroup.LayoutParams.WRAP_CONTENT);
        logoParams.gravity = Gravity.CENTER;
        logoParams.setMargins(0, 16, 0, 24);
        layout.addView(logoImage, logoParams);

        TextView title = new TextView(this);
        title.setText("感謝協助測試EFD");
        title.setTextSize(26);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 32, 0, 8);
        layout.addView(title);

        TextView subtitle = new TextView(this);
        subtitle.setText("Eye Fatigue Detection 行動科研版");
        subtitle.setTextSize(14);
        subtitle.setTypeface(null, android.graphics.Typeface.BOLD);
        subtitle.setTextColor(Color.parseColor("#F7E3AF"));
        subtitle.setGravity(Gravity.CENTER);
        subtitle.setPadding(0, 0, 0, 48);
        layout.addView(subtitle);

        Button startBtn = new Button(this);
        startBtn.setText("開始眼動校準測驗");
        startBtn.setTextSize(16);
        startBtn.setPadding(32, 16, 32, 16);
        styleButton(startBtn, "#F7E3AF", "#25291C");
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
        title.setText("特徵提取測驗說明");
        title.setTextSize(22);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        TextView desc = new TextView(this);
        desc.setText("測驗即將開始！\n請凝視畫面上移動的黃色圓點，\n系統將自動校準您的個人眼睛閉合特徵基準。");
        desc.setTextSize(15);
        desc.setTypeface(null, android.graphics.Typeface.BOLD);
        desc.setTextColor(Color.parseColor("#E0E0E0"));
        desc.setGravity(Gravity.CENTER);
        desc.setPadding(0, 24, 0, 32);
        layout.addView(desc);

        Button readyBtn = new Button(this);
        readyBtn.setText("我準備好了，開始測驗");
        readyBtn.setTextSize(16);
        readyBtn.setPadding(32, 16, 32, 16);
        styleButton(readyBtn, "#1EB18A", "#FFFFFF");
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
        cdText.setTypeface(null, android.graphics.Typeface.BOLD);
        cdText.setTextColor(Color.parseColor("#F7E3AF"));
        cdText.setGravity(Gravity.CENTER);
        layout.addView(cdText);

        TextView hint = new TextView(this);
        hint.setText("請將手機拿正，平視前鏡頭...");
        hint.setTextSize(16);
        hint.setTypeface(null, android.graphics.Typeface.BOLD);
        hint.setTextColor(Color.WHITE);
        hint.setGravity(Gravity.CENTER);
        hint.setPadding(0, 24, 0, 0);
        layout.addView(hint);

        mStageContentContainer.addView(layout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        mCountdownSec = 3;
        mHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (isActivityDestroyed()) return;
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
                if (isActivityDestroyed() || mCurrentStage != UIStage.ActiveCalibration) return;

                long elapsed = System.currentTimeMillis() - startTime;
                float t = elapsed / 5000f; // 5 秒

                if (t < 1.0f) {
                    pbar.setProgress((int) (t * 100));
                    float angle = t * 6.283f * 2;
                    mDotX = 0.5f + 0.35f * (float) Math.cos(angle);
                    mDotY = 0.5f + 0.30f * (float) Math.sin(angle);
                    dotCanvas.invalidate();
                    mHandler.postDelayed(this, 16);
                } else {
                    // 只採用校準期間有偵測到人臉的幀；不足 60% 則校準失敗、保留原基準
                    mLastCalibrationOk = EfdNativeBridge.finishCalibration();
                    if (mLastCalibrationOk) {
                        getSharedPreferences("efd_prefs", MODE_PRIVATE)
                                .edit().putBoolean("has_calibrated", true).apply();
                    }
                    setStage(UIStage.CalibrationResult);
                }
            }
        });
        EfdNativeBridge.beginCalibration();
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

        TextView title = new TextView(this);
        title.setText(mLastCalibrationOk ? "眼動特徵基準校準完成！" : "校準未完成");
        title.setTextSize(24);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 16, 0, 8);
        layout.addView(title);

        TextView val = new TextView(this);
        if (mLastCalibrationOk) {
            val.setText(String.format(java.util.Locale.getDefault(),
                    "個人適應性 EAR 判定閾值 = %.3f\n下次開啟將直接沿用此基準", mLastThreshold));
        } else if (!EfdNativeBridge.isEngineRunning()) {
            val.setText("偵測引擎尚未啟動，請確認已允許相機權限後重新校準");
        } else {
            val.setText("校準期間未穩定偵測到臉部\n請在光線充足處正對鏡頭後重新校準");
        }
        val.setTextSize(15);
        val.setTypeface(null, android.graphics.Typeface.BOLD);
        val.setTextColor(Color.parseColor("#F7E3AF"));
        val.setGravity(Gravity.CENTER);
        val.setPadding(0, 0, 0, 36);
        layout.addView(val);

        if (!mLastCalibrationOk) {
            Button retryBtn = new Button(this);
            retryBtn.setText("重新校準");
            retryBtn.setTextSize(15);
            retryBtn.setPadding(24, 14, 24, 14);
            styleButton(retryBtn, "#FFFFFF", "#25291C");
            retryBtn.setOnClickListener(v -> setStage(UIStage.CalibrationInstruction));
            LinearLayout.LayoutParams retryLp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            retryLp.setMargins(0, 0, 0, 12);
            retryBtn.setLayoutParams(retryLp);
            layout.addView(retryBtn);
        }

        Button proceedBtn = new Button(this);
        proceedBtn.setText(mLastCalibrationOk ? "進入即時眼動監控中心" : "略過，先使用預設基準");
        proceedBtn.setTextSize(15);
        proceedBtn.setPadding(24, 14, 24, 14);
        styleButton(proceedBtn, "#F7E3AF", "#25291C");
        proceedBtn.setOnClickListener(v -> setStage(UIStage.MainDashboard));
        layout.addView(proceedBtn);

        // 新增按鈕：數據提取後關閉系統介面 (背景持續監控)
        Button closeBgBtn = new Button(this);
        closeBgBtn.setText("關閉系統介面");
        closeBgBtn.setTextSize(14);
        closeBgBtn.setPadding(24, 12, 24, 12);
        styleButton(closeBgBtn, "#FFFFFF", "#1EB18A");
        LinearLayout.LayoutParams closeLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        closeLp.setMargins(0, 12, 0, 0);
        closeBgBtn.setLayoutParams(closeLp);
        closeBgBtn.setOnClickListener(v -> {
            Toast.makeText(this, "EFD 已轉入背景監控，僅於疲勞超標時跳出提醒", Toast.LENGTH_LONG).show();
            moveTaskToBack(true);
        });
        layout.addView(closeBgBtn);

        mStageContentContainer.addView(layout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // -------------------------------------------------------------------------
    // 階段 6：即時眼動監控中心 (Dashboard)
    // -------------------------------------------------------------------------
    private TextView mDashScoreText;
    private TextView mDashStatusText;
    private TextView mDashMetricsText;
    private boolean mPreviewBoundToService = false;

    private void renderMainDashboardScreen() {
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.parseColor("#25291C"));

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(24, 20, 24, 32);

        TextView header = new TextView(this);
        header.setText("即時眼動與疲勞監控中心");
        header.setTextSize(20);
        header.setTypeface(null, android.graphics.Typeface.BOLD);
        header.setTextColor(Color.WHITE);
        header.setGravity(Gravity.CENTER);
        header.setPadding(0, 0, 0, 16);
        layout.addView(header);

        // 置中固定比例相機預覽視窗 (280dp 正方形圓角卡片，FILL_CENTER 滿版無黑邊)
        try {
            int boxSize = (int) (280 * getResources().getDisplayMetrics().density);
            FrameLayout cardView = new FrameLayout(this);
            android.graphics.drawable.GradientDrawable cardBg = new android.graphics.drawable.GradientDrawable();
            cardBg.setCornerRadius(24f);
            cardBg.setColor(Color.parseColor("#181C12"));
            cardView.setBackground(cardBg);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                cardView.setClipToOutline(true);
            }
            LinearLayout.LayoutParams cardLp = new LinearLayout.LayoutParams(boxSize, boxSize);
            cardLp.gravity = Gravity.CENTER_HORIZONTAL;
            cardLp.setMargins(0, 0, 0, 20);
            cardView.setLayoutParams(cardLp);

            androidx.camera.view.PreviewView previewView = new androidx.camera.view.PreviewView(this);
            previewView.setScaleType(androidx.camera.view.PreviewView.ScaleType.FILL_CENTER);
            cardView.addView(previewView, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

            layout.addView(cardView);

            // 相機只由前景服務擁有；Activity 不另開相機，避免兩邊對同一個
            // ProcessCameraProvider 呼叫 unbindAll() 而互相中斷影像分析
            EfdForegroundService fgService = EfdForegroundService.getInstance();
            if (fgService != null && fgService.getCameraManager() != null) {
                fgService.getCameraManager().startCameraWithPreview(previewView);
                mPreviewBoundToService = true;
            }
        } catch (Throwable t) {
            Log.w(TAG, "Failed to embed camera preview: " + t.getMessage());
        }

        // 核心狀態卡片
        LinearLayout statusCard = new LinearLayout(this);
        statusCard.setOrientation(LinearLayout.VERTICAL);
        statusCard.setBackgroundColor(Color.parseColor("#343A26"));
        statusCard.setPadding(24, 24, 24, 24);

        TextView subjText = new TextView(this);
        String uuid = EfdNativeBridge.getSubjectUuid();
        int day = EfdNativeBridge.getCurrentDay();
        if (uuid == null || uuid.isEmpty() || day <= 0) {
            subjText.setText("受試者代碼：-- (偵測服務啟動中)");
        } else {
            subjText.setText(String.format(java.util.Locale.getDefault(), "受試者代碼：%s (實驗第 %d / 14 天)", uuid, day));
        }
        subjText.setTextSize(13);
        subjText.setTypeface(null, android.graphics.Typeface.BOLD);
        subjText.setTextColor(Color.parseColor("#F7E3AF"));
        subjText.setPadding(0, 0, 0, 8);
        statusCard.addView(subjText);

        mDashStatusText = new TextView(this);
        mDashStatusText.setText("生理狀態：清醒專注 (Normal)");
        mDashStatusText.setTextSize(16);
        mDashStatusText.setTypeface(null, android.graphics.Typeface.BOLD);
        mDashStatusText.setTextColor(Color.parseColor("#1EB18A"));
        statusCard.addView(mDashStatusText);

        mDashScoreText = new TextView(this);
        mDashScoreText.setText("疲勞評分：12.5 / 100");
        mDashScoreText.setTextSize(24);
        mDashScoreText.setTypeface(null, android.graphics.Typeface.BOLD);
        mDashScoreText.setTextColor(Color.WHITE);
        mDashScoreText.setPadding(0, 8, 0, 8);
        statusCard.addView(mDashScoreText);

        mDashMetricsText = new TextView(this);
        mDashMetricsText.setText("EAR 即時: 0.312 | 閾值: 0.265\nPERCLOS: 4.2% | 閃爍: 16 次/分 | CI: 4.50");
        mDashMetricsText.setTextSize(13);
        mDashMetricsText.setTypeface(null, android.graphics.Typeface.BOLD);
        mDashMetricsText.setTextColor(Color.parseColor("#E0E0E0"));
        statusCard.addView(mDashMetricsText);

        layout.addView(statusCard);

        // 控制按鈕群 (僅保留：進入設定介面 與 關閉系統介面)
        LinearLayout btns = new LinearLayout(this);
        btns.setOrientation(LinearLayout.VERTICAL);
        btns.setPadding(0, 20, 0, 0);

        Button settingsBtn = new Button(this);
        settingsBtn.setText("進入設定介面");
        settingsBtn.setPadding(16, 14, 16, 14);
        styleButton(settingsBtn, "#1EB18A", "#FFFFFF");
        settingsBtn.setOnClickListener(v -> setStage(UIStage.SettingsPanel));
        btns.addView(settingsBtn);

        Button closeBgBtn = new Button(this);
        closeBgBtn.setText("關閉系統介面");
        closeBgBtn.setPadding(16, 14, 16, 14);
        styleButton(closeBgBtn, "#F7E3AF", "#25291C");
        LinearLayout.LayoutParams closeLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        closeLp.setMargins(0, 12, 0, 0);
        closeBgBtn.setLayoutParams(closeLp);
        closeBgBtn.setOnClickListener(v -> {
            Toast.makeText(this, "EFD 已轉入背景監控，僅於疲勞超標時跳出提醒", Toast.LENGTH_LONG).show();
            moveTaskToBack(true);
        });
        btns.addView(closeBgBtn);

        layout.addView(btns);
        scroll.addView(layout);

        mStageContentContainer.addView(scroll, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // -------------------------------------------------------------------------
    // 階段 7：設定面板 (僅保留：提示靈敏度、重設眼動監測、警告通知發送)
    // -------------------------------------------------------------------------
    private int mSensitivityLevel = 1; // 0: 低, 1: 標準, 2: 高 (實際值保存在 C++ 設定檔)

    private void renderSettingsScreen() {
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.parseColor("#25291C"));

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(24, 20, 24, 32);

        TextView title = new TextView(this);
        title.setText("系統設定");
        title.setTextSize(20);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 0, 0, 20);
        layout.addView(title);

        // 項目 1: 提示靈敏度
        // 只調整提醒門檻與持續秒數，20/5/5 冷卻規則不變
        final String[] sensNames = {
                "低靈敏度 (≥70 分持續 15 秒)", "標準靈敏度 (≥65 分持續 10 秒，推薦)", "高靈敏度 (≥58 分持續 5 秒)" };
        Button sensBtn = new Button(this);
        int savedLevel = EfdNativeBridge.getSensitivity();
        final boolean engineReady = savedLevel >= 0;
        if (engineReady) {
            mSensitivityLevel = savedLevel;
            sensBtn.setText("提示靈敏度: " + sensNames[mSensitivityLevel] + " (點擊切換)");
        } else {
            sensBtn.setText("提示靈敏度: 請先開始監測後再調整");
        }
        styleButton(sensBtn, "#343A26", "#F7E3AF");
        sensBtn.setEnabled(engineReady);
        sensBtn.setOnClickListener(v -> {
            mSensitivityLevel = (mSensitivityLevel + 1) % 3;
            EfdNativeBridge.setSensitivity(mSensitivityLevel);
            sensBtn.setText("提示靈敏度: " + sensNames[mSensitivityLevel] + " (點擊切換)");
        });
        layout.addView(sensBtn);

        // 項目 2: 重設眼動監測
        Button recalibBtn = new Button(this);
        recalibBtn.setText("重設眼動監測");
        styleButton(recalibBtn, "#1EB18A", "#FFFFFF");
        LinearLayout.LayoutParams recalibLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        recalibLp.setMargins(0, 12, 0, 0);
        recalibBtn.setLayoutParams(recalibLp);
        recalibBtn.setOnClickListener(v -> setStage(UIStage.CalibrationInstruction));
        layout.addView(recalibBtn);

        // 項目 3: 測試背景通知 (與真實疲勞提醒同一路徑：C++ 引擎發出警報 → 前景服務 → 通知)
        Button testAlertBtn = new Button(this);
        testAlertBtn.setText("測試背景通知（10 秒後發送）");
        styleButton(testAlertBtn, "#343A26", "#F7E3AF");
        LinearLayout.LayoutParams testLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        testLp.setMargins(0, 12, 0, 0);
        testAlertBtn.setLayoutParams(testLp);
        testAlertBtn.setOnClickListener(v -> onTestBackgroundAlertClicked());
        layout.addView(testAlertBtn);

        // 項目 4: 通知與背景執行設定 (依手機廠牌說明)
        Button bgSettingsBtn = new Button(this);
        bgSettingsBtn.setText("通知與背景執行設定");
        styleButton(bgSettingsBtn, "#343A26", "#F7E3AF");
        LinearLayout.LayoutParams bgLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        bgLp.setMargins(0, 12, 0, 0);
        bgSettingsBtn.setLayoutParams(bgLp);
        bgSettingsBtn.setOnClickListener(v -> showBackgroundSettingsGuide());
        layout.addView(bgSettingsBtn);

        // 項目 5: 匯出診斷畫面 (抓不到臉時，回傳 C++ 實際分析的畫面與數值)
        Button diagBtn = new Button(this);
        diagBtn.setText("匯出診斷畫面（存到相簿＋複製數值）");
        styleButton(diagBtn, "#343A26", "#F7E3AF");
        LinearLayout.LayoutParams diagLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        diagLp.setMargins(0, 12, 0, 0);
        diagBtn.setLayoutParams(diagLp);
        diagBtn.setOnClickListener(v -> onExportDiagnosticClicked());
        layout.addView(diagBtn);

        // 返回按鈕
        Button returnBtn = new Button(this);
        returnBtn.setText("返回監控中心");
        styleButton(returnBtn, "#96C5F7", "#25291C");
        LinearLayout.LayoutParams retLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        retLp.setMargins(0, 24, 0, 0);
        returnBtn.setLayoutParams(retLp);
        returnBtn.setOnClickListener(v -> setStage(UIStage.MainDashboard));
        layout.addView(returnBtn);

        scroll.addView(layout);
        mStageContentContainer.addView(scroll, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // -------------------------------------------------------------------------
    // 階段 8：後測介面 (問卷連結與狀態解鎖)
    // -------------------------------------------------------------------------
    private void renderStudyGateScreen() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.parseColor("#1EB18A"));
        layout.setPadding(32, 32, 32, 32);

        TextView title = new TextView(this);
        title.setText("後測介面");
        title.setTextSize(24);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 0, 0, 16);
        layout.addView(title);

        TextView urlText = new TextView(this);
        urlText.setText("施測已結束，請點擊下方按鈕前往填寫後測問卷以完成實驗流程\n問卷網址: https://forms.gle/y5f1jTnrrtsxz65G9");
        urlText.setTextSize(14);
        urlText.setTypeface(null, android.graphics.Typeface.BOLD);
        urlText.setTextColor(Color.parseColor("#F7E3AF"));
        urlText.setGravity(Gravity.CENTER);
        urlText.setPadding(0, 0, 0, 32);
        layout.addView(urlText);

        Button surveyBtn = new Button(this);
        surveyBtn.setText("前往填寫後測問卷 (Google 表單)");
        surveyBtn.setTextSize(15);
        surveyBtn.setPadding(24, 14, 24, 14);
        styleButton(surveyBtn, "#F7E3AF", "#25291C");
        surveyBtn.setOnClickListener(v -> {
            try {
                android.content.Intent intent = new android.content.Intent(
                        android.content.Intent.ACTION_VIEW,
                        android.net.Uri.parse("https://forms.gle/y5f1jTnrrtsxz65G9"));
                startActivity(intent);
            } catch (Throwable t) {
                Log.w(TAG, "Error opening survey URL: " + t.getMessage());
            }
            EfdNativeBridge.submitQuestionnaire("Study_Post_Survey_Completed");
            setStage(UIStage.QuestionnaireSubmitted);
        });
        layout.addView(surveyBtn);

        Button returnBtn = new Button(this);
        returnBtn.setText("返回即時監控中心");
        returnBtn.setTextSize(14);
        returnBtn.setPadding(20, 10, 20, 10);
        styleButton(returnBtn, "#343A26", "#F7E3AF");
        LinearLayout.LayoutParams retLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        retLp.setMargins(0, 16, 0, 0);
        retLp.gravity = Gravity.CENTER;
        returnBtn.setLayoutParams(retLp);
        returnBtn.setOnClickListener(v -> setStage(UIStage.MainDashboard));
        layout.addView(returnBtn);

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
        title.setText("問卷已成功送出！");
        title.setTextSize(24);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        TextView tokenText = new TextView(this);
        String unlockToken = EfdNativeBridge.getUnlockToken();
        tokenText.setText("科研憑證: " + (unlockToken.isEmpty() ? "尚未取得" : unlockToken)
                + "\n\n感謝您 14 天以來的協助！\n現在您可以關閉或解除安裝本系統。");
        tokenText.setTextSize(15);
        tokenText.setTypeface(null, android.graphics.Typeface.BOLD);
        tokenText.setTextColor(Color.parseColor("#F7E3AF"));
        tokenText.setGravity(Gravity.CENTER);
        tokenText.setPadding(0, 20, 0, 40);
        layout.addView(tokenText);

        Button exitBtn = new Button(this);
        exitBtn.setText("關閉並結束應用程式");
        styleButton(exitBtn, "#EB5757", "#FFFFFF");
        exitBtn.setOnClickListener(v -> finish());
        layout.addView(exitBtn);

        mStageContentContainer.addView(layout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    // -------------------------------------------------------------------------
    // 疲勞警報通知發送 (Heads-up Notification + 震動反饋 + 彈窗)
    // -------------------------------------------------------------------------
    @android.annotation.SuppressLint("MissingPermission")
    @SuppressWarnings("deprecation")
    private void triggerFatigueAlertNotification(String message) {
        if (isActivityDestroyed()) return;

        // 1. 手機震動 (安全調用)
        try {
            if (mVibrator != null) {
                boolean canVibrate = true;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB) {
                    canVibrate = mVibrator.hasVibrator();
                }
                if (canVibrate) {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                        mVibrator.vibrate(VibrationEffect.createOneShot(500, VibrationEffect.DEFAULT_AMPLITUDE));
                    } else {
                        mVibrator.vibrate(500);
                    }
                }
            }
        } catch (Throwable t) {
            Log.w(TAG, "Vibration failed: " + t.getMessage());
        }

        // 2. 系統 Toast
        Toast.makeText(this, "你的眼睛處於疲勞狀態，請適當休息", Toast.LENGTH_LONG).show();

        // 3. Android 頂部 Heads-up 橫幅通知 (安全調用)
        try {
            NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
                    .setSmallIcon(android.R.drawable.ic_dialog_alert)
                    .setContentTitle("你的眼睛處於疲勞狀態，請適當休息")
                    .setContentText(message)
                    .setPriority(NotificationCompat.PRIORITY_HIGH)
                    .setAutoCancel(true);

            NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager != null) {
                manager.notify(101, builder.build());
            }
        } catch (Throwable t) {
            Log.w(TAG, "Notification trigger failed: " + t.getMessage());
        }

        // 4. 應用內紅色警告對話框
        try {
            new AlertDialog.Builder(this)
                    .setTitle("疲勞警報提醒")
                    .setMessage("你的眼睛處於疲勞狀態，請適當休息\n\n系統偵測到眼睛疲勞指數達到紅色危險狀態，請閉眼放鬆 5 分鐘。")
                    .setPositiveButton("我知道了，立即休息", null)
                    .show();
        } catch (Throwable t) {
            Log.w(TAG, "Dialog trigger failed: " + t.getMessage());
        }
    }

    // -------------------------------------------------------------------------
    // 遙測數據背景循環更新 (10FPS)
    // -------------------------------------------------------------------------
    private void startTelemetryLoop() {
        mTelemetryRunnable = new Runnable() {
            @Override
            public void run() {
                if (isActivityDestroyed()) return;

                try {
                    String jsonStr = EfdNativeBridge.getTelemetryJson();
                    JSONObject obj = new JSONObject(jsonStr != null ? jsonStr : EfdNativeBridge.ENGINE_NOT_RUNNING_JSON);
                    if (!obj.optBoolean("engineRunning", false)) {
                        // 引擎未啟動時明確告知，不顯示任何假數值
                        if (mCurrentStage == UIStage.MainDashboard && mDashScoreText != null) {
                            mDashStatusText.setText("偵測引擎未啟動：請確認已允許相機權限");
                            mDashStatusText.setTextColor(Color.parseColor("#EB5757"));
                            mDashScoreText.setText("疲勞評分：--");
                            if (mDashMetricsText != null) {
                                mDashMetricsText.setText("EAR 即時: -- | 閾值: --\nPERCLOS: -- | 眨眼: -- | CI: --");
                            }
                        }
                    } else {
                        mCurrentScore = (float) obj.optDouble("fatigueScore", 0.0);
                        mCurrentLevel = obj.optInt("fatigueLevel", 0);
                        mLastThreshold = (float) obj.optDouble("threshold", 0.0);
                        int cooldownState = obj.optInt("cooldownState", 0);
                        int cooldownRemaining = obj.optInt("cooldownRemaining", 0);

                        boolean hasFace = obj.optBoolean("hasFace", true);
                        float currentEar = (float) obj.optDouble("currentEar", 0.312);
                        float threshold = (float) obj.optDouble("threshold", 0.265);
                        float perclos = (float) obj.optDouble("perclos", 0.0);
                        int blinkRate = obj.optInt("blinkRate", 0);
                        float complexityCI = (float) obj.optDouble("complexityCI", 0.0);

                        if (mCurrentStage == UIStage.MainDashboard && mDashScoreText != null) {
                            if (!hasFace) {
                                // 顯示未偵測到臉的原因與相機畫面資訊，方便使用者調整或回報
                                mDashStatusText.setText("未偵測到臉：" + presenceReasonText(obj));
                                mDashStatusText.setTextColor(Color.parseColor("#F7E3AF"));
                                mDashScoreText.setText("疲勞評分：未就座 (暫停計算)");
                                if (mDashMetricsText != null) {
                                    mDashMetricsText.setText(String.format(java.util.Locale.getDefault(),
                                            "相機畫面: %dx%d %.0f fps | 卡在: %s\nEAR 閾值: %.3f | PERCLOS: -- | CI: --",
                                            obj.optInt("frameW", 0), obj.optInt("frameH", 0), obj.optDouble("measuredFps", 0.0),
                                            obj.optString("failStage", "-").isEmpty() ? "-" : obj.optString("failStage", "-"),
                                            threshold));
                                }
                            } else {
                                mDashScoreText.setText(String.format(java.util.Locale.getDefault(), "疲勞評分：%.1f / 100", mCurrentScore));
                                if (cooldownState == 1 && cooldownRemaining > 0) {
                                    int mins = cooldownRemaining / 60;
                                    int secs = cooldownRemaining % 60;
                                    mDashStatusText.setText(String.format(java.util.Locale.getDefault(),
                                            "生理狀態：重度疲勞【防打擾冷卻中 剩餘 %d分%d秒】", mins, secs));
                                    mDashStatusText.setTextColor(Color.parseColor("#EB5757"));
                                } else if (mCurrentLevel == 2) {
                                    mDashStatusText.setText("生理狀態：你的眼睛處於疲勞狀態，請適當休息");
                                    mDashStatusText.setTextColor(Color.parseColor("#EB5757"));
                                } else if (mCurrentLevel == 1) {
                                    mDashStatusText.setText("生理狀態：用眼注意 (Attention)");
                                    mDashStatusText.setTextColor(Color.parseColor("#F7E3AF"));
                                } else {
                                    mDashStatusText.setText("生理狀態：清醒專注 (Normal)");
                                    mDashStatusText.setTextColor(Color.parseColor("#1EB18A"));
                                }
                                if (mDashMetricsText != null) {
                                    mDashMetricsText.setText(String.format(java.util.Locale.getDefault(),
                                            "EAR 即時: %.3f | 閾值: %.3f\nPERCLOS: %.1f%% | 閃爍: %d 次/分 | CI: %.2f",
                                            currentEar, threshold, perclos, blinkRate, complexityCI));
                                }
                            }
                        }

                        // 疲勞警報不在這裡處理：由前景服務取出後，透過 AlertListener 通知前景的 Activity
                    }
                } catch (Throwable e) {
                    Log.w(TAG, "Telemetry update failed: " + e.getMessage());
                }

                mHandler.postDelayed(this, 100);
            }
        };
        mHandler.post(mTelemetryRunnable);
    }

    // -------------------------------------------------------------------------
    // 偵測診斷：未偵測到臉的原因、匯出 C++ 實際分析的畫面
    // -------------------------------------------------------------------------
    private static String reasonForStage(String stage) {
        switch (stage) {
            case "camera":   return "尚未收到實體相機畫面";
            case "stale":    return "相機畫面延遲或已暫停";
            case "skin":     return "找不到足夠的臉部膚色（太暗或臉不在畫面中）";
            case "size":     return "臉太大或太小（請把手機拿到約一個手臂的距離）";
            case "aspect":   return "臉部形狀不完整（請讓整張臉出現在畫面中）";
            case "fill":     return "臉部區域不連續（可能有遮擋或強烈陰影）";
            case "eyes":     return "找不到眼睛特徵（請正視鏡頭、避免反光或太暗）";
            case "symmetry": return "臉偏向一側（請正對鏡頭）";
            default:         return "臉部追蹤中斷（請保持臉部穩定）";
        }
    }

    private static String presenceReasonText(JSONObject obj) {
        if (obj.optBoolean("synthetic", false)) return reasonForStage("camera");
        if (obj.optBoolean("stale", false)) return reasonForStage("stale");
        return reasonForStage(obj.optString("failStage", ""));
    }

    private static final int DIAGNOSTIC_FRAMES = 5;

    private void onExportDiagnosticClicked() {
        new AlertDialog.Builder(this)
                .setTitle("匯出診斷畫面")
                .setMessage("將擷取 5 張偵測程式實際分析的前鏡頭畫面（含你的臉部影像），存到相簿的「EFD」資料夾，並把偵測數值複製成文字。\n\n"
                        + "之後在 LINE 傳送這 5 張照片、再貼上文字給研究人員即可。\n\n請按「開始」後看著前鏡頭，約 3 秒後擷取。")
                .setNegativeButton("取消", null)
                .setPositiveButton("開始", (d, w) -> {
                    Toast.makeText(this, "3 秒後擷取，請看著前鏡頭…", Toast.LENGTH_LONG).show();
                    new Thread(this::runDiagnosticExport, "EFD-DiagnosticExport").start();
                })
                .show();
    }

    // 背景執行緒：逐張向 C++ 要求擷取，存成 PNG 到相簿，最後在主執行緒複製數值並說明下一步
    private void runDiagnosticExport() {
        StringBuilder summary = new StringBuilder();
        int saved = 0;
        String stamp = new java.text.SimpleDateFormat("yyyyMMdd_HHmmss", java.util.Locale.US).format(new java.util.Date());
        try {
            Thread.sleep(3000);
            for (int i = 1; i <= DIAGNOSTIC_FRAMES; i++) {
                EfdNativeBridge.requestDebugCapture();
                String info = "";
                byte[] rgb = null;
                for (int wait = 0; wait < 30 && info.isEmpty(); wait++) {
                    Thread.sleep(50);
                    info = EfdNativeBridge.takeDebugCaptureInfo();
                }
                if (!info.isEmpty()) {
                    rgb = EfdNativeBridge.takeDebugCaptureRgb();
                }
                if (info.isEmpty() || rgb == null) {
                    summary.append('#').append(i).append(" 未取得畫面（相機可能未運作）\n");
                    continue;
                }
                JSONObject obj = new JSONObject(info);
                int w = obj.optInt("width", 0);
                int h = obj.optInt("height", 0);
                summary.append('#').append(i).append(' ').append(info).append('\n');
                if (w > 0 && h > 0 && rgb.length >= w * h * 3
                        && saveRgbToGallery(rgb, w, h, "EFD_diag_" + stamp + "_" + i + ".png")) {
                    saved++;
                }
                Thread.sleep(300);
            }
        } catch (Throwable t) {
            summary.append("匯出中斷: ").append(t.getMessage()).append('\n');
        }

        final int savedCount = saved;
        final String text = "[EFD Android 診斷]\n"
                + "裝置: " + Build.MANUFACTURER + " " + Build.MODEL + " / Android " + Build.VERSION.RELEASE
                + " (SDK " + Build.VERSION.SDK_INT + ")\n"
                + "時間: " + stamp + " / 通知: " + (EfdNotificationHudManager.canPostFatigueAlerts(this) ? "開啟" : "關閉") + "\n"
                + summary;
        mHandler.post(() -> {
            if (isActivityDestroyed()) return;
            android.content.ClipboardManager clipboard =
                    (android.content.ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (clipboard != null) {
                clipboard.setPrimaryClip(android.content.ClipData.newPlainText("EFD 診斷", text));
            }
            new AlertDialog.Builder(this)
                    .setTitle(savedCount > 0 ? "已存 " + savedCount + " 張到相簿，數值已複製" : "沒有存到畫面，數值已複製")
                    .setMessage(savedCount > 0
                            ? "請到 LINE：\n1. 從相簿的「EFD」資料夾選這幾張照片傳送（可選原始畫質）\n2. 在對話框長按「貼上」，送出數值文字"
                            : "請確認監控中心有相機畫面後再試一次；數值文字已複製，可以先貼給研究人員。")
                    .setPositiveButton("好", null)
                    .show();
        });
    }

    /** RGB888 → PNG，存到相簿 Pictures/EFD (Android 10+ 不需要儲存權限；舊版存到 App 專屬相片資料夾)。 */
    private boolean saveRgbToGallery(byte[] rgb, int w, int h, String fileName) {
        int[] pixels = new int[w * h];
        for (int p = 0, i = 0; p < pixels.length; p++, i += 3) {
            pixels[p] = 0xFF000000 | ((rgb[i] & 0xFF) << 16) | ((rgb[i + 1] & 0xFF) << 8) | (rgb[i + 2] & 0xFF);
        }
        android.graphics.Bitmap bitmap = android.graphics.Bitmap.createBitmap(pixels, w, h, android.graphics.Bitmap.Config.ARGB_8888);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                android.content.ContentValues values = new android.content.ContentValues();
                values.put(android.provider.MediaStore.Images.Media.DISPLAY_NAME, fileName);
                values.put(android.provider.MediaStore.Images.Media.MIME_TYPE, "image/png");
                values.put(android.provider.MediaStore.Images.Media.RELATIVE_PATH, "Pictures/EFD");
                android.net.Uri uri = getContentResolver().insert(android.provider.MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
                if (uri == null) return false;
                try (java.io.OutputStream out = getContentResolver().openOutputStream(uri)) {
                    return out != null && bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out);
                }
            }
            java.io.File dir = getExternalFilesDir(android.os.Environment.DIRECTORY_PICTURES);
            if (dir == null) return false;
            try (java.io.FileOutputStream out = new java.io.FileOutputStream(new java.io.File(dir, fileName))) {
                return bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out);
            }
        } catch (Throwable t) {
            Log.w(TAG, "儲存診斷畫面失敗: " + t.getMessage());
            return false;
        } finally {
            bitmap.recycle();
        }
    }

    // -------------------------------------------------------------------------
    // 背景通知：測試、權限檢查與廠牌背景執行說明
    // -------------------------------------------------------------------------
    private boolean mNotificationCheckShown = false;

    private void onTestBackgroundAlertClicked() {
        if (!EfdNotificationHudManager.canPostFatigueAlerts(this)) {
            showNotificationsDisabledDialog();
            return;
        }
        EfdForegroundService service = EfdForegroundService.getInstance();
        if (service == null) {
            Toast.makeText(this, "背景偵測尚未啟動，請先回到監控中心", Toast.LENGTH_LONG).show();
            return;
        }
        service.scheduleTestAlert(10_000L);
        new AlertDialog.Builder(this)
                .setTitle("10 秒後發送測試通知")
                .setMessage("請現在把 EFD 從最近使用的 App 清單劃掉，或回到桌面。\n\n"
                        + "10 秒後應會收到內容為「【測試通知】背景通知運作正常」的通知；若沒有收到，請按「通知與背景執行設定」依說明調整。")
                .setPositiveButton("好", null)
                .show();
    }

    /** 進入監控中心時檢查一次：通知被關閉時疲勞提醒會被系統直接丟棄。 */
    private void checkNotificationsEnabledOnce() {
        if (mNotificationCheckShown) return;
        mNotificationCheckShown = true;
        if (!EfdNotificationHudManager.canPostFatigueAlerts(this)) {
            showNotificationsDisabledDialog();
        }
    }

    private void showNotificationsDisabledDialog() {
        if (isActivityDestroyed()) return;
        new AlertDialog.Builder(this)
                .setTitle("疲勞提醒通知已關閉")
                .setMessage("EFD 的通知目前被關閉，App 不在畫面上時將無法提醒你休息。\n請在設定中開啟 EFD 的通知（包含「EFD 眼睛疲勞警告提醒」）。")
                .setPositiveButton("前往設定", (d, w) -> openAppNotificationSettings())
                .setNegativeButton("稍後", null)
                .show();
    }

    private void openAppNotificationSettings() {
        try {
            Intent intent = new Intent(android.provider.Settings.ACTION_APP_NOTIFICATION_SETTINGS);
            intent.putExtra(android.provider.Settings.EXTRA_APP_PACKAGE, getPackageName());
            startActivity(intent);
        } catch (Throwable t) {
            openAppDetailsSettings();
        }
    }

    private void openAppDetailsSettings() {
        try {
            Intent intent = new Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
            intent.setData(android.net.Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Throwable t) {
            Log.w(TAG, "無法開啟 App 設定頁: " + t.getMessage());
        }
    }

    /** 不同廠牌關閉背景服務的設定位置不同，依 Build.MANUFACTURER 顯示對應說明。 */
    private void showBackgroundSettingsGuide() {
        String maker = Build.MANUFACTURER == null ? "" : Build.MANUFACTURER.toLowerCase(java.util.Locale.ROOT);
        String brandTip;
        if (maker.contains("xiaomi") || maker.contains("redmi") || maker.contains("poco")) {
            brandTip = "小米：設定 > 應用程式 > EFD > 自動啟動 開啟；省電策略 選「無限制」。\n最近使用清單中長按 EFD 選「鎖定」。";
        } else if (maker.contains("oppo") || maker.contains("realme") || maker.contains("oneplus")) {
            brandTip = "OPPO / realme / 一加：設定 > 電池 > EFD > 允許背景活動、允許自動啟動。\n最近使用清單中將 EFD 往下拉「鎖定」。";
        } else if (maker.contains("vivo") || maker.contains("iqoo")) {
            brandTip = "vivo：設定 > 電池 > 背景耗電管理 > EFD 允許背景高耗電；i管家 > 應用程式管理 > 自動啟動 開啟。";
        } else if (maker.contains("huawei") || maker.contains("honor")) {
            brandTip = "華為 / 榮耀：設定 > 電池 > 啟動管理 > EFD 改為手動管理，三項全部開啟。";
        } else if (maker.contains("samsung")) {
            brandTip = "三星：設定 > 電池 > 背景使用限制 > 確認 EFD 不在「休眠」或「深度休眠」清單；EFD 的電池設定選「不受限制」。";
        } else {
            brandTip = "設定 > 應用程式 > EFD > 電池：選「不受限制」或「不最佳化」。";
        }
        String status = EfdNotificationHudManager.canPostFatigueAlerts(this) ? "目前：通知已開啟" : "目前：通知已關閉（請先開啟）";
        new AlertDialog.Builder(this)
                .setTitle("通知與背景執行設定")
                .setMessage(status + "\n\n"
                        + "1. 通知：開啟 EFD 通知，以及「EFD 眼睛疲勞警告提醒」類別。\n"
                        + "2. 背景執行：" + brandTip + "\n\n"
                        + "疲勞提醒只在偵測到疲勞時發送（平滑分數達門檻並持續數秒），平常沒有通知是正常的；可用「測試背景通知」確認設定是否正確。")
                .setPositiveButton("通知設定", (d, w) -> openAppNotificationSettings())
                .setNeutralButton("App 設定", (d, w) -> openAppDetailsSettings())
                .setNegativeButton("關閉", null)
                .show();
    }

    @Override
    protected void onResume() {
        super.onResume();
        mIsResumed = true;
        // 服務被系統關閉後，使用者點通知回到 App：Activity 在前景時可重新啟動相機類型前景服務
        if (EfdForegroundService.getInstance() == null &&
                ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            startForegroundMonitoringService();
        }
        // 前景時由對話框呈現警報；背景時服務改發通知
        EfdForegroundService.setAlertListener(message -> mHandler.post(() -> {
            if (mIsResumed && !isActivityDestroyed()) {
                triggerFatigueAlertNotification(message);
            }
        }));
        if (mHandler != null && mTelemetryRunnable != null) {
            mHandler.removeCallbacks(mTelemetryRunnable);
            mHandler.post(mTelemetryRunnable);
        }
        if (mCurrentStage == UIStage.MainDashboard) {
            renderCurrentStage(); // 重新綁定相機預覽
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        mIsResumed = false;
        EfdForegroundService.setAlertListener(null);
        if (mHandler != null && mTelemetryRunnable != null) {
            mHandler.removeCallbacks(mTelemetryRunnable);
        }
    }

    @Override
    protected void onStop() {
        super.onStop();
        // 介面不可見時，服務的相機改回只做影像分析，不再綁定即將消失的預覽畫面
        if (mPreviewBoundToService) {
            EfdForegroundService fgService = EfdForegroundService.getInstance();
            if (fgService != null && fgService.getCameraManager() != null) {
                fgService.getCameraManager().startCamera();
            }
            mPreviewBoundToService = false;
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mScreenStateReceiver != null) {
            try {
                mScreenStateReceiver.unregister(this);
            } catch (Throwable t) {
                // ignore
            }
        }
        if (mHandler != null && mTelemetryRunnable != null) {
            mHandler.removeCallbacks(mTelemetryRunnable);
        }
        // 不停止偵測引擎：引擎由 EfdForegroundService 擁有，App 介面被劃掉後仍需持續偵測與提醒
    }
}
