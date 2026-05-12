# 빌드 & 배포 가이드 (gst-android-camera)

이 문서는 소스 코드를 수정한 뒤 **빌드 → 폰에 설치 → 실행 → 로그 확인**까지의 전체 사이클을 다룹니다.
CLI(터미널) 방식과 GUI(Android Studio) 방식을 둘 다 설명합니다.

---

## 0. 환경 전제

아래 설정은 이미 완료되어 있어야 합니다 (초기 세팅 때 진행함):

| 항목 | 경로 / 값 |
|------|-----------|
| Android SDK | `/home/kimrihyeon/Android/Sdk` |
| Android NDK | `/home/kimrihyeon/Android/Sdk/ndk/25.2.9519653` |
| GStreamer Android SDK | `/home/kimrihyeon/gstreamer-android/1.26.1` |
| Android Studio | `/opt/android-studio-2025.2.3/android-studio` |
| 내장 JDK (JBR 21) | `/opt/android-studio-2025.2.3/android-studio/jbr` |
| `gradle.properties` | `gstAndroidRoot=/home/kimrihyeon/gstreamer-android/1.26.1` 줄 포함 |
| 테스트 기기 | 갤럭시 S10e (SM-G970N, Android 12) |

기기(Samsung S10e) 사전 조건:

- **개발자 옵션 활성화** (설정 → 휴대폰 정보 → 소프트웨어 정보 → "빌드번호" 7번 탭)
- **USB 디버깅 ON** (개발자 옵션 내)
- USB 연결 시 알림에서 **"파일 전송(MTP)"** 선택
- 최초 연결 시 **"USB 디버깅 허용"** 팝업에 "항상 허용" 체크 후 허용

연결 상태 확인:
```bash
/home/kimrihyeon/Android/Sdk/platform-tools/adb devices -l
```
`R39M305B37W    device    model:SM_G970N` 처럼 `device` 상태여야 합니다.
(`unauthorized`면 허용 팝업 미수락, 목록 비어있으면 USB/드라이버 문제.)

---

## 1. 어떤 파일을 수정하게 되나

| 수정하려는 것 | 건드릴 파일 |
|---------------|-------------|
| **UI / 레이아웃** | `app/src/main/res/layout/*.xml` (예: `activity_main.xml`, `resolution.xml`, `white_balance.xml`) |
| **문자열 / 색상** | `app/src/main/res/values/*.xml` (`strings.xml`, `colors.xml`, `styles.xml`) |
| **카메라/앱 로직 (Java)** | `app/src/main/java/org/freedesktop/gstreamer/camera/CameraActivity.java`<br>`.../camera/GstAhc.java` |
| **GStreamer 파이프라인 (네이티브 C)** | `app/src/main/jni/android_camera.c` |
| **네이티브 빌드 설정** | `app/src/main/jni/Android.mk`, `app/src/main/jni/Application.mk` |
| **Gradle 빌드 설정** | `app/build.gradle`, 루트 `build.gradle` |
| **권한 / 앱 메타데이터** | `app/src/main/AndroidManifest.xml` |

> 중요: `Android.mk`의 `GSTREAMER_PLUGINS_*` 목록에 없는 GStreamer 엘리먼트를 파이프라인에서 쓰면
> 런타임에 `no element "..."` 오류가 납니다. 새 엘리먼트를 쓰려면 `Android.mk`에 해당 플러그인을 추가해야 합니다.

---

## 2. CLI(터미널) 방식

### 2.1 매 세션 최초 1회 — 환경변수 세팅

Android Studio의 내장 JDK(JBR)를 쓰도록 `JAVA_HOME`을 설정합니다.

```bash
export JAVA_HOME=/opt/android-studio-2025.2.3/android-studio/jbr
export PATH=$JAVA_HOME/bin:$PATH
```

매번 치기 싫으면 `~/.bashrc` 맨 아래에 위 두 줄을 추가하고 `source ~/.bashrc`.

확인:
```bash
java -version   # openjdk version "21.x.x" 가 나오면 OK
```

### 2.2 기본 사이클 — 수정 → 빌드 → 설치 → 실행 → 로그

```bash
cd /home/kimrihyeon/workdir/gst-android-camera

# (1) 소스 수정 — 에디터/IDE로 원하는 파일 편집

# (2) 빌드 + 설치 한 번에 (권장)
./gradlew installDebug

# (3) 앱 실행
adb shell monkey -p org.freedesktop.gstreamer.examples.camera -c android.intent.category.LAUNCHER 1

# (4) 실시간 로그 보기 (Ctrl+C로 중단)
adb logcat | grep -iE "gstreamer|ahc|camera|AndroidRuntime"
```

