package mcoverlay;

/** Observation only: never substitutes an action or modifies arguments. */
public final class NativeDiagnosticBridge_v33 {
    private NativeDiagnosticBridge_v33() {}
    public static native void observe(int event, Object argument);
}
