package mcoverlay;

public final class NativeHotbarBridge_v50 {
    private NativeHotbarBridge_v50() {}
    public static native boolean filterPress(boolean pressed, Object binding);
    public static native boolean useEvent(boolean entering, Object minecraft);
}