> `adb`는 `/home/kimrihyeon/Android/Sdk/platform-tools/adb`에 있습니다.
> 자주 쓴다면 PATH에 추가해두면 편합니다:
> ```bash
> export PATH=/home/kimrihyeon/Android/Sdk/platform-tools:$PATH
> ```

### 2.3 개별 Gradle 태스크

| 목적 | 명령 |
|------|------|
| APK만 빌드 (설치 안 함) | `./gradlew assembleDebug` |
| 빌드 + 기기에 설치 | `./gradlew installDebug` |
| 기기에서 앱 제거 | `./gradlew uninstallDebug` |
| 빌드 결과물 완전 삭제 | `./gradlew clean` |
| 생성된 APK 위치 | `app/build/outputs/apk/debug/app-debug.apk` |

### 2.4 자주 쓰는 adb 명령

```bash
# 연결된 기기 목록
adb devices -l

# 앱 강제 종료
adb shell am force-stop org.freedesktop.gstreamer.examples.camera

# 앱 실행
adb shell monkey -p org.freedesktop.gstreamer.examples.camera -c android.intent.category.LAUNCHER 1

# 앱 제거
adb uninstall org.freedesktop.gstreamer.examples.camera

# APK 수동 설치 (기존 버전 유지하며 업데이트: -r 옵션)
adb install -r app/build/outputs/apk/debug/app-debug.apk

# 로그 - GStreamer 관련만
adb logcat -s GStreamer:* GLib:* GLib-GObject:* *:E

# 로그 - 앱 태그만 + 에러/경고
adb logcat '*:W' | grep gstreamer

# 로그 버퍼 초기화 후 깨끗하게 보기
adb logcat -c && adb logcat | grep -iE "gstreamer|ahc"
```

### 2.5 전형적인 "수정 → 확인" 한 줄 명령

```bash
./gradlew installDebug && \
  adb shell am force-stop org.freedesktop.gstreamer.examples.camera && \
  adb shell monkey -p org.freedesktop.gstreamer.examples.camera -c android.intent.category.LAUNCHER 1 && \
  adb logcat -c && \
  adb logcat | grep -iE "gstreamer|ahc|AndroidRuntime"
```

빌드 → 기존 앱 종료 → 새로 실행 → 로그 스트림을 한 줄로 이어서 처리합니다.

---

## 3. GUI(Android Studio) 방식

### 3.1 프로젝트 열기

1. Android Studio 실행
2. **File → Open → `/home/kimrihyeon/workdir/gst-android-camera`** 선택 (프로젝트 **루트 폴더**, app 폴더 아님)
3. 우측 하단 "Gradle Sync" 팝업이 뜨면 자동 진행됨. 끝날 때까지 대기

### 3.2 한 번만 확인할 설정

- **File → Project Structure → SDK Location**
  - Android SDK location: `/home/kimrihyeon/Android/Sdk`
  - Android NDK location: `/home/kimrihyeon/Android/Sdk/ndk/25.2.9519653`
  - JDK: 비워두면 내장 JBR 사용 (권장)
- **File → Settings → Build, Execution, Deployment → Build Tools → Gradle**
  - Gradle JDK: `Embedded JDK (jbr-21)` 선택

### 3.3 기기 선택

상단 툴바의 기기 선택 드롭다운(▶ 버튼 옆)에서 **"SM G970N"** 선택.
안 뜨면:
- USB 연결 재확인 + USB 디버깅 허용 팝업 재승인
- **Tools → Device Manager**에서 "Physical" 탭에 기기가 있는지 확인

### 3.4 실행/디버그

| 버튼 | 동작 |
|------|------|
| ▶ (Run 'app') — `Shift+F10` | 빌드 + 설치 + 실행 |
| 🐞 (Debug 'app') — `Shift+F9` | 빌드 + 설치 + 디버거 연결하여 실행 |
| 🔨 (Build) — `Ctrl+F9` | APK만 빌드 |
| Apply Changes — `Ctrl+F10` | 리소스/일부 코드 변경만 빠르게 반영 (재설치 없이) |

일반 사이클은 그냥 **▶ Run** 버튼만 누르면 됩니다. Gradle이 변경 감지해서 필요한 것만 다시 빌드.

### 3.5 로그 보기

- **View → Tool Windows → Logcat** (단축키: `Alt+6`)
- 상단 필터 바에 다음 중 하나 입력:
  - `package:mine` — 우리 앱 로그만
  - `tag:GStreamer` — GStreamer 태그만
  - `level:error` — 에러만
  - 복합: `package:mine level:warn`

