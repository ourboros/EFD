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
        mMainContainer.setBackgroundColor(Color.parseColor("#1EB18A")); // EFD 薄荷綠背景 (與各頁一致)

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
        scroll.setBackgroundColor(Color.parseColor("#148C6C"));
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
            styleButton(btn, isActive ? "#FFFFFF" : "#33FFFFFF", isActive ? "#1EB18A" : "#FFFFFF");

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
                styleButton(btn, isActive ? "#FFFFFF" : "#33FFFFFF", isActive ? "#1EB18A" : "#FFFFFF");
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
    // 共用版面：與監控中心、設定頁相同的設計系統 (薄荷綠底、白色圓角卡片、墨黑文字)
    // -------------------------------------------------------------------------
    /** 薄荷綠底、內容置中、可捲動的頁面；回傳內容容器。 */
    private LinearLayout createMintPage(boolean centerVertically) {
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.parseColor(C_MINT));
        scroll.setFillViewport(true);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(centerVertically ? Gravity.CENTER : Gravity.CENTER_HORIZONTAL);
        layout.setPadding(dp(24), dp(20), dp(24), dp(28));
        scroll.addView(layout, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        mStageContentContainer.addView(scroll, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        return layout;
    }

    private TextView addPageTitle(LinearLayout parent, String text, float sizeSp) {
        TextView title = new TextView(this);
        title.setText(text);
        title.setTextSize(sizeSp);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        parent.addView(title);
        return title;
    }

    private TextView addPageSubtitle(LinearLayout parent, String text) {
        TextView sub = new TextView(this);
        sub.setText(text);
        sub.setTextSize(14);
        sub.setTypeface(null, android.graphics.Typeface.BOLD);
        sub.setTextColor(Color.parseColor(C_AMBER));
        sub.setGravity(Gravity.CENTER);
        sub.setPadding(0, dp(6), 0, 0);
        parent.addView(sub);
        return sub;
    }

    /** 白色圓角說明卡片：薄荷綠粗體標題 + 墨黑內文。 */
    private LinearLayout addInfoCard(LinearLayout parent, String title, String body) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(roundedBg(C_WHITE, 14));
        card.setPadding(dp(16), dp(12), dp(16), dp(14));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, dp(12), 0, 0);
        card.setLayoutParams(lp);
        if (title != null) {
            TextView t = new TextView(this);
            t.setText(title);
            t.setTextSize(15);
            t.setTypeface(null, android.graphics.Typeface.BOLD);
            t.setTextColor(Color.parseColor(C_MINT));
            card.addView(t);
        }
        TextView b = new TextView(this);
        b.setText(body);
        b.setTextSize(13);
        b.setTextColor(Color.parseColor(C_INK));
        b.setLineSpacing(dp(2), 1f);
        b.setPadding(0, title != null ? dp(4) : 0, 0, 0);
        card.addView(b);
        parent.addView(card);
        return card;
    }

    // -------------------------------------------------------------------------
    // 階段 1：歡迎介面
    // -------------------------------------------------------------------------
    private void renderWelcomeScreen() {
        LinearLayout layout = createMintPage(true);

        // 白色圓形徽章內嵌官方「資產 10」Logo
        FrameLayout badge = new FrameLayout(this);
        android.graphics.drawable.GradientDrawable badgeBg = new android.graphics.drawable.GradientDrawable();
        badgeBg.setShape(android.graphics.drawable.GradientDrawable.OVAL);
        badgeBg.setColor(Color.WHITE);
        badgeBg.setStroke(dp(3), Color.parseColor(C_AMBER));
        badge.setBackground(badgeBg);
        android.widget.ImageView logoImage = new android.widget.ImageView(this);
        int logoResId = getResources().getIdentifier("logo10", "drawable", getPackageName());
        if (logoResId != 0) {
            logoImage.setImageResource(logoResId);
        }
        logoImage.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);
        FrameLayout.LayoutParams logoLp = new FrameLayout.LayoutParams(dp(74), dp(74), Gravity.CENTER);
        badge.addView(logoImage, logoLp);
        LinearLayout.LayoutParams badgeLp = new LinearLayout.LayoutParams(dp(112), dp(112));
        badgeLp.gravity = Gravity.CENTER_HORIZONTAL;
        badgeLp.setMargins(0, 0, 0, dp(20));
        layout.addView(badge, badgeLp);

        addPageTitle(layout, "感謝協助測試 EFD", 26);
        addPageSubtitle(layout, "AI 驅動眼睛特徵提取與即時疲勞監控研究系統");

        addInfoCard(layout, null,
                "EFD 會使用前鏡頭即時分析眨眼與閉眼特徵，在眼睛疲勞時提醒你休息。\n"
                + "影像只在這支手機上分析，不會儲存或上傳；每日只上傳疲勞數值統計。");

        Button startBtn = dashButton("進入測驗說明", C_WHITE, C_MINT, 24);
        startBtn.setOnClickListener(v -> setStage(UIStage.CalibrationInstruction));
        layout.addView(startBtn);
    }

    // -------------------------------------------------------------------------
    // 階段 2：校準說明介面
    // -------------------------------------------------------------------------
    private void renderInstructionScreen() {
        LinearLayout layout = createMintPage(true);
        addPageTitle(layout, "眼動特徵校準說明", 23);
        addPageSubtitle(layout, "約 5 秒，讓系統認識你睜眼時的眼睛特徵");

        addInfoCard(layout, "1. 臉部正對前鏡頭",
                "把手機拿到與眼睛同高、約一個手臂的距離，讓整張臉出現在畫面中。");
        addInfoCard(layout, "2. 眼睛跟著黃點移動",
                "校準開始後黃點會依序移到螢幕四個角落，請保持頭部不動、只用眼睛跟隨。");
        addInfoCard(layout, "3. 避免背光與強光",
                "天花板燈或窗戶不要出現在鏡頭裡，臉部太暗會無法偵測。保持自然眨眼即可。");

        Button readyBtn = dashButton("我準備好了，開始校準", C_WHITE, C_MINT, 24);
        readyBtn.setOnClickListener(v -> setStage(UIStage.CountdownWait));
        layout.addView(readyBtn);

        Button backBtn = dashButton("返回歡迎介面", "#33FFFFFF", C_WHITE, 10);
        backBtn.setOnClickListener(v -> setStage(UIStage.Welcome));
        layout.addView(backBtn);
    }

    // -------------------------------------------------------------------------
    // 階段 3：倒數計時等待
    // -------------------------------------------------------------------------
    private void renderCountdownScreen() {
        LinearLayout layout = createMintPage(true);

        TextView cdText = new TextView(this);
        cdText.setText("3");
        cdText.setTextSize(64);
        cdText.setTypeface(null, android.graphics.Typeface.BOLD);
        cdText.setTextColor(Color.parseColor(C_MINT));
        cdText.setGravity(Gravity.CENTER);
        android.graphics.drawable.GradientDrawable circle = new android.graphics.drawable.GradientDrawable();
        circle.setShape(android.graphics.drawable.GradientDrawable.OVAL);
        circle.setColor(Color.WHITE);
        circle.setStroke(dp(4), Color.parseColor(C_AMBER));
        cdText.setBackground(circle);
        LinearLayout.LayoutParams cdLp = new LinearLayout.LayoutParams(dp(140), dp(140));
        cdLp.gravity = Gravity.CENTER_HORIZONTAL;
        layout.addView(cdText, cdLp);

        TextView hint = addPageTitle(layout, "正在尋找你的臉部…", 17);
        hint.setPadding(0, dp(24), 0, 0);
        TextView sub = addPageSubtitle(layout, "請將手機拿到與眼睛同高，正對前鏡頭");

        // 偵測到臉才倒數：第一次使用時手機還在判斷畫面方向，提早開始校準會浪費掉大部分校準時間
        Button skipBtn = dashButton("略過，先使用預設基準", "#33FFFFFF", C_WHITE, 24);
        skipBtn.setVisibility(View.GONE);
        skipBtn.setOnClickListener(v -> setStage(UIStage.MainDashboard));
        layout.addView(skipBtn);

        mCountdownSec = 3;
        final long startTime = System.currentTimeMillis();
        mHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (isActivityDestroyed() || mCurrentStage != UIStage.CountdownWait) return;
                if (isFaceDetectedNow()) {
                    hint.setText("已偵測到臉部，請保持不動…");
                    sub.setText("接下來只用眼睛跟著黃點移動");
                    mCountdownSec--;
                    if (mCountdownSec <= 0) {
                        setStage(UIStage.ActiveCalibration);
                        return;
                    }
                    cdText.setText(String.valueOf(mCountdownSec));
                } else {
                    mCountdownSec = 3;
                    cdText.setText("3");
                    hint.setText("正在尋找你的臉部…");
                    if (System.currentTimeMillis() - startTime > 10000) {
                        sub.setText("還沒偵測到臉部：請面向窗戶或燈光（不要背對光源），讓整張臉出現在鏡頭前");
                        skipBtn.setVisibility(View.VISIBLE);
                    }
                }
                mHandler.postDelayed(this, 1000);
            }
        }, 1000);
    }

    /** 偵測引擎目前是否判定使用者在鏡頭前 (校準前檢查用)。 */
    private boolean isFaceDetectedNow() {
        try {
            JSONObject obj = new JSONObject(EfdNativeBridge.getTelemetryJson());
            return obj.optBoolean("engineRunning", false)
                    && obj.optBoolean("hasFace", false)
                    && !obj.optBoolean("stale", false);
        } catch (Throwable t) {
            return false;
        }
    }

    /** 回傳 {有效人臉幀, 第一次看到臉之後的總幀數}。 */
    private int[] getCalibrationProgress() {
        try {
            JSONObject obj = new JSONObject(EfdNativeBridge.getTelemetryJson());
            return new int[] { obj.optInt("calibSamples", 0), obj.optInt("calibFrames", 0) };
        } catch (Throwable t) {
            return new int[] { 0, 0 };
        }
    }

    // -------------------------------------------------------------------------
    // 階段 4：動態 5 點眼動特徵採樣 (與 Windows 版相同路徑：中心→左上→右上→右下→左下→中心)
    // -------------------------------------------------------------------------
    private static final float[][] CALIB_PATH = {
            {0.50f, 0.50f}, {0.15f, 0.22f}, {0.85f, 0.22f}, {0.85f, 0.78f}, {0.15f, 0.78f}, {0.50f, 0.50f}
    };

    private void renderActiveCalibrationScreen() {
        FrameLayout layout = new FrameLayout(this);
        layout.setBackgroundColor(Color.parseColor(C_MINT));

        TextView title = new TextView(this);
        title.setText("眼動特徵基準校準中…");
        title.setTextSize(22);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        FrameLayout.LayoutParams titleLp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP);
        titleLp.setMargins(0, dp(24), 0, 0);
        layout.addView(title, titleLp);

        final float dotRadius = dp(22);
        View dotCanvas = new View(this) {
            @Override
            protected void onDraw(Canvas canvas) {
                super.onDraw(canvas);
                Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
                float x = getWidth() * mDotX;
                float y = getHeight() * mDotY;
                p.setColor(Color.parseColor(C_AMBER)); // 暖琥珀黃凝視點
                canvas.drawCircle(x, y, dotRadius, p);
                p.setStyle(Paint.Style.STROKE);
                p.setColor(Color.parseColor("#CC96C5F7")); // 晴空藍外暈
                p.setStrokeWidth(dp(4));
                canvas.drawCircle(x, y, dotRadius + dp(3), p);
            }
        };
        FrameLayout.LayoutParams canvasLp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        canvasLp.setMargins(dp(16), dp(80), dp(16), dp(110));
        layout.addView(dotCanvas, canvasLp);

        TextView hint = new TextView(this);
        hint.setText("請保持臉部不動，只用眼睛跟著黃點移動…");
        hint.setTextSize(14);
        hint.setTypeface(null, android.graphics.Typeface.BOLD);
        hint.setTextColor(Color.WHITE);
        hint.setGravity(Gravity.CENTER);
        FrameLayout.LayoutParams hintLp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM);
        hintLp.setMargins(dp(24), 0, dp(24), dp(64));
        layout.addView(hint, hintLp);

        // 底部暖琥珀色進度條
        ProgressBar pbar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        pbar.setMax(100);
        pbar.setProgress(0);
        pbar.setProgressTintList(android.content.res.ColorStateList.valueOf(Color.parseColor(C_AMBER)));
        pbar.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(Color.parseColor("#148C6C")));
        FrameLayout.LayoutParams pbarParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(10), Gravity.BOTTOM);
        pbarParams.setMargins(dp(40), 0, dp(40), dp(36));
        layout.addView(pbar, pbarParams);

        mStageContentContainer.addView(layout, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        mDotX = CALIB_PATH[0][0];
        mDotY = CALIB_PATH[0][1];
        final long startTime = System.currentTimeMillis();
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                if (isActivityDestroyed() || mCurrentStage != UIStage.ActiveCalibration) return;

                long elapsed = System.currentTimeMillis() - startTime;
                float t = (elapsed % 5000) / 5000f; // 一輪 5 秒，每段 1 秒

                // 至少一輪 5 秒；有效畫面不足時 (例如中途臉被判定離開) 繼續巡迴，最多 15 秒
                boolean keepGoing = elapsed < 5000;
                if (!keepGoing && elapsed < 15000) {
                    int[] prog = getCalibrationProgress();
                    keepGoing = prog[0] < 20 || prog[0] < prog[1] * 0.6f;
                    if (keepGoing) hint.setText("偵測中斷，請正對鏡頭並保持不動，校準會自動延長…");
                }

                if (keepGoing) {
                    pbar.setProgress((int) (elapsed < 5000 ? elapsed / 50 : elapsed / 150));
                    float segPos = t * 5f;
                    int seg = Math.min(4, (int) segPos);
                    float segT = segPos - seg;
                    float smooth = 0.5f * (1f - (float) Math.cos(segT * Math.PI)); // 緩入緩出
                    mDotX = CALIB_PATH[seg][0] + (CALIB_PATH[seg + 1][0] - CALIB_PATH[seg][0]) * smooth;
                    mDotY = CALIB_PATH[seg][1] + (CALIB_PATH[seg + 1][1] - CALIB_PATH[seg][1]) * smooth;
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
        LinearLayout layout = createMintPage(true);
        addPageTitle(layout, mLastCalibrationOk ? "眼動特徵基準校準完成！" : "校準未完成", 24);

        String body;
        if (mLastCalibrationOk) {
            body = String.format(java.util.Locale.getDefault(),
                    "個人化閉眼判定閾值 EAR = %.3f\n下次開啟將直接沿用此基準。", mLastThreshold);
        } else if (!EfdNativeBridge.isEngineRunning()) {
            body = "偵測引擎尚未啟動，請確認已允許相機權限後重新校準。";
        } else {
            body = "校準期間未穩定偵測到臉部。\n請把手機拿到與眼睛同高、避免燈光在畫面中，正對鏡頭後重新校準。";
        }
        addInfoCard(layout, mLastCalibrationOk ? "校準結果" : "可能原因", body);

        if (!mLastCalibrationOk) {
            Button retryBtn = dashButton("重新校準", C_WHITE, C_MINT, 24);
            retryBtn.setOnClickListener(v -> setStage(UIStage.CalibrationInstruction));
            layout.addView(retryBtn);

            // 失敗當下最能看出原因：直接擷取偵測畫面存到相簿，方便回傳給研究人員
            Button exportBtn = dashButton("匯出診斷畫面 (回報問題用)", "#33FFFFFF", C_WHITE, 10);
            exportBtn.setOnClickListener(v -> onExportDiagnosticClicked());
            layout.addView(exportBtn);
        }

        Button proceedBtn = dashButton(mLastCalibrationOk ? "進入即時眼動監控中心" : "略過，先使用預設基準",
                C_WHITE, C_MINT, mLastCalibrationOk ? 24 : 10);
        proceedBtn.setOnClickListener(v -> setStage(UIStage.MainDashboard));
        layout.addView(proceedBtn);

        // 數據提取後轉入背景持續監控
        Button closeBgBtn = dashButton("背景運行模式", C_CORAL, C_WHITE, 10);
        closeBgBtn.setOnClickListener(v -> {
            Toast.makeText(this, "EFD 已轉入背景監控，僅於疲勞超標時跳出提醒", Toast.LENGTH_LONG).show();
            moveTaskToBack(true);
        });
        layout.addView(closeBgBtn);
    }

    // -------------------------------------------------------------------------
    // 階段 6：即時眼動監控中心 (Dashboard)
    // -------------------------------------------------------------------------
    private TextView mDashScoreText;
    private TextView mDashStatusText;
    private TextView mDashMetricsText;
    private boolean mPreviewBoundToService = false;

    // 與原 iOS 版相同的設計系統：薄荷綠底、白色圓角卡片、墨黑文字、珊瑚紅警示
    private static final String C_MINT = "#1EB18A";
    private static final String C_WHITE = "#FFFFFF";
    private static final String C_AMBER = "#F7E3AF";
    private static final String C_INK = "#25291C";
    private static final String C_CORAL = "#EB5757";

    private TextView mDashEarValue;
    private TextView mDashPerclosValue;
    private TextView mDashCiValue;
    private TextView mDashScoreValue;
    private String mLastTelemetryJson = "";

    private int dp(float value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private android.graphics.drawable.GradientDrawable roundedBg(String color, float radiusDp) {
        android.graphics.drawable.GradientDrawable bg = new android.graphics.drawable.GradientDrawable();
        bg.setColor(Color.parseColor(color));
        bg.setCornerRadius(dp(radiusDp));
        return bg;
    }

    /** 2x2 指標白卡片：標題 + 大字數值，回傳數值 TextView。 */
    private TextView addMetricCard(LinearLayout row, String title, String initial, boolean leftCell) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(roundedBg(C_WHITE, 12));
        card.setPadding(dp(12), dp(8), dp(12), dp(10));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        lp.setMargins(leftCell ? 0 : dp(4), 0, leftCell ? dp(4) : 0, 0);
        card.setLayoutParams(lp);

        TextView t = new TextView(this);
        t.setText(title);
        t.setTextSize(12);
        t.setTypeface(null, android.graphics.Typeface.BOLD);
        t.setTextColor(Color.parseColor("#B325291C"));
        card.addView(t);

        TextView v = new TextView(this);
        v.setText(initial);
        v.setTextSize(20);
        v.setTypeface(null, android.graphics.Typeface.BOLD);
        v.setTextColor(Color.parseColor(C_INK));
        card.addView(v);

        row.addView(card);
        return v;
    }

    private Button dashButton(String text, String bg, String fg, float topMarginDp) {
        Button btn = new Button(this);
        btn.setText(text);
        btn.setAllCaps(false);
        btn.setTextSize(15);
        btn.setTypeface(null, android.graphics.Typeface.BOLD);
        btn.setTextColor(Color.parseColor(fg));
        btn.setBackground(roundedBg(bg, 16));
        btn.setStateListAnimator(null);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(46));
        lp.setMargins(0, dp(topMarginDp), 0, 0);
        btn.setLayoutParams(lp);
        return btn;
    }

    private void renderMainDashboardScreen() {
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.parseColor(C_MINT));

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(dp(20), dp(12), dp(20), dp(24));

        TextView header = new TextView(this);
        header.setText("EFD 即時眼睛疲勞監控中心");
        header.setTextSize(21);
        header.setTypeface(null, android.graphics.Typeface.BOLD);
        header.setTextColor(Color.WHITE);
        header.setGravity(Gravity.CENTER);
        header.setPadding(0, 0, 0, dp(10));
        layout.addView(header);

        // 1. 4:3 相機預覽卡片 (圓角、白色細邊框，右上角「回報問題」)
        try {
            int screenW = getResources().getDisplayMetrics().widthPixels;
            int camW = screenW - dp(40);
            int camH = camW * 3 / 4;
            FrameLayout cardView = new FrameLayout(this);
            android.graphics.drawable.GradientDrawable cardBg = roundedBg("#EBF5F0", 14);
            cardBg.setStroke(dp(2), Color.WHITE);
            cardView.setBackground(cardBg);
            cardView.setClipToOutline(true);
            cardView.setPadding(dp(2), dp(2), dp(2), dp(2));
            LinearLayout.LayoutParams cardLp = new LinearLayout.LayoutParams(camW, camH);
            cardLp.gravity = Gravity.CENTER_HORIZONTAL;
            cardView.setLayoutParams(cardLp);

            androidx.camera.view.PreviewView previewView = new androidx.camera.view.PreviewView(this);
            previewView.setScaleType(androidx.camera.view.PreviewView.ScaleType.FILL_CENTER);
            cardView.addView(previewView, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

            TextView reportBtn = new TextView(this);
            reportBtn.setText("回報問題");
            reportBtn.setTextSize(12);
            reportBtn.setTypeface(null, android.graphics.Typeface.BOLD);
            reportBtn.setTextColor(Color.WHITE);
            reportBtn.setBackground(roundedBg(C_CORAL, 12));
            reportBtn.setPadding(dp(12), dp(4), dp(12), dp(4));
            FrameLayout.LayoutParams reportLp = new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.END);
            reportLp.setMargins(0, dp(8), dp(8), 0);
            reportBtn.setOnClickListener(v -> copyDiagnosticReport());
            cardView.addView(reportBtn, reportLp);

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

        // 2. 狀態白卡片：受試者、生理狀態、疲勞評分、偵測細節
        LinearLayout statusCard = new LinearLayout(this);
        statusCard.setOrientation(LinearLayout.VERTICAL);
        statusCard.setBackground(roundedBg(C_WHITE, 12));
        statusCard.setPadding(dp(14), dp(10), dp(14), dp(12));
        LinearLayout.LayoutParams statusLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        statusLp.setMargins(0, dp(10), 0, 0);
        statusCard.setLayoutParams(statusLp);

        TextView subjText = new TextView(this);
        String uuid = EfdNativeBridge.getSubjectUuid();
        int day = EfdNativeBridge.getCurrentDay();
        if (uuid == null || uuid.isEmpty() || day <= 0) {
            subjText.setText("受試者代碼：-- (偵測服務啟動中)");
        } else {
            subjText.setText(String.format(java.util.Locale.getDefault(), "受試者代碼：%s (實驗第 %d / 14 天)", uuid, day));
        }
        subjText.setTextSize(12);
        subjText.setTextColor(Color.parseColor("#9925291C"));
        statusCard.addView(subjText);

        mDashStatusText = new TextView(this);
        mDashStatusText.setText("生理狀態：偵測中…");
        mDashStatusText.setTextSize(15);
        mDashStatusText.setTypeface(null, android.graphics.Typeface.BOLD);
        mDashStatusText.setTextColor(Color.parseColor(C_MINT));
        mDashStatusText.setPadding(0, dp(4), 0, 0);
        statusCard.addView(mDashStatusText);

        mDashScoreText = new TextView(this);
        mDashScoreText.setText("疲勞評分：--");
        mDashScoreText.setTextSize(13);
        mDashScoreText.setTextColor(Color.parseColor(C_INK));
        statusCard.addView(mDashScoreText);

        mDashMetricsText = new TextView(this);
        mDashMetricsText.setText("");
        mDashMetricsText.setTextSize(11);
        mDashMetricsText.setTextColor(Color.parseColor("#9925291C"));
        statusCard.addView(mDashMetricsText);

        layout.addView(statusCard);

        // 3. 2x2 指標白卡片
        LinearLayout row1 = new LinearLayout(this);
        row1.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams row1Lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        row1Lp.setMargins(0, dp(10), 0, 0);
        row1.setLayoutParams(row1Lp);
        mDashEarValue = addMetricCard(row1, "雙眼 EAR", "--", true);
        mDashPerclosValue = addMetricCard(row1, "閉眼比 (PERCLOS)", "--", false);
        layout.addView(row1);

        LinearLayout row2 = new LinearLayout(this);
        row2.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams row2Lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        row2Lp.setMargins(0, dp(8), 0, 0);
        row2.setLayoutParams(row2Lp);
        mDashCiValue = addMetricCard(row2, "複雜度 (MSE / CI)", "--", true);
        mDashScoreValue = addMetricCard(row2, "綜合疲勞分數", "--", false);
        layout.addView(row2);

        // 4. 控制按鈕
        Button recalibBtn = dashButton("重新偵測疲勞數據", C_WHITE, C_MINT, 14);
        recalibBtn.setOnClickListener(v -> setStage(UIStage.CalibrationInstruction));
        layout.addView(recalibBtn);

        LinearLayout subRow = new LinearLayout(this);
        subRow.setOrientation(LinearLayout.HORIZONTAL);
        Button settingsBtn = dashButton("系統參數設定", C_WHITE, C_MINT, 8);
        settingsBtn.setOnClickListener(v -> setStage(UIStage.SettingsPanel));
        Button bgBtn = dashButton("背景運行模式", C_CORAL, C_WHITE, 8);
        bgBtn.setOnClickListener(v -> {
            Toast.makeText(this, "EFD 已轉入背景監控，僅於疲勞超標時跳出提醒", Toast.LENGTH_LONG).show();
            moveTaskToBack(true);
        });
        LinearLayout.LayoutParams half1 = (LinearLayout.LayoutParams) settingsBtn.getLayoutParams();
        half1.width = 0; half1.weight = 1f; half1.setMarginEnd(dp(4));
        LinearLayout.LayoutParams half2 = (LinearLayout.LayoutParams) bgBtn.getLayoutParams();
        half2.width = 0; half2.weight = 1f; half2.setMarginStart(dp(4));
        subRow.addView(settingsBtn);
        subRow.addView(bgBtn);
        layout.addView(subRow);

        scroll.addView(layout);
        mStageContentContainer.addView(scroll, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    /** 監控中心「回報問題」：把目前偵測狀態與裝置資訊複製成文字 (不含影像)。 */
    private void copyDiagnosticReport() {
        String text = "[EFD Android 錯誤回報]\n"
                + "裝置: " + Build.MANUFACTURER + " " + Build.MODEL + " / Android " + Build.VERSION.RELEASE
                + " (SDK " + Build.VERSION.SDK_INT + ")\n"
                + "通知: " + (EfdNotificationHudManager.canPostFatigueAlerts(this) ? "開啟" : "關閉")
                + " / 背景偵測: " + (EfdForegroundService.getInstance() != null ? "執行中" : "未執行") + "\n"
                + "目前偵測: " + mLastTelemetryJson;
        android.content.ClipboardManager clipboard =
                (android.content.ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (clipboard != null) {
            clipboard.setPrimaryClip(android.content.ClipData.newPlainText("EFD 錯誤回報", text));
        }
        Toast.makeText(this, "已複製錯誤資訊，請貼到 LINE 傳給研究人員", Toast.LENGTH_LONG).show();
    }

    // -------------------------------------------------------------------------
    // 階段 7：設定面板 (僅保留：提示靈敏度、重設眼動監測、警告通知發送)
    // -------------------------------------------------------------------------
    private int mSensitivityLevel = 1; // 0: 低, 1: 標準, 2: 高 (實際值保存在 C++ 設定檔)

    /** 設定白卡片：左側標題 (薄荷綠粗體) 與說明 (灰黑)，右側狀態文字與箭頭；回傳右側 TextView。 */
    private TextView addSettingsCard(LinearLayout parent, String title, String subtitle, String rightText,
                                     boolean enabled, View.OnClickListener onClick) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.HORIZONTAL);
        card.setGravity(Gravity.CENTER_VERTICAL);
        card.setBackground(roundedBg(C_WHITE, 14));
        card.setPadding(dp(16), dp(12), dp(14), dp(12));
        card.setAlpha(enabled ? 1f : 0.6f);
        LinearLayout.LayoutParams cardLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        cardLp.setMargins(0, dp(10), 0, 0);
        card.setLayoutParams(cardLp);

        LinearLayout texts = new LinearLayout(this);
        texts.setOrientation(LinearLayout.VERTICAL);
        texts.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        TextView t = new TextView(this);
        t.setText(title);
        t.setTextSize(15);
        t.setTypeface(null, android.graphics.Typeface.BOLD);
        t.setTextColor(Color.parseColor(C_MINT));
        texts.addView(t);

        TextView s = new TextView(this);
        s.setText(subtitle);
        s.setTextSize(11);
        s.setTextColor(Color.parseColor("#BF25291C"));
        s.setPadding(0, dp(2), 0, 0);
        texts.addView(s);
        card.addView(texts);

        TextView right = new TextView(this);
        right.setText(rightText);
        right.setTextSize(13);
        right.setTypeface(null, android.graphics.Typeface.BOLD);
        right.setTextColor(Color.parseColor(C_INK));
        right.setPadding(dp(8), 0, 0, 0);
        card.addView(right);

        if (enabled && onClick != null) {
            card.setClickable(true);
            card.setOnClickListener(onClick);
        }
        parent.addView(card);
        return right;
    }

    private void renderSettingsScreen() {
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.parseColor(C_MINT));

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(dp(20), dp(12), dp(20), dp(24));

        TextView title = new TextView(this);
        title.setText("系統參數與偏好設定");
        title.setTextSize(21);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 0, 0, dp(4));
        layout.addView(title);

        // 1. 提示靈敏度：只調整提醒門檻與持續秒數，20/5/5 冷卻規則不變 (點擊循環切換)
        final String[] sensLabels = { "低", "標準", "高" };
        final String[] sensDescs = {
                "疲勞分數 ≥70 持續 15 秒才提醒（較少打擾）",
                "疲勞分數 ≥65 持續 10 秒才提醒（推薦）",
                "疲勞分數 ≥58 持續 5 秒即提醒（較敏感）" };
        int savedLevel = EfdNativeBridge.getSensitivity();
        final boolean engineReady = savedLevel >= 0;
        if (engineReady) mSensitivityLevel = savedLevel;
        final TextView[] sensRight = new TextView[1];
        final TextView[] sensSub = new TextView[1];
        sensRight[0] = addSettingsCard(layout, "提示靈敏度",
                engineReady ? sensDescs[mSensitivityLevel] : "請先回到監控中心開始偵測後再調整",
                engineReady ? sensLabels[mSensitivityLevel] + "  ›" : "--", engineReady, v -> {
                    mSensitivityLevel = (mSensitivityLevel + 1) % 3;
                    EfdNativeBridge.setSensitivity(mSensitivityLevel);
                    sensRight[0].setText(sensLabels[mSensitivityLevel] + "  ›");
                    if (sensSub[0] != null) sensSub[0].setText(sensDescs[mSensitivityLevel]);
                });
        // 取得同一張卡片的說明文字，切換時一併更新
        LinearLayout sensCard = (LinearLayout) sensRight[0].getParent();
        sensSub[0] = (TextView) ((LinearLayout) sensCard.getChildAt(0)).getChildAt(1);

        // 2. 通知狀態與背景執行設定
        boolean notifyOn = EfdNotificationHudManager.canPostFatigueAlerts(this);
        addSettingsCard(layout, "通知與背景執行設定",
                "依手機廠牌設定自動啟動與電池不受限制，劃掉 App 後仍會提醒",
                notifyOn ? "通知開啟  ›" : "通知關閉  ›", true, v -> showBackgroundSettingsGuide());

        // 3. 測試背景通知
        addSettingsCard(layout, "測試背景通知",
                "10 秒後送出測試通知，可在這段時間把 EFD 劃掉確認背景運作",
                "›", true, v -> onTestBackgroundAlertClicked());

        // 4. 重設眼動監測
        addSettingsCard(layout, "重新校準眼動基準",
                "換了使用環境、光線或配戴眼鏡時，重新校準可提升準確度",
                "›", true, v -> setStage(UIStage.CalibrationInstruction));

        // 5. 錯誤回報
        addSettingsCard(layout, "複製錯誤資訊",
                "把目前偵測狀態與手機資訊複製成文字（不含影像），貼給研究人員",
                "›", true, v -> copyDiagnosticReport());
        addSettingsCard(layout, "匯出診斷畫面",
                "擷取 5 張偵測畫面存到相簿「EFD」，並複製數值（含臉部影像）",
                "›", true, v -> onExportDiagnosticClicked());

        // 6. 自動科研同步 (恆定開啟)
        addSettingsCard(layout, "自動科研同步",
                "每晚 21:00 後上傳當日數值；錯過時會自動補傳",
                "已開啟", false, null).setTextColor(Color.parseColor(C_MINT));

        // 底部返回按鈕
        Button returnBtn = dashButton("返回監控中心", C_WHITE, C_MINT, 20);
        returnBtn.getLayoutParams().height = dp(48);
        returnBtn.setTextSize(16);
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
        LinearLayout layout = createMintPage(true);
        addPageTitle(layout, "14 天施測已完成", 24);
        addPageSubtitle(layout, "感謝你的參與，還差最後一步");

        addInfoCard(layout, "請填寫後測問卷",
                "點擊下方按鈕前往 Google 表單填寫後測問卷，完成後即結束本次實驗流程。\n"
                + "問卷網址：https://forms.gle/y5f1jTnrrtsxz65G9");

        Button surveyBtn = dashButton("前往填寫後測問卷 (Google 表單)", C_WHITE, C_MINT, 24);
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

        Button returnBtn = dashButton("返回即時監控中心", "#33FFFFFF", C_WHITE, 10);
        returnBtn.setOnClickListener(v -> setStage(UIStage.MainDashboard));
        layout.addView(returnBtn);
    }

    // -------------------------------------------------------------------------
    // 階段 9：問卷提交完成
    // -------------------------------------------------------------------------
    private void renderQuestionnaireSubmittedScreen() {
        LinearLayout layout = createMintPage(true);
        addPageTitle(layout, "問卷已成功送出！", 24);
        addPageSubtitle(layout, "感謝你 14 天以來的協助");

        String unlockToken = EfdNativeBridge.getUnlockToken();
        addInfoCard(layout, "科研憑證",
                (unlockToken.isEmpty() ? "尚未取得" : unlockToken)
                + "\n\n現在你可以關閉或解除安裝本系統。");

        Button exitBtn = dashButton("關閉並結束應用程式", C_CORAL, C_WHITE, 24);
        exitBtn.setOnClickListener(v -> finish());
        layout.addView(exitBtn);
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

                        mLastTelemetryJson = obj.toString();
                        if (mCurrentStage == UIStage.MainDashboard && mDashScoreText != null) {
                            if (!hasFace) {
                                // 顯示未偵測到臉的原因、相機畫面與臉框尺寸，只看截圖也能判斷是太大、太小或其他原因
                                mDashStatusText.setText("未偵測到臉：" + presenceReasonText(obj));
                                mDashStatusText.setTextColor(Color.parseColor(C_CORAL));
                                mDashScoreText.setText("疲勞評分：未就座 (暫停計算)");
                                org.json.JSONArray face = obj.optJSONArray("faceBox");
                                org.json.JSONArray skin = obj.optJSONArray("skinBox");
                                String stage = obj.optString("failStage", "");
                                mDashMetricsText.setText(String.format(java.util.Locale.getDefault(),
                                        "相機畫面 %dx%d %.0f fps | 卡在 %s | 膚色區 %dx%d | 臉框 %dx%d",
                                        obj.optInt("frameW", 0), obj.optInt("frameH", 0), obj.optDouble("measuredFps", 0.0),
                                        stage.isEmpty() ? "-" : stage,
                                        skin != null ? skin.optInt(2) : 0, skin != null ? skin.optInt(3) : 0,
                                        face != null ? face.optInt(2) : 0, face != null ? face.optInt(3) : 0));
                                mDashEarValue.setText("--");
                                mDashPerclosValue.setText("離座暫停");
                                mDashCiValue.setText("--");
                                mDashScoreValue.setText("暫停中");
                            } else {
                                mDashScoreText.setText(String.format(java.util.Locale.getDefault(),
                                        "閾值 %.3f ｜ 眨眼 %d 次/分", threshold, blinkRate));
                                if (cooldownState == 1 && cooldownRemaining > 0) {
                                    int mins = cooldownRemaining / 60;
                                    int secs = cooldownRemaining % 60;
                                    mDashStatusText.setText(String.format(java.util.Locale.getDefault(),
                                            "生理狀態：重度疲勞【防打擾冷卻中 剩餘 %d分%d秒】", mins, secs));
                                    mDashStatusText.setTextColor(Color.parseColor(C_CORAL));
                                } else if (mCurrentLevel == 2) {
                                    mDashStatusText.setText("生理狀態：你的眼睛處於疲勞狀態，請適當休息");
                                    mDashStatusText.setTextColor(Color.parseColor(C_CORAL));
                                } else if (mCurrentLevel == 1) {
                                    mDashStatusText.setText("生理狀態：用眼注意 (Attention)");
                                    mDashStatusText.setTextColor(Color.parseColor("#C98A12"));
                                } else {
                                    mDashStatusText.setText("生理狀態：清醒專注 (Normal)");
                                    mDashStatusText.setTextColor(Color.parseColor(C_MINT));
                                }
                                mDashMetricsText.setText(obj.optBoolean("tracking", false) ? "臉部追蹤中 (閉眼或低頭仍算在座)" : "");
                                mDashEarValue.setText(String.format(java.util.Locale.getDefault(), "%.3f", currentEar));
                                mDashPerclosValue.setText(String.format(java.util.Locale.getDefault(), "%.1f%%", perclos));
                                mDashCiValue.setText(String.format(java.util.Locale.getDefault(), "%.2f", complexityCI));
                                mDashScoreValue.setText(String.format(java.util.Locale.getDefault(), "%.1f", mCurrentScore));
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
