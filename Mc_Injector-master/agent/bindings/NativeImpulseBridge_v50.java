package mcoverlay;
public final class NativeImpulseBridge_v50 {
    private NativeImpulseBridge_v50() {}
    public static native boolean cancelImpulse(Object entity, Object attacker);
    public static native boolean cancelVelocity(Object handler, Object packet);
}
