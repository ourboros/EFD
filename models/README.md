# 臉部特徵點模型

| 檔案 | 用途 | 來源 |
|---|---|---|
| `face_landmarker.task` | Android (MediaPipe Tasks)；同一份複製在 `android/app/src/main/assets/` | Google MediaPipe 官方 (Apache 2.0)：`https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/latest/face_landmarker.task` |
| `face_detector.onnx` | Windows (ONNX Runtime)：臉部偵測，輸入 1×128×128×3，輸出 896 個 anchor | 由 `face_landmarker.task` 內的 `face_detector.tflite` 轉換 |
| `face_landmarks.onnx` | Windows (ONNX Runtime)：478 點特徵點，輸入 1×256×256×3，輸出 1434 (=478×3) | 由 `face_landmarker.task` 內的 `face_landmarks_detector.tflite` 轉換 |

兩個平台使用同一個模型，研究資料的量測口徑一致。

## 重新產生 ONNX

`.task` 是 zip 檔 (`Expand-Archive` 只認 `.zip` 副檔名，請用 `unzip` 或先改副檔名)。

```powershell
py -3.12 -m venv .venv-models
.\.venv-models\Scripts\python.exe -m pip install tensorflow tf2onnx==1.17.0 onnx onnxruntime
unzip -o models/face_landmarker.task -d models/extracted
.\.venv-models\Scripts\python.exe -m tf2onnx.convert --tflite models\extracted\face_detector.tflite --output models\face_detector.onnx --opset 13
.\.venv-models\Scripts\python.exe -m tf2onnx.convert --tflite models\extracted\face_landmarks_detector.tflite --output models\face_landmarks.onnx --opset 13
```

2026-11-02 轉換後以相同輸入比對 tflite 與 ONNX 輸出：最大相對誤差分別為 1.4e-6 與 1.5e-4。
