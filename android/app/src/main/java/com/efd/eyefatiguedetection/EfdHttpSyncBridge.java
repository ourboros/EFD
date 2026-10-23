package com.efd.eyefatiguedetection;

import android.util.Log;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class EfdHttpSyncBridge {
    private static final String TAG = "EFD_HttpSyncBridge";
    public static final String GOOGLE_SHEETS_ENDPOINT = "https://script.google.com/macros/s/AKfycbzwniwhgb6exjMC67tb_QMNOmp5xDCKKaS7aQs99bXmFv-qFY-U1ZhXcHLUFrJIFvyb5Q/exec";

    private static final ExecutorService sExecutor = Executors.newSingleThreadExecutor();

    public interface SyncCallback {
        void onSyncResult(boolean success, String responseMessage);
    }

    public static void uploadFullPayloadAsync(String jsonPayload, SyncCallback callback) {
        sExecutor.execute(() -> {
            try {
                String jsonStr = jsonPayload;
                if (jsonStr == null || jsonStr.trim().isEmpty()) {
                    jsonStr = EfdNativeBridge.getCloudSyncPayloadJson();
                }

                if (jsonStr == null || jsonStr.trim().isEmpty()) {
                    Log.w(TAG, "無可用的同步 Payload JSON，取消上傳");
                    if (callback != null) callback.onSyncResult(false, "Empty JSON");
                    return;
                }

                Log.i(TAG, "正在發送全欄位 Google 試算表 JSON 封裝:\n" + jsonStr);

                URL url = new URL(GOOGLE_SHEETS_ENDPOINT);
                HttpURLConnection conn = (HttpURLConnection) url.openConnection();
                conn.setRequestMethod("POST");
                conn.setRequestProperty("Content-Type", "application/json; charset=UTF-8");
                conn.setConnectTimeout(15000);
                conn.setReadTimeout(15000);
                conn.setDoOutput(true);
                conn.setInstanceFollowRedirects(true);

                try (OutputStream os = conn.getOutputStream()) {
                    byte[] input = jsonStr.getBytes(StandardCharsets.UTF_8);
                    os.write(input, 0, input.length);
                }

                int code = conn.getResponseCode();
                Log.i(TAG, "Google Apps Script 伺服器回應 Code: " + code);

                BufferedReader br;
                if (code >= 200 && code < 300) {
                    br = new BufferedReader(new InputStreamReader(conn.getInputStream(), StandardCharsets.UTF_8));
                } else {
                    br = new BufferedReader(new InputStreamReader(conn.getErrorStream(), StandardCharsets.UTF_8));
                }

                StringBuilder response = new StringBuilder();
                String line;
                while ((line = br.readLine()) != null) {
                    response.append(line.trim());
                }
                br.close();

                String respStr = response.toString();
                Log.i(TAG, "Google 試算表回應內容: " + respStr);

                boolean success = (code == 200 || code == 302) &&
                        (respStr.contains("success") || respStr.contains("OK") || respStr.contains("status"));

                if (callback != null) {
                    callback.onSyncResult(success, respStr);
                }
            } catch (Throwable t) {
                Log.e(TAG, "Google 試算表上傳失敗: " + t.getMessage(), t);
                if (callback != null) {
                    callback.onSyncResult(false, t.getMessage());
                }
            }
        });
    }

    public static void uploadEyelidDataAsync(String subjectUuid, int studyDay, String questionnaireData, SyncCallback callback) {
        uploadFullPayloadAsync(null, callback);
    }
}
