public final class HotbarBindingFixture {
    public int pressTime;
    public int currentSlot;
    public boolean isPressed() {
        if (pressTime == 0) return false;
        --pressTime;
        return true;
    }
    public void vanillaSelect(int slot) {
        if (isPressed()) currentSlot = slot;
    }
}
