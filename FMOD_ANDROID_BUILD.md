# FMOD Android Build Setup for PizzaScotch

## Prerequisites

You need:
1. **Android NDK** (r21 or newer)
2. **FMOD Android SDK** (download from https://www.fmod.com/download)
3. **CMake** 3.21+

## Environment Setup

Before building, export these variables:

```bash
# Path to your Android NDK
export ANDROID_NDK=/path/to/android-ndk

# Path to FMOD SDK root (where api/, lib/ folders are)
export FMOD_DIR=/path/to/fmod

# Example (macOS/Linux typical setup):
export ANDROID_NDK=$HOME/Library/Android/sdk/ndk/25.1.8937393
export FMOD_DIR=$HOME/fmod/api/lowlevel

# Or if using environment variable FMOD_HOME:
export FMOD_HOME=/path/to/fmod
```

## FMOD SDK Structure

Your FMOD directory should have this structure for Android to work:

```
fmod/
├── api/
│   └── lowlevel/
│       ├── inc/
│       │   └── fmod.h
│       └── lib/
│           ├── aarch64-android/
│           │   └── libfmod.so
│           ├── arm64-v8a/
│           │   └── libfmod.so
│           └── x86_64-android/
│               └── libfmod.so
```

## Build Commands

### For ARM64 (arm64-v8a) - Most Common

```bash
export FMOD_DIR=/path/to/fmod
export ANDROID_NDK=/path/to/android-ndk

cmake -S . -B build-android-arm64 \
  -DPLATFORM=android \
  -DAUDIO_BACKEND=fmod \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-android-arm64 --config Release
```

### For x86_64 (if needed)

```bash
cmake -S . -B build-android-x86_64 \
  -DPLATFORM=android \
  -DAUDIO_BACKEND=fmod \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=x86_64 \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-android-x86_64 --config Release
```

## What Happens During Build

The CMake script will:

1. **Look for FMOD** in:
   - `$FMOD_DIR/api/lowlevel/lib/aarch64-android/libfmod.so`
   - `$FMOD_DIR/api/lowlevel/lib/arm64-v8a/libfmod.so`
   - `$FMOD_DIR/api/lowlevel/inc/fmod.h`

2. **If FMOD is found:**
   - ✅ Defines `USE_FMOD` in compilation
   - ✅ Links against `libfmod.so`
   - ✅ Audio system will use `FmodAudioSystem_create()`
   - Output: `Found FMOD for Android: /path/to/libfmod.so`

3. **If FMOD is NOT found:**
   - ⚠️ Defines `BUTTERSCOTCH_FMOD_STUB` instead
   - ⚠️ Audio will be silent (no-op backend)
   - ⚠️ Build succeeds but without real audio
   - Output: `FMOD SDK not found for Android; building stub FMOD backend`

## Verify FMOD Was Found

After running cmake, check the output for:

```
Found FMOD for Android: /path/to/fmod/api/lowlevel/lib/aarch64-android/libfmod.so
```

If you see this, FMOD is correctly integrated. If you see the warning about stub, your `$FMOD_DIR` path is wrong.

## Troubleshooting

### CMake says "FMOD SDK not found for Android"

Check:
1. `echo $FMOD_DIR` - is it set correctly?
2. `ls $FMOD_DIR/api/lowlevel/inc/fmod.h` - does this file exist?
3. `ls $FMOD_DIR/api/lowlevel/lib/aarch64-android/` - does the lib folder exist?

### Wrong FMOD_DIR path

If FMOD_DIR points to the SDK root, it should be:
- ❌ `/home/user/fmod` (just the root)
- ✅ `/home/user/fmod/api/lowlevel` (with api/lowlevel)

Or set it to the root and CMake will find `api/lowlevel` automatically.

### Linker errors about libfmod.so

Make sure the `.so` file is in the correct ABI folder matching your `ANDROID_ABI`.

## Final Integration Notes

Once compiled with FMOD:

1. The `libbutterscotch.so` will be built with FMOD audio support
2. You must include `libfmod.so` in your APK's `lib/arm64-v8a/` directory
3. The Android frontend must have permission to load the native libraries

Check the Android frontend gradle file to ensure it's packaging the FMOD library correctly.

## Next Steps

After successful build:
1. Verify `libbutterscotch.so` was created in `build-android-arm64/`
2. Copy `libfmod.so` from your FMOD SDK to the Android project's lib directory
3. Build the APK using Android Studio or Gradle
4. Test on device

---

**Branch:** `feat/android-fmod-backend`  
**Modified Files:**
- `CMakeLists.txt` - Added FMOD detection for Android
- `src/android/main.c` - Audio backend selection with `#ifdef USE_FMOD`
