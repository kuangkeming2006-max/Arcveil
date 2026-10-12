#include "GameBindingsCache.internal.h"
#include "src/AgentLog.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mcoverlay {

bool GameBindings::resolveModernProfile(JNIEnv* env, const MappingProfile& p,
                                       jclass minecraft, BindingCache& c)
{
    if (env->PushLocalFrame(64)<0) {clearException(env);return false;}
    const auto finish=[&](bool ok) {clearException(env);env->PopLocalFrame(nullptr);return ok;};
    jobject loader=nullptr;
    ++m_bindingJvmtiCalls;
    if (m_jvmti->GetClassLoader(minecraft,&loader)!=JVMTI_ERROR_NONE || !loader) return finish(false);
    ++m_bindingJniCalls;
    jclass loaderClass=env->FindClass("java/lang/ClassLoader");
    if (env->ExceptionCheck() || !loaderClass) return finish(false);
    ++m_bindingJniCalls;
    const auto load=env->GetMethodID(loaderClass,"loadClass","(Ljava/lang/String;)Ljava/lang/Class;");
    if (env->ExceptionCheck() || !load) return finish(false);
    const auto loadMapped=[&](const std::string& name) -> jclass {
        ++m_bindingJniCalls;
        auto klass=loadWithClassLoader(env,loader,load,name.c_str());
        if (!klass || env->ExceptionCheck()) {log::error("Modern required class unavailable: "+name);return nullptr;}
        jobject defining=nullptr;
        ++m_bindingJvmtiCalls;
        if (m_jvmti->GetClassLoader(klass,&defining)!=JVMTI_ERROR_NONE) return nullptr;
        const bool same=env->IsSameObject(loader,defining);
        if (defining) env->DeleteLocalRef(defining);
        if (!same) log::error("Modern defining loader mismatch: "+name);
        return same ? klass : nullptr;
    };
    auto player=loadMapped(p.playerName), living=loadMapped(p.livingName), entity=loadMapped(p.entityName);
    auto playerBase=loadMapped(p.entityPlayerName);
    auto aabb=loadMapped(p.aabbName), world=loadMapped(p.worldName), level=loadMapped(p.worldClientName);
    auto component=loadMapped(p.chatComponentName), constants=loadMapped(p.sharedConstantsName);
    auto version=loadMapped(p.worldVersionName), mouse=loadMapped(p.mouseHandlerName);
    if (!player || !playerBase || !living || !entity || !aabb || !world || !level || !component || !constants || !version || !mouse)
        return finish(false);
    if (!env->IsAssignableFrom(player,playerBase) || !env->IsAssignableFrom(playerBase,living) ||
        !env->IsAssignableFrom(living,entity) || !env->IsAssignableFrom(level,world)) {
        log::error("Modern mapped class hierarchy does not match the required API.");
        return finish(false);
    }
    bool valid=true;
    const auto method=[&](jclass owner,const std::string& name,const std::string& desc,bool isStatic=false) {
        if (!valid) return jmethodID{};
        ++m_bindingJniCalls;
        auto id=isStatic?env->GetStaticMethodID(owner,name.c_str(),desc.c_str()):env->GetMethodID(owner,name.c_str(),desc.c_str());
        if (env->ExceptionCheck() || !id) {clearException(env); valid=false;log::error("Modern method binding failed: "+name+" "+desc);}
        return id;
    };
    const auto field=[&](jclass owner,const std::string& name,const std::string& desc) {
        if (!valid) return jfieldID{};
        ++m_bindingJniCalls;
        auto id=env->GetFieldID(owner,name.c_str(),desc.c_str());
        if (env->ExceptionCheck() || !id) {clearException(env);valid=false;log::error("Modern field binding failed: "+name+" "+desc);}
        return id;
    };
    const auto currentVersion=method(constants,p.getCurrentVersion,"()"+p.worldVersionSignature,true);
    const auto versionName=method(version,p.getVersionName,"()Ljava/lang/String;");
    if (!valid) return finish(false);
    ++m_bindingJniCalls;
    auto versionObject=env->CallStaticObjectMethod(constants,currentVersion);
    if (env->ExceptionCheck() || !versionObject) return finish(false);
    auto name=static_cast<jstring>(env->CallObjectMethod(versionObject,versionName));
    if (env->ExceptionCheck() || !name) return finish(false);
    const char* text=env->GetStringUTFChars(name,nullptr);
    if (env->ExceptionCheck() || !text) return finish(false);
    const std::string runtimeVersion(text);
    const bool exact=bindings::MinecraftVersion::parse(runtimeVersion)==p.minecraftVersion;
    env->ReleaseStringUTFChars(name,text);
    if (!exact) {log::error("Unsupported runtime Minecraft version: "+runtimeVersion+"; dictionary requires exact 1.20.1.");return finish(false);}
    c.getMinecraft=method(minecraft,p.getMinecraft,"()"+p.minecraftSignature,true);
    c.playerField=field(minecraft,p.playerField,p.playerSignature);
    c.worldField=field(minecraft,p.worldField,p.worldClientSignature);
    c.currentScreen=field(minecraft,p.currentScreenField,p.guiScreenSignature);
    c.isMainThread=method(minecraft,p.isMainThread,"()Z");
    c.isSingleplayer=method(minecraft,p.isSingleplayer,"()Z");
    c.getLoadedEntities=method(level,p.getLoadedEntities,"()Ljava/lang/Iterable;");
    c.getHealth=method(living,p.getHealth,"()F"); c.getMaxHealth=method(living,p.getMaxHealth,"()F");
    c.getEntityId=method(entity,p.getEntityId,"()I"); c.getBounds=method(entity,p.getBounds,"()"+p.aabbSignature);
    c.getName=method(entity,p.getName,"()"+p.chatComponentSignature);
    c.getFormattedText=method(component,p.getFormattedText,"()Ljava/lang/String;");
    c.getEyeHeight=method(entity,p.getEyeHeight,"()F"); c.isInvisible=method(entity,p.isInvisible,"()Z");
    c.isAlive=method(entity,p.isAlive,"()Z"); c.isRemoved=method(entity,p.isRemoved,"()Z");
    for (unsigned i=0;i<3;++i) {
        c.positionMethods[i]=method(entity,p.positionMethods[i],"()D");
        c.previousPosition[i]=field(entity,p.previousPositionFields[i],"D");
    }
    for (unsigned i=0;i<2;++i) c.rotationMethods[i]=method(entity,p.rotationMethods[i],"()F");
    jfieldID* bounds[]={&c.minX,&c.minY,&c.minZ,&c.maxX,&c.maxY,&c.maxZ};
    for (unsigned i=0;i<6;++i) *bounds[i]=field(aabb,p.aabbFields[i],"D");
    c.mouseHandlerField=field(minecraft,p.mouseHandlerField,p.mouseHandlerSignature);
    c.grabMouse=method(mouse,p.grabMouse,"()V"); c.releaseMouse=method(mouse,p.releaseMouse,"()V");
    c.isMouseGrabbed=method(mouse,p.isMouseGrabbed,"()Z");
    auto iterable=env->FindClass("java/lang/Iterable");
    if (env->ExceptionCheck() || !iterable) return finish(false);
    auto iterator=env->FindClass("java/util/Iterator");
    if (env->ExceptionCheck() || !iterator) return finish(false);
    c.iterableIterator=method(iterable,"iterator","()Ljava/util/Iterator;");
    c.iteratorHasNext=method(iterator,"hasNext","()Z"); c.iteratorNext=method(iterator,"next","()Ljava/lang/Object;");
    if (!valid) return finish(false);
    ++m_bindingJniCalls;
    auto instance=env->CallStaticObjectMethod(minecraft,c.getMinecraft);
    if (env->ExceptionCheck() || !instance) return finish(false);
    // Main Agent independently rechecks the exact version and every required JNI ID
    // before the existing immutable-cache publication can authorize BINDING_READY.
    const std::pair<jclass,jclass*> refs[]={{minecraft,&c.minecraftClass},{player,&c.playerClass},
        {living,&c.livingClass},{entity,&c.entityClass},{aabb,&c.aabbClass},{world,&c.worldClass},
        {level,&c.worldClientClass},{component,&c.chatComponentClass},{mouse,&c.mouseHandlerClass},
        {playerBase,&c.entityPlayerClass}};
    for (const auto& [local,destination]:refs) {
        ++m_bindingJniCalls;
        *destination=static_cast<jclass>(env->NewGlobalRef(local));
        if (env->ExceptionCheck() || !*destination) return finish(false);
    }
    c.profile=&p;
    log::info("Modern 1.20.1 API verified: entity snapshots and mouse capture available; inventory, world projection and gameplay hooks unavailable.");
    return finish(true);
}

