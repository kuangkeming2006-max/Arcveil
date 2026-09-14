// Isolated JVM fixture.  It models only the three Minecraft entry points that
// the authoritative logical-state pipeline owns; it has no game or network.
public final class LogicalPipelineFixture {
    public float yaw;
    public float observedYaw;
    public float observedStrafe;
    public float observedForward;
    public boolean sprinting;
    public float observedJumpYaw;
    public boolean observedJumpSprinting;
    public float jumpImpulseX;
    public float jumpImpulseZ;
    public int vanillaClicks;
    public int vanillaHeld;
    public Object queuedPacket;
    public int vanillaPackets;
    public int vanillaAttacks;
    public Object lastAttackTarget;
    public int vanillaBlockStarts, vanillaBlockDamage, vanillaBlockResets;
    public void attackEntity(Object player,LogicalPipelineFixture target) {
        vanillaAttacks++;
        lastAttackTarget = target;
    }
    public boolean clickBlock(Object position, Object face) {
        vanillaBlockStarts++;
        return true;
    }
    public boolean onPlayerDamageBlock(Object position, Object face) {
        vanillaBlockDamage++;
        return true;
    }
    public void resetBlockRemoving() { vanillaBlockResets++; }

    public void moveFlying(float strafe, float forward, float friction) {
        observedYaw = yaw;
        observedStrafe = strafe;
        observedForward = forward;
    }

    public boolean isSprinting() { return sprinting; }
    public void setSprinting(boolean value) { sprinting = value; }
    public void jump() {
        observedJumpYaw = yaw;
        observedJumpSprinting = sprinting;
        if (sprinting) {
            float radians = yaw * 0.017453292F;
            jumpImpulseX -= (float)Math.sin(radians) * 0.2F;
            jumpImpulseZ += (float)Math.cos(radians) * 0.2F;
        }
    }

    public void click() {
        vanillaClicks++;
    }

    public void held(boolean down) {
        vanillaHeld += down ? 1 : -1;
    }

    public void queuePacket(Object packet) {
        queuedPacket = packet;
        vanillaPackets++;
    }
}

// Minimal camera fixture for the live FreeLook transformation. Keeping these
// package-private lets one javac invocation emit all three isolated classes.
final class FreeLookEntityFixture {
    public float rotationYaw = 11.0F;
    public float rotationPitch = 22.0F;
    public float prevRotationYaw = 9.0F;
    public float prevRotationPitch = 20.0F;
    public int setAnglesCalls;

    public void setAngles(float yawDelta, float pitchDelta) {
        setAnglesCalls++;
        rotationYaw += yawDelta * 0.15F;
        rotationPitch -= pitchDelta * 0.15F;
    }
}

final class FreeLookRendererFixture {
    public float observedYaw;
    public float observedPitch;
    public float observedPreviousYaw;
    public float observedPreviousPitch;

    public void updateCameraAndRender(FreeLookEntityFixture entity,
                                      float yawDelta, float pitchDelta) {
        entity.setAngles(yawDelta, pitchDelta);
    }

    public void orientCamera(FreeLookEntityFixture entity, float partialTicks) {
        observedYaw = entity.rotationYaw + partialTicks * 0.0F;
        observedPitch = entity.rotationPitch;
        observedPreviousYaw = entity.prevRotationYaw;
        observedPreviousPitch = entity.prevRotationPitch;
    }
}

final class FreeLookTerrainFixture {
    public float observedYaw;
    public float observedPitch;

    public void setupTerrain(FreeLookEntityFixture entity, double cameraX,
                             Object frustum, int frame, boolean spectator) {
        observedYaw = entity.rotationYaw + (float)cameraX * 0.0F;
        observedPitch = entity.rotationPitch + frame * 0.0F +
            (spectator ? 0.0F : 0.0F);
    }
}
