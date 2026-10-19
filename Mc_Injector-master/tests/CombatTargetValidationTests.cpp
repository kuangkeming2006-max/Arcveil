#include "../agent/bindings/CombatTargetValidation.h"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
template<class T> T token(std::uintptr_t value) {
    return reinterpret_cast<T>(value);
}
const jobject world=token<jobject>(1),local=token<jobject>(2),target=token<jobject>(3),
    targetAlias=token<jobject>(4),other=token<jobject>(5),list=token<jobject>(6);
const jclass playerClass=token<jclass>(7);
const jfieldID playersField=token<jfieldID>(8);
const jmethodID sizeMethod=token<jmethodID>(9),getMethod=token<jmethodID>(10),
    healthMethod=token<jmethodID>(11);

struct FakeJvm final {
    enum class ThrowAt {None,Self,Class,Field,Size,Element,Membership};
    static inline FakeJvm* active=nullptr;
    JNINativeInterface_ table{};
    JNIEnv env{&table};
    bool targetIsPlayer=true,listPresent=true,pending=false,healthThrows=false;
    ThrowAt throwAt=ThrowAt::None;
    float health=20.F;
    int healthReads=0,classReads=0,listReads=0,sizeReads=0,elementReads=0,
        comparisons=0,localDeletes=0,borrowedDeletes=0,exceptionClears=0;
    std::vector<jobject> players{other,targetAlias};

    FakeJvm() {
        active=this;
        table.ExceptionCheck=[](JNIEnv*) -> jboolean {
            return active->pending?JNI_TRUE:JNI_FALSE;
        };
        table.ExceptionClear=[](JNIEnv*) {
            ++active->exceptionClears;active->pending=false;
        };
        table.IsSameObject=[](JNIEnv*,jobject a,jobject b) -> jboolean {
            auto& s=*active;++s.comparisons;
            if((b==local&&s.throwAt==ThrowAt::Self)||
               (b!=local&&s.throwAt==ThrowAt::Membership)) s.pending=true;
            const bool alias=(a==target||a==targetAlias)&&(b==target||b==targetAlias);
            return a==b||alias?JNI_TRUE:JNI_FALSE;
        };
        table.IsInstanceOf=[](JNIEnv*,jobject object,jclass type) -> jboolean {
            auto& s=*active;++s.classReads;
            if(s.throwAt==ThrowAt::Class) s.pending=true;
            return object==target&&type==playerClass&&s.targetIsPlayer?JNI_TRUE:JNI_FALSE;
        };
        table.GetObjectField=[](JNIEnv*,jobject object,jfieldID field) -> jobject {
            auto& s=*active;++s.listReads;
            if(s.throwAt==ThrowAt::Field) s.pending=true;
            return object==world&&field==playersField&&s.listPresent?list:nullptr;
        };
        table.CallIntMethodV=[](JNIEnv*,jobject object,jmethodID method,va_list) -> jint {
            auto& s=*active;++s.sizeReads;
            if(s.throwAt==ThrowAt::Size) s.pending=true;
            return object==list&&method==sizeMethod?static_cast<jint>(s.players.size()):-1;
        };
        table.CallObjectMethodV=[](JNIEnv*,jobject object,jmethodID method,va_list args) -> jobject {
            auto& s=*active;++s.elementReads;
            if(s.throwAt==ThrowAt::Element) s.pending=true;
            const jint index=va_arg(args,jint);
            if(object!=list||method!=getMethod||index<0||
               static_cast<std::size_t>(index)>=s.players.size()) return nullptr;
            return s.players[static_cast<std::size_t>(index)];
        };
        table.CallFloatMethodV=[](JNIEnv*,jobject,jmethodID,va_list) -> jfloat {
            auto& s=*active;++s.healthReads;
            if(s.healthThrows) s.pending=true;
            return s.health;
        };
        table.DeleteLocalRef=[](JNIEnv*,jobject object) {
            auto& s=*active;++s.localDeletes;
            if(object==world||object==local||object==target) ++s.borrowedDeletes;
        };
    }
    mcoverlay::aim::ForcePlayerValidation validate() {
        return mcoverlay::aim::validateForcePlayerEntity(&env,world,local,target,
            playerClass,playersField,sizeMethod,getMethod);
    }
};
}