const GameSnapshot& GameBindings::sampleModern(JNIEnv* env,std::uint64_t now) noexcept
{
    if (!env || (m_lastSample && now-m_lastSample<50)) return m_snapshot;
    m_lastSample=now;
    auto& c=*m_cache;
    if (env->PushLocalFrame(32)<0) {clearException(env);m_snapshot.state=GameSnapshot::State::JniError;return m_snapshot;}
    const auto fail=[&]() -> const GameSnapshot& {
        clearException(env);env->PopLocalFrame(nullptr);
        // Never publish a partial world/entity generation on an exception.
        m_snapshot=GameSnapshot{};m_snapshot.state=GameSnapshot::State::JniError;return m_snapshot;
    };
    auto minecraft=c.gameApi().minecraft(env);
    if (env->ExceptionCheck() || !minecraft) return fail();
    const bool main=env->CallBooleanMethod(minecraft,c.isMainThread);
    if (env->ExceptionCheck()) return fail();
    if (!main) {env->PopLocalFrame(nullptr);m_snapshot.state=GameSnapshot::State::WaitingForGameThread;return m_snapshot;}
    GameSnapshot next{};
    auto player=c.gameApi().player(env,minecraft);
    if (env->ExceptionCheck()) return fail();
    auto world=c.gameApi().world(env,minecraft);
    if (env->ExceptionCheck()) return fail();
    auto screen=env->GetObjectField(minecraft,c.currentScreen);
    if (env->ExceptionCheck()) return fail();
    next.gameScreenOpen=screen!=nullptr;
    if (!player || !world) {
        if (m_lastWorld) {env->DeleteWeakGlobalRef(m_lastWorld);m_lastWorld=nullptr;++m_worldGeneration;}
        next.state=GameSnapshot::State::NoPlayer;next.worldGeneration=m_worldGeneration;
        env->PopLocalFrame(nullptr);m_snapshot=next;return m_snapshot;
    }
    if (!env->IsInstanceOf(player,c.playerClass) || !env->IsInstanceOf(world,c.worldClientClass)) return fail();
    if (!m_lastWorld || !env->IsSameObject(world,m_lastWorld)) {
        if (m_lastWorld) env->DeleteWeakGlobalRef(m_lastWorld);
        m_lastWorld=env->NewWeakGlobalRef(world);++m_worldGeneration;
        if (env->ExceptionCheck() || !m_lastWorld) return fail();
    }
    // Borrowed per-operation GameApi objects; nothing crosses the local frame.
    const auto read=[&](jobject object,EntityMarker& marker) -> bool {
        const auto checked=[&] {return !env->ExceptionCheck();};
        marker.entityId=env->CallIntMethod(object,c.getEntityId); if (!checked()) return false;
        marker.currentX=env->CallDoubleMethod(object,c.positionMethods[0]);if (!checked()) return false;
        marker.currentY=env->CallDoubleMethod(object,c.positionMethods[1]);if (!checked()) return false;
        marker.currentZ=env->CallDoubleMethod(object,c.positionMethods[2]);if (!checked()) return false;
        marker.previousX=env->GetDoubleField(object,c.previousPosition[0]);if (!checked()) return false;
        marker.previousY=env->GetDoubleField(object,c.previousPosition[1]);if (!checked()) return false;
        marker.previousZ=env->GetDoubleField(object,c.previousPosition[2]);if (!checked()) return false;
        marker.yaw=env->CallFloatMethod(object,c.rotationMethods[0]);if (!checked()) return false;
        marker.pitch=env->CallFloatMethod(object,c.rotationMethods[1]);if (!checked()) return false;
        marker.invisible=env->CallBooleanMethod(object,c.isInvisible);if (!checked()) return false;
        auto box=env->CallObjectMethod(object,c.getBounds);if (!checked() || !box) return false;
        double* values[]={&marker.bounds.minX,&marker.bounds.minY,&marker.bounds.minZ,&marker.bounds.maxX,&marker.bounds.maxY,&marker.bounds.maxZ};
        const jfieldID fields[]={c.minX,c.minY,c.minZ,c.maxX,c.maxY,c.maxZ};
        for (unsigned i=0;i<6;++i) {*values[i]=env->GetDoubleField(box,fields[i]);if (!checked() || !std::isfinite(*values[i])) return false;}
        env->DeleteLocalRef(box);
        if (!std::isfinite(marker.currentX)||!std::isfinite(marker.currentY)||!std::isfinite(marker.currentZ)||
            !std::isfinite(marker.previousX)||!std::isfinite(marker.previousY)||!std::isfinite(marker.previousZ)||
            !std::isfinite(marker.yaw)||!std::isfinite(marker.pitch)||
            marker.bounds.minX>marker.bounds.maxX||marker.bounds.minY>marker.bounds.maxY||marker.bounds.minZ>marker.bounds.maxZ) return false;
        marker.player=env->IsInstanceOf(object,c.entityPlayerClass);
        if (env->IsInstanceOf(object,c.livingClass)) {
            marker.health=env->CallFloatMethod(object,c.getHealth);if (!checked()) return false;
            marker.maxHealth=env->CallFloatMethod(object,c.getMaxHealth);if (!checked()) return false;
            if (!std::isfinite(marker.health)||!std::isfinite(marker.maxHealth)) return false;
        }
        auto component=env->CallObjectMethod(object,c.getName);if (!checked() || !component) return false;
        auto name=static_cast<jstring>(env->CallObjectMethod(component,c.getFormattedText));if (!checked() || !name) return false;
        const char* text=env->GetStringUTFChars(name,nullptr);if (!checked() || !text) return false;
        std::snprintf(marker.displayName.data(),marker.displayName.size(),"%s",text);
        std::snprintf(marker.playerName.data(),marker.playerName.size(),"%s",text);
        env->ReleaseStringUTFChars(name,text);env->DeleteLocalRef(name);env->DeleteLocalRef(component);
        return checked();
    };
    EntityMarker local{};
    if (!read(player,local)) return fail();
    next.x=local.currentX;next.y=local.currentY;next.z=local.currentZ;next.bounds=local.bounds;
    next.yaw=local.yaw;next.pitch=local.pitch;next.health=local.health;next.maxHealth=local.maxHealth;
    next.entityId=local.entityId;next.localPlayerName=local.playerName;
    next.integratedSinglePlayer=next.singlePlayer=env->CallBooleanMethod(minecraft,c.isSingleplayer);
    if (env->ExceptionCheck()) return fail();
    auto entities=c.gameApi().entities(env,world);if (env->ExceptionCheck() || !entities) return fail();
    auto iterator=env->CallObjectMethod(entities,c.iterableIterator);if (env->ExceptionCheck() || !iterator) return fail();
    for (unsigned count=0;;++count) {
        const bool more=env->CallBooleanMethod(iterator,c.iteratorHasNext);if (env->ExceptionCheck()) return fail();
        if (!more) break;
        if (count==4096) {next.entityEnumerationTruncated=true;break;}
        auto entity=env->CallObjectMethod(iterator,c.iteratorNext);if (env->ExceptionCheck() || !entity) return fail();
        if (!env->IsInstanceOf(entity,c.entityClass)) return fail();
        ++next.loadedEntities;
        const bool removed=env->CallBooleanMethod(entity,c.isRemoved);if (env->ExceptionCheck()) return fail();
        const bool alive=env->CallBooleanMethod(entity,c.isAlive);if (env->ExceptionCheck()) return fail();
        if (!removed && alive && !env->IsSameObject(player,entity) && next.entityMarkerCount<next.entityMarkers.size()) {
            auto& marker=next.entityMarkers[next.entityMarkerCount];
            if (!read(entity,marker)) return fail();
            marker.distance=std::hypot(marker.currentX-next.x,marker.currentY-next.y,marker.currentZ-next.z);
            ++next.entityMarkerCount;
        }
        env->DeleteLocalRef(entity);
    }
    next.entitySampleGeneration=++m_entitySampleGeneration;next.worldGeneration=m_worldGeneration;
    next.state=GameSnapshot::State::Ready;
    env->PopLocalFrame(nullptr);m_snapshot=next;return m_snapshot;
}

