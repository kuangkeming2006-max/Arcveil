#pragma once

#include "VersionAdapter.h"
#include <jni.h>

namespace mcoverlay::bindings {

// Borrowed frame-local view. No global refs, JNI lookups, exception clearing or
// local-frame management here; the calling GameBindings operation owns those.
class InventoryView final {
public:
    InventoryView() = default;
    explicit InventoryView(jobject contents) noexcept : m_contents(contents) {}
    explicit operator bool() const noexcept { return m_contents != nullptr; }
    [[nodiscard]] jsize size(JNIEnv* env) const noexcept {
        return m_contents ? env->GetArrayLength(static_cast<jobjectArray>(m_contents)) : 0;
    }
    [[nodiscard]] jobject at(JNIEnv* env, jint slot) const noexcept {
        return m_contents ? env->GetObjectArrayElement(static_cast<jobjectArray>(m_contents), slot) : nullptr;
    }
private:
    jobject m_contents = nullptr; // Legacy18 representation stays inside this boundary.
};

struct GameApi final {
    const VersionAdapter* adapter;
    jclass minecraftClass;
    jmethodID singletonMethod;
    jfieldID singletonField, playerField, worldField;
    jmethodID entitiesMethod;
    jfieldID entitiesField, inventoryField;

    [[nodiscard]] jobject minecraft(JNIEnv* env) const noexcept {
        return singletonField ? env->GetStaticObjectField(minecraftClass, singletonField)
                              : env->CallStaticObjectMethod(minecraftClass, singletonMethod);
    }
    [[nodiscard]] jobject player(JNIEnv* env, jobject minecraft) const noexcept {
        return env->GetObjectField(minecraft, playerField);
    }
    [[nodiscard]] jobject world(JNIEnv* env, jobject minecraft) const noexcept {
        return env->GetObjectField(minecraft, worldField);
    }
    [[nodiscard]] jobject entities(JNIEnv* env, jobject world) const noexcept {
        return entitiesField ? env->GetObjectField(world, entitiesField)
                             : env->CallObjectMethod(world, entitiesMethod);
    }
    [[nodiscard]] InventoryView inventory(JNIEnv* env, jobject inventory) const noexcept {
        if (!inventory || !inventoryField || !adapter || adapter->shape != GameApiShape::Legacy18 ||
            !adapter->capabilities.supports(VersionCapability::InventoryAccess)) return {};
        return InventoryView(env->GetObjectField(inventory, inventoryField));
    }
};

} // namespace mcoverlay::bindings