### 3.6 기기에 앱 제거

**Run → Edit Configurations → Before launch**에서 "Gradle task: uninstallDebug" 추가하거나,
터미널에서 `adb uninstall org.freedesktop.gstreamer.examples.camera`.

### 3.7 유용한 Android Studio 기능

- **Logcat 필터 저장**: 자주 쓰는 필터를 드롭다운에서 저장 가능
- **Layout Inspector** (Tools → Layout Inspector): 런타임 UI 구조 확인
- **Profiler** (View → Tool Windows → Profiler): CPU/메모리/네트워크 실시간 모니터링
- **Device File Explorer** (View → Tool Windows → Device Explorer): 폰 파일시스템 탐색
- **Ctrl+N**: 클래스 검색 / **Ctrl+Shift+N**: 파일 검색 / **Ctrl+Shift+F**: 전체 검색

---

## 4. 네이티브 코드(C) 수정 시 주의사항

`app/src/main/jni/android_camera.c` 등 C 파일을 수정하면 **ndk-build**가 다시 돌아야 합니다.
보통은 Gradle이 자동 감지해주지만, 변경이 반영 안 되는 것 같으면:

```bash
# 네이티브 빌드 캐시 삭제
rm -rf app/.cxx app/build/intermediates/cxx
./gradlew clean
./gradlew installDebug
```

`Android.mk`에 새 GStreamer 플러그인을 추가했을 때도 동일하게 clean 후 재빌드가 안전합니다.

---

## 5. 트러블슈팅 체크리스트

| 증상 | 확인사항 |
|------|----------|
| `JAVA_HOME is not set` | 2.1 환경변수 세팅 누락. `export JAVA_HOME=...` 다시 실행 |
| `GSTREAMER_ROOT_ANDROID must be set` | `gradle.properties`의 `gstAndroidRoot=...` 누락 혹은 경로 오타 |
| `NDK not configured` / NDK 버전 미스매치 | `app/build.gradle`의 `ndkVersion "25.2.9519653"`과 설치된 NDK 버전 일치 확인 |
| `adb devices`에 기기 안 보임 | USB 디버깅 ON / MTP 모드 / 케이블 교체 / `adb kill-server && adb start-server` |
| `unauthorized` 상태 | 폰 화면의 "USB 디버깅 허용" 팝업 승인. 안 뜨면 개발자 옵션 → "USB 디버깅 승인 취소" 후 재연결 |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | 서명 달라진 것. `adb uninstall org.freedesktop.gstreamer.examples.camera` 후 재설치 |
| 앱 실행 시 검은 화면 / 프리뷰 안 나옴 | 카메라 권한 미승인. 앱 강제종료 후 재실행하면 권한 재요청 |
| `no element "xxx"` 런타임 오류 | `Android.mk`의 `GSTREAMER_PLUGINS_*`에 해당 플러그인 없음. 추가 후 clean 재빌드 |
| APK 설치는 되는데 실행 시 `UnsatisfiedLinkError` | arm64-v8a/armeabi-v7a 중 기기 아키텍처 라이브러리 누락. `abiFilters` 확인 |
| Gradle Sync 실패 | Android Studio → File → Invalidate Caches → Invalidate and Restart |

로그를 읽을 때 `AndroidRuntime` 태그의 **FATAL EXCEPTION** 블록 + 바로 위 `GStreamer` 태그 메시지를
같이 보면 원인이 빠르게 드러납니다.

---

## 6. 참고 경로 한눈에

```
/home/kimrihyeon/workdir/gst-android-camera/          ← 프로젝트 루트
├─ app/
│  ├─ build.gradle                                    ← Android/Gradle 빌드 설정
│  ├─ src/main/
│  │  ├─ AndroidManifest.xml
│  │  ├─ java/org/freedesktop/gstreamer/
│  │  │  ├─ camera/CameraActivity.java                ← 메인 액티비티
│  │  │  ├─ camera/GstAhc.java                        ← Camera Params 래퍼
│  │  │  └─ GStreamer.java
│  │  ├─ jni/
│  │  │  ├─ android_camera.c                          ← GStreamer 파이프라인 (C)
│  │  │  ├─ Android.mk
│  │  │  └─ Application.mk
│  │  └─ res/                                         ← 레이아웃/문자열/색상
│  └─ build/outputs/apk/debug/app-debug.apk           ← 빌드 결과 APK
├─ build.gradle
├─ gradle.properties                                  ← gstAndroidRoot 여기
├─ local.properties                                   ← sdk.dir 여기
└─ gradlew                                            ← Gradle 래퍼
```
