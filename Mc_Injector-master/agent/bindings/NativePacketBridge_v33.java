package mcoverlay;

// Embedded into the target JVM.  This class is deliberately a transport-only
// bridge: all rotation state is finalized before serialization reaches it.
public final class NativePacketBridge_v33 {
    public static native Object serialize(Object packet);
}
