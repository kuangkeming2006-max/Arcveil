package mcoverlay;

// Embedded in the native agent. The Java side contains no targeting logic;
// it only exposes reversible hook boundaries to the authoritative controller.
public final class NativeLogicalBridge_v32 {
    public static native float beginMovement(
        Object entity, float originalStrafe, float originalForward);
    public static native float mappedForward(Object entity);
    public static native void endMovement(Object entity);
    public static native boolean arbitrateSprint(Object entity, boolean requested);
    public static native void beginJump(Object entity);
    public static native void endJump(Object entity);
    public static native boolean consumeClick(Object minecraft);
    public static native boolean consumeHeld(Object minecraft, boolean leftDown);
}
