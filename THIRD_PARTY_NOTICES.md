# 第三方元件與授權 (Third-Party Notices)

EFD 使用下列第三方元件。各元件依其授權條款散布；授權全文見各專案網址。

## 臉部偵測

| 元件 | 版本 | 用途 | 授權 |
|---|---|---|---|
| MediaPipe Tasks Vision (`com.google.mediapipe:tasks-vision`) | 0.10.35 | Android 臉部特徵點偵測 | Apache License 2.0，Copyright Google LLC — https://github.com/google-ai-edge/mediapipe |
| MediaPipe Face Landmarker 模型 (`models/face_landmarker.task`) | float16 / latest | 臉部偵測與 478 點特徵點模型 (Android 直接使用) | Apache License 2.0，Copyright Google LLC |
| `models/face_detector.onnx`、`models/face_landmarks.onnx` | — | 由上列模型以 tf2onnx 轉換，供 Windows 使用 | 衍生自 Face Landmarker 模型，Apache License 2.0 |
| ONNX Runtime | 1.31.0 | Windows 執行上列 ONNX 模型 | MIT License，Copyright (c) Microsoft Corporation — https://github.com/microsoft/onnxruntime (Windows 安裝包的 `licenses/` 附授權全文與 ThirdPartyNotices) |

MediaPipe 間接相依的 Java 程式庫隨 Android App 散布：Guava、Flogger、AutoValue (Apache License 2.0)；Protocol Buffers (BSD 3-Clause License，Copyright Google Inc.)；Google datatransport (Apache License 2.0，App 已移除其回傳後端，不送出使用統計)。

## Android 程式庫

| 元件 | 版本 | 授權 |
|---|---|---|
| AndroidX AppCompat / Core / CardView | 1.7.0 / 1.13.1 / 1.0.0 | Apache License 2.0 |
| AndroidX CameraX (core、camera2、lifecycle、view) | 1.3.4 | Apache License 2.0 |
| AndroidX Lifecycle | 2.8.5 | Apache License 2.0 |
| Kotlin 標準函式庫 | 1.8.22 | Apache License 2.0 |

## Apache License 2.0 摘要

依 Apache License 2.0 散布時須附上授權條款與原始著作權聲明，並註明修改之處。EFD 未修改 MediaPipe 程式庫；ONNX 模型為格式轉換，權重未經修改。授權全文：https://www.apache.org/licenses/LICENSE-2.0

## MIT License (ONNX Runtime)

```
MIT License

Copyright (c) Microsoft Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