int main() {
    using namespace mcoverlay::aim;
    int checks=0,failures=0;
    const auto check=[&](bool value,const char* reason) {
        ++checks;if(!value) {++failures;std::printf("FAIL: %s\n",reason);}
    };
    const std::array<float,8> healthValues{{std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
        0.F,-1.F,1.F,20.F,std::numeric_limits<float>::max()}};
    for(const bool classMember:{false,true}) for(const bool healthThrows:{false,true}) {
        FakeJvm jvm;
        jvm.targetIsPlayer=classMember;jvm.healthThrows=healthThrows;
        for(int repeat=0;repeat<12;++repeat) for(float health:healthValues) {
            jvm.health=health;
            // Exercise the production Force identity validator repeatedly at
            // candidate, publication and execution boundaries. Geometry and
            // actual GameBindings call-site wiring are separate regressions.
            for(int boundary=0;boundary<3;++boundary) {
                check(jvm.validate()==ForcePlayerValidation::Valid,
                    "Force live player validation survives health updates / reacquisition");
                check(jvm.healthReads==0,"Force never calls the JNI health getter");
                check(!jvm.pending,"throwing getHealth cannot poison Force JNI state");
            }
        }
        check(jvm.borrowedDeletes==0,"Force never deletes caller-owned references");
        check(jvm.exceptionClears==0,"Force validator leaves exception ownership with caller");
        check(classMember?jvm.listReads==0:jvm.listReads>0,
            "class classification is sufficient; exact world membership is fallback");
        (void)jvm.env.CallFloatMethod(target,healthMethod);
        check(jvm.healthReads==1,"JNI health spy is connected to actual CallFloatMethod wrapper");
        check(jvm.pending==healthThrows,"JNI health spy can model throwing health getter");
    }
    {
        FakeJvm jvm;
        jvm.targetIsPlayer=false;jvm.players={nullptr,other,targetAlias};
        check(jvm.validate()==ForcePlayerValidation::Valid,
            "exact object alias membership accepts a player wrapper without cached class membership");
        check(jvm.localDeletes==3,"world list and all nonnull element references released");
        check(jvm.borrowedDeletes==0,"membership does not delete target or world reference");
    }
    {
        FakeJvm jvm;
        jvm.targetIsPlayer=false;jvm.players={other};
        check(jvm.validate()==ForcePlayerValidation::NotPlayer,
            "unrelated living entity absent from actual world player list stays rejected");
        check(jvm.localDeletes==2,"nonplayer rejection releases list and element references");
    }
    {
        FakeJvm jvm;
        jvm.targetIsPlayer=false;jvm.players.assign(4097,nullptr);
        jvm.players.back()=targetAlias;
        check(jvm.validate()==ForcePlayerValidation::NotPlayer,
            "fallback scan is bounded against pathological player lists");
        check(jvm.elementReads==4096,"fallback never scans more than loaded-entity safety cap");
        jvm.players[4095]=targetAlias;jvm.elementReads=0;
        check(jvm.validate()==ForcePlayerValidation::Valid,"last bounded world-player member is accepted");
        check(jvm.elementReads==4096,"last bounded member lookup stops at match");
        check(jvm.localDeletes==3,"bounded scans release list and matched local refs");
    }
    {
        FakeJvm jvm;
        check(validateForcePlayerEntity(&jvm.env,world,local,local,playerClass,
            playersField,sizeMethod,getMethod)==ForcePlayerValidation::Self,"self rejected");
        check(jvm.classReads==0&&jvm.healthReads==0,"self rejection happens before player / health access");
        check(validateForcePlayerEntity(nullptr,world,local,target,playerClass,
            playersField,sizeMethod,getMethod)==ForcePlayerValidation::ContextUnavailable,"missing env rejected");
        check(validateForcePlayerEntity(&jvm.env,nullptr,local,target,playerClass,
            playersField,sizeMethod,getMethod)==ForcePlayerValidation::ContextUnavailable,"missing current world rejected");
        check(validateForcePlayerEntity(&jvm.env,world,nullptr,target,playerClass,
            playersField,sizeMethod,getMethod)==ForcePlayerValidation::ContextUnavailable,"missing local player rejected");
        check(validateForcePlayerEntity(&jvm.env,world,local,nullptr,playerClass,
            playersField,sizeMethod,getMethod)==ForcePlayerValidation::ContextUnavailable,"missing live target rejected");
    }
    for(const auto site:{FakeJvm::ThrowAt::Self,FakeJvm::ThrowAt::Class,
            FakeJvm::ThrowAt::Field,FakeJvm::ThrowAt::Size,
            FakeJvm::ThrowAt::Element,FakeJvm::ThrowAt::Membership}) {
        FakeJvm jvm;
        jvm.targetIsPlayer=false;jvm.throwAt=site;
        check(jvm.validate()==ForcePlayerValidation::JniException,
            "identity JNI failure rejects Force instead of granting eligibility");
        check(jvm.pending&&jvm.exceptionClears==0,"identity exception stays owned by call site");
        check(jvm.healthReads==0,"identity failure never falls back to health");
        check(jvm.borrowedDeletes==0,"exception cleanup preserves borrowed references");
        const int expectedDeletes=site==FakeJvm::ThrowAt::Self||site==FakeJvm::ThrowAt::Class?0:
            site==FakeJvm::ThrowAt::Field||site==FakeJvm::ThrowAt::Size?1:2;
        check(jvm.localDeletes==expectedDeletes,"all acquired local references released on identity exception");
    }
    {
        FakeJvm jvm;jvm.pending=true;
        check(jvm.validate()==ForcePlayerValidation::JniException,"preexisting JNI failure stays rejected");
        check(jvm.comparisons==0&&jvm.classReads==0&&jvm.healthReads==0,
            "no entity access while an unrelated JNI exception is pending");
    }
    {
        FakeJvm jvm;jvm.targetIsPlayer=false;jvm.listPresent=false;
        check(jvm.validate()==ForcePlayerValidation::PlayerListUnavailable,"missing actual list rejects unclassified target");
        check(validateForcePlayerEntity(&jvm.env,world,local,target,nullptr,playersField,
            sizeMethod,getMethod)==ForcePlayerValidation::PlayerListUnavailable,
            "missing class and actual list cannot grant player identity");
        jvm.listPresent=true;
        check(validateForcePlayerEntity(&jvm.env,world,local,target,nullptr,playersField,
            sizeMethod,getMethod)==ForcePlayerValidation::Valid,
            "world membership can classify when optional player class is unavailable");
        check(validateForcePlayerEntity(&jvm.env,world,local,target,playerClass,nullptr,
            sizeMethod,getMethod)==ForcePlayerValidation::PlayerListUnavailable,"missing player list binding rejected");
        check(validateForcePlayerEntity(&jvm.env,world,local,target,playerClass,playersField,
            nullptr,getMethod)==ForcePlayerValidation::PlayerListUnavailable,"missing size method rejected");
        check(validateForcePlayerEntity(&jvm.env,world,local,target,playerClass,playersField,
            sizeMethod,nullptr)==ForcePlayerValidation::PlayerListUnavailable,"missing get method rejected");
    }
    for(const auto result:{ForcePlayerValidation::Valid,ForcePlayerValidation::ContextUnavailable,
            ForcePlayerValidation::Self,ForcePlayerValidation::NotPlayer,
            ForcePlayerValidation::PlayerListUnavailable,ForcePlayerValidation::JniException})
        check(forcePlayerValidationReason(result)[0]!='\0',"every live identity result has a diagnostic reason");
    std::printf("Combat target JNI validation: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
