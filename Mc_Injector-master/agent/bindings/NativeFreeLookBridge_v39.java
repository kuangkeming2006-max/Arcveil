package mcoverlay;

// Embedded in the native agent. Camera policy remains native; this class only
// provides stack-compatible call sites for the two transformed render methods.
public final class NativeFreeLookBridge_v39 {
    public static native void rotateCamera(
        Object entity, float yawDelta, float pitchDelta);
    public static native float cameraYaw(Object entity);
    public static native float cameraPitch(Object entity);
    public static native float previousCameraYaw(Object entity);
    public static native float previousCameraPitch(Object entity);
}
