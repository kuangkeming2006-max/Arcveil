public final class AssistFeaturesFixture {
    public int selected=0;
    public int[] counts={1,64};
    public int entrySlot=-1;
    public int cleanupSlot=-1;
    public int events=0;
    public double velocity=0.375;
    public void rightClickMouse() {
        if(counts[selected]<=0) return;
        --counts[selected];
        if(counts[selected]==0) {
            cleanupSlot=selected;
            counts[selected]=0;
        }
    }
    public void observeUse(boolean enter) {
        ++events;
        if(enter) entrySlot=selected;
        else if(selected==entrySlot&&counts[selected]==0&&counts[1]>0) selected=1;
    }
    public void knockBack(Object attacker,float strength,double x,double z) {
        velocity=velocity*0.5+strength+x+z;
    }
    public void handleVelocity(Object packet) { velocity=99.0; }
}
