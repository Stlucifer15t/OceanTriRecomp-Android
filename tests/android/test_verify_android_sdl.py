import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("verify_android_sdl", ROOT / "scripts" / "verify_android_sdl.py")
verify = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(verify)


class AndroidSdlContractTests(unittest.TestCase):
    def make_tree(self, root: Path, pad_signature: str = "(III)Z", java_pad_args: str = "int device_id, int keycode, int scancode", java_version=(3, 4, 16), load_sdl3=False):
        sdl = root / "sdl"
        (sdl / "src" / "core" / "android").mkdir(parents=True)
        (sdl / "include" / "SDL3").mkdir(parents=True)
        java = root / "java"
        java.mkdir()
        major, minor, micro = java_version
        (sdl / "include" / "SDL3" / "SDL_version.h").write_text(
            f"#define SDL_MAJOR_VERSION {major}\n#define SDL_MINOR_VERSION {minor}\n#define SDL_MICRO_VERSION {micro}\n"
        )
        (sdl / "src" / "core" / "android" / "SDL_android.c").write_text(
            """
static JNINativeMethod SDLActivity_tab[] = {
    { "nativeSetupJNI", "()V", SDL_JAVA_INTERFACE(nativeSetupJNI) }
};
static JNINativeMethod SDLInputConnection_tab[] = {
    { "nativeCommitText", "(Ljava/lang/String;I)V", SDL_JAVA_INTERFACE_INPUT_CONNECTION(nativeCommitText) }
};
static JNINativeMethod SDLAudioManager_tab[] = {
    { "nativeSetupJNI", "()V", SDL_JAVA_AUDIO_INTERFACE(nativeSetupJNI) }
};
static JNINativeMethod SDLControllerManager_tab[] = {
    { "nativeSetupJNI", "()V", SDL_JAVA_CONTROLLER_INTERFACE(nativeSetupJNI) },
    { "onNativePadDown", "%s", SDL_JAVA_CONTROLLER_INTERFACE(onNativePadDown) },
    { "onNativePadUp", "%s", SDL_JAVA_CONTROLLER_INTERFACE(onNativePadUp) }
};
""" % (pad_signature, pad_signature)
        )
        (java / "SDLActivity.java").write_text(
            f"""
public class SDLActivity {{
    private static final int SDL_MAJOR_VERSION = {major};
    private static final int SDL_MINOR_VERSION = {minor};
    private static final int SDL_MICRO_VERSION = {micro};
    public static native void nativeSetupJNI();
}}
"""
        )
        (java / "SDLInputConnection.java").write_text(
            "public class SDLInputConnection { private native void nativeCommitText(String text, int newCursorPosition); }\n"
        )
        (java / "SDLAudioManager.java").write_text(
            "public class SDLAudioManager { public static native void nativeSetupJNI(); }\n"
        )
        (java / "SDLControllerManager.java").write_text(
            f"""
public class SDLControllerManager {{
    static native void nativeSetupJNI();
    static public native boolean onNativePadDown({java_pad_args});
    static public native boolean onNativePadUp({java_pad_args});
}}
"""
        )
        libs = '"png16", "SDL3", "main"' if load_sdl3 else '"png16", "main"'
        app = root / "OceanTriActivity.java"
        app.write_text(
            f"""
public class OceanTriActivity extends SDLActivity {{
    @Override protected String[] getLibraries() {{ return new String[] {{ {libs} }}; }}
}}
"""
        )
        return sdl, java, app

    def test_matching_contract_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            sdl, java, app = self.make_tree(Path(tmp))
            self.assertEqual(verify.verify_source(sdl, java, app), 6)

    def test_native_two_arg_pad_vs_java_three_arg_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            sdl, java, app = self.make_tree(Path(tmp), pad_signature="(II)Z")
            with self.assertRaisesRegex(verify.VerificationError, "onNativePadDown.*native \\(II\\)Z, Java \\(III\\)Z"):
                verify.verify_source(sdl, java, app)

    def test_java_two_arg_pad_vs_native_three_arg_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            sdl, java, app = self.make_tree(Path(tmp), java_pad_args="int device_id, int keycode")
            with self.assertRaisesRegex(verify.VerificationError, "onNativePadDown.*native \\(III\\)Z, Java \\(II\\)Z"):
                verify.verify_source(sdl, java, app)

    def test_version_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            sdl, java, app = self.make_tree(Path(tmp), java_version=(3, 4, 4))
            # Keep the Java version constants at 3.4.4 while native header says 3.4.16.
            (sdl / "include" / "SDL3" / "SDL_version.h").write_text(
                "#define SDL_MAJOR_VERSION 3\n#define SDL_MINOR_VERSION 4\n#define SDL_MICRO_VERSION 16\n"
            )
            with self.assertRaisesRegex(verify.VerificationError, "SDL version mismatch"):
                verify.verify_source(sdl, java, app)

    def test_redundant_shared_sdl_load_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            sdl, java, app = self.make_tree(Path(tmp), load_sdl3=True)
            with self.assertRaisesRegex(verify.VerificationError, "do not load a separate libSDL3.so"):
                verify.verify_source(sdl, java, app)

    def test_apk_with_redundant_sdl_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            apk = Path(tmp) / "app.apk"
            with zipfile.ZipFile(apk, "w") as zf:
                for name in ["classes.dex", "lib/arm64-v8a/libmain.so", "lib/arm64-v8a/libpng16.so", "lib/arm64-v8a/libSDL3.so"]:
                    zf.writestr(name, b"x")
            with self.assertRaisesRegex(verify.VerificationError, "redundant shared SDL"):
                verify.verify_apk(apk)

    def test_apk_without_redundant_sdl_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            apk = Path(tmp) / "app.apk"
            with zipfile.ZipFile(apk, "w") as zf:
                for name in ["classes.dex", "lib/arm64-v8a/libmain.so", "lib/arm64-v8a/libpng16.so"]:
                    zf.writestr(name, b"x")
            verify.verify_apk(apk)

    def test_vendored_controller_uses_three_arg_pad_callbacks(self):
        methods = verify.java_native_methods(ROOT / "packaging" / "android" / "src" / "org" / "libsdl" / "app" / "SDLControllerManager.java")
        self.assertEqual(methods["onNativePadDown"], "(III)Z")
        self.assertEqual(methods["onNativePadUp"], "(III)Z")

    def test_descriptor_arrays_and_objects(self):
        self.assertEqual(verify.method_descriptor("void", "int requestCode, String[] filelist, int filter"), "(I[Ljava/lang/String;I)V")
        self.assertEqual(verify.method_descriptor("int", "String library, String function, Object arguments"), "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Object;)I")


if __name__ == "__main__":
    unittest.main()