bool GameBindings::setModernInputCaptured(JNIEnv* env,bool captured) noexcept
{
    if (!env || env->PushLocalFrame(8)<0) {clearException(env);return false;}
    const auto finish=[&](bool ok) {clearException(env);env->PopLocalFrame(nullptr);return ok;};
    auto& c=*m_cache;
    auto minecraft=c.gameApi().minecraft(env);
    if (env->ExceptionCheck() || !minecraft) return finish(false);
    const bool main=env->CallBooleanMethod(minecraft,c.isMainThread);
    if (env->ExceptionCheck() || !main) return finish(false);
    auto mouse=env->GetObjectField(minecraft,c.mouseHandlerField);
    if (env->ExceptionCheck() || !mouse) return finish(false);
    if (captured && !m_overlayInputSessionActive) {
        const bool grabbed=env->CallBooleanMethod(mouse,c.isMouseGrabbed);
        if (env->ExceptionCheck()) return finish(false);
        m_inputWasGrabbed=grabbed;m_overlayInputSessionActive=true;
        ::ReleaseCapture();
    }
    if (captured) {env->CallVoidMethod(mouse,c.releaseMouse);::ClipCursor(nullptr);}
    else if (m_overlayInputSessionActive) {
        auto screen=env->GetObjectField(minecraft,c.currentScreen);
        if (env->ExceptionCheck()) return finish(false);
        if (m_inputWasGrabbed && !screen) env->CallVoidMethod(mouse,c.grabMouse);
        m_overlayInputSessionActive=false;m_inputGrabStateKnown=false;
    }
    return finish(!env->ExceptionCheck());
}
} // namespace mcoverlay
