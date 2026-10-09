#include "GameApi.internal.h"
#include <cstdio>

using namespace mcoverlay::bindings;
namespace {
int calls=0, failures=0, checks=0;
template<class T> T ref(unsigned value) { return reinterpret_cast<T>(static_cast<std::uintptr_t>(value)); }
void check(bool ok,const char* why) { ++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);} }
jobject JNICALL staticField(JNIEnv*,jclass,jfieldID) { ++calls;return ref<jobject>(10); }
jobject JNICALL staticMethod(JNIEnv*,jclass,jmethodID,va_list) { ++calls;return ref<jobject>(11); }
jobject JNICALL objectField(JNIEnv*,jobject,jfieldID field) { ++calls;return ref<jobject>(field==ref<jfieldID>(5)?20:21); }
jobject JNICALL objectMethod(JNIEnv*,jobject,jmethodID,va_list) { ++calls;return ref<jobject>(22); }
jsize JNICALL arrayLength(JNIEnv*,jarray) { ++calls;return 36; }
jobject JNICALL arrayElement(JNIEnv*,jobjectArray,jint slot) { ++calls;return ref<jobject>(unsigned(slot)+100); }
}
int main() {
    check(MinecraftVersion::parse("1.8.9")==MinecraftVersion{1,8,9},"exact release version");
    check(MinecraftVersion::parse("1.12.2")==MinecraftVersion{1,12,2},"independent version identity");
    check(MinecraftVersion::parse("1.21")==MinecraftVersion{1,21,0},"two-component release");
    for(auto bad:{"","1","1.8.","1.8.9.0","01.8.9","1.8.9-Forge","Lunar","1.-8.9","1.65536.0"})
        check(!MinecraftVersion::parse(bad).known(),"reject malformed/ambiguous version");
    const auto* adapter=selectVersionAdapter({1,8,9});
    check(adapter&&adapter->capabilities.supports(VersionCapability::InventoryAccess),"verified inventory support ceiling");
    check(adapter&&adapter->capabilities.supports(VersionCapability::BedScanning),"verified section scanner ceiling");
    check(!VersionCapabilities{}.supports(VersionCapability::EntitySnapshots),"unresolved adapter grants no capability");
    for(auto unsupported:{MinecraftVersion{},MinecraftVersion{1,8,8},MinecraftVersion{1,12,2},MinecraftVersion{1,21,0}})
        check(!selectVersionAdapter(unsupported),"nearby/new versions fail closed");
    check(adapter->inventoryDescriptor("Lfixture/Stack;")=="[Lfixture/Stack;","array shape uses mapped type descriptor");
    JNINativeInterface_ table{};
    table.GetStaticObjectField=staticField;table.CallStaticObjectMethodV=staticMethod;
    table.GetObjectField=objectField;table.CallObjectMethodV=objectMethod;
    table.GetArrayLength=arrayLength;table.GetObjectArrayElement=arrayElement;
    JNIEnv env{&table};
    GameApi api{adapter,ref<jclass>(1),ref<jmethodID>(2),nullptr,
        ref<jfieldID>(3),ref<jfieldID>(4),ref<jmethodID>(6),nullptr,ref<jfieldID>(5)};
    check(api.minecraft(&env)==ref<jobject>(11),"mapped singleton method");
    api.singletonField=ref<jfieldID>(7);
    check(api.minecraft(&env)==ref<jobject>(10),"mapped singleton field without version/family branching");
    check(api.player(&env,ref<jobject>(1))==ref<jobject>(21),"player access");
    check(api.world(&env,ref<jobject>(1))==ref<jobject>(21),"world access");
    check(api.entities(&env,ref<jobject>(1))==ref<jobject>(22),"mapped entity list method");
    api.entitiesField=ref<jfieldID>(8);
    check(api.entities(&env,ref<jobject>(1))==ref<jobject>(21),"mapped entity list field");
    const auto inventory=api.inventory(&env,ref<jobject>(1));
    check(bool(inventory)&&inventory.size(&env)==36&&inventory.at(&env,8)==ref<jobject>(108),"uniform slot view");
    check(calls==9,"same JNI call count, no extra lookup/reference operations");
    auto disabled=*adapter;disabled.capabilities={};api.adapter=&disabled;
    check(!api.inventory(&env,ref<jobject>(1)),"unsupported inventory does not access JNI");
    api.adapter=adapter;api.inventoryField=nullptr;
    check(!api.inventory(&env,ref<jobject>(1)),"missing optional mapping does not access JNI");
    check(!api.inventory(&env,nullptr)&&calls==9,"null inventory preserves JNI boundary");
    std::printf("GameApiTests: %d checks, %d failures (mock JNI; no Minecraft)\n",checks,failures);
    return failures?1:0;
}
