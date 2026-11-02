# 測試影像

| 檔案 | 來源 | 用途 |
|---|---|---|
| `portrait_480.ppm` | MediaPipe 官方公開測試照片 `https://storage.googleapis.com/mediapipe-assets/portrait.jpg` (Apache 2.0)，縮成寬 480 並存成 P6 PPM | 測試 35：ONNX 臉部模型 (Windows) 與 MediaPipe 官方結果比對 |

測試 35 的參考值 (EAR 左 0.2060 / 右 0.2000、特徵點 1 / 33 / 263 座標) 由 MediaPipe Python 套件 (FaceLandmarker，IMAGE 模式，同一個 `face_landmarker.task`) 在這張 PPM 上計算。
