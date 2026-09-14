package mcoverlay;

// Embedded in the native agent. The returned object replaces the vanilla
// PlayerControllerMP attack target; null cancels the one original dispatch.
public final class NativeAttackBridge_v40 {
    public static native Object arbitrateAttack(Object originalTarget);
}
