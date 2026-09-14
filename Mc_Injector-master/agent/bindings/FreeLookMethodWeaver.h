#pragma once

#include <jni.h>
#include <span>
#include <cstdio>
#include <cstring>

namespace mcoverlay {

struct FreeLookWeaveReport final {
    bool updateFound=false;
    bool orientFound=false;
    int rotateCount=0;
    int fieldCounts[4]{};
    char stage[48]{"entry"};
    char observed[384]{};
    char callSites[768]{};
};

// Camera-render hook: redirect only orientation reads. Mouse ownership is
// installed independently at Entity.setAngles itself, because Lunar commonly
// removes the direct updateCameraAndRender -> setAngles call-site.
inline jbyteArray weaveFreeLookMethods(
    JNIEnv* env, std::span<const unsigned char> bytes,
    const char* updateName, const char* updateDescriptor,
    const char* orientName, const char* orientDescriptor,
    const char* entityOwner, const char* setAnglesName,
    const char* yawField, const char* pitchField,
    const char* previousYawField, const char* previousPitchField,
    FreeLookWeaveReport* report=nullptr) noexcept {
    if(report)*report={};
    const auto stage=[&](const char* value) noexcept {
        if(report)std::snprintf(report->stage,sizeof(report->stage),"%s",value);
    };
    stage("resolve_asm");
#define FREELOOK_GET(var, expression) auto var=(expression); if(env->ExceptionCheck() || !var) return nullptr
#define FREELOOK_DO(expression) expression; if(env->ExceptionCheck()) return nullptr
    FREELOOK_GET(readerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassReader"));
    FREELOOK_GET(nodeClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/ClassNode"));
    FREELOOK_GET(writerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassWriter"));
    FREELOOK_GET(methodClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodNode"));
    FREELOOK_GET(listClass,env->FindClass("java/util/List"));
    FREELOOK_GET(insnsClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnList"));
    FREELOOK_GET(callClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodInsnNode"));
    FREELOOK_GET(fieldClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/FieldInsnNode"));
    FREELOOK_GET(abstractClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/AbstractInsnNode"));
    FREELOOK_GET(readerCtor,env->GetMethodID(readerClass,"<init>","([B)V"));
    FREELOOK_GET(nodeCtor,env->GetMethodID(nodeClass,"<init>","()V"));
    FREELOOK_GET(writerCtor,env->GetMethodID(writerClass,"<init>","(I)V"));
    FREELOOK_GET(callCtor,env->GetMethodID(callClass,"<init>",
        "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V"));
    FREELOOK_GET(acceptReader,env->GetMethodID(readerClass,"accept",
        "(Ljdk/internal/org/objectweb/asm/ClassVisitor;I)V"));
    FREELOOK_GET(acceptNode,env->GetMethodID(nodeClass,"accept",
        "(Ljdk/internal/org/objectweb/asm/ClassVisitor;)V"));
    FREELOOK_GET(toBytes,env->GetMethodID(writerClass,"toByteArray","()[B"));
    FREELOOK_GET(methodsField,env->GetFieldID(nodeClass,"methods","Ljava/util/List;"));
    FREELOOK_GET(methodNameField,env->GetFieldID(methodClass,"name","Ljava/lang/String;"));
    FREELOOK_GET(methodDescField,env->GetFieldID(methodClass,"desc","Ljava/lang/String;"));
    FREELOOK_GET(insnsField,env->GetFieldID(methodClass,"instructions",
        "Ljdk/internal/org/objectweb/asm/tree/InsnList;"));
    FREELOOK_GET(callNameField,env->GetFieldID(callClass,"name","Ljava/lang/String;"));
    FREELOOK_GET(callDescField,env->GetFieldID(callClass,"desc","Ljava/lang/String;"));
    FREELOOK_GET(callOwnerField,env->GetFieldID(callClass,"owner","Ljava/lang/String;"));
    FREELOOK_GET(fieldOwnerField,env->GetFieldID(fieldClass,"owner","Ljava/lang/String;"));
    FREELOOK_GET(fieldNameField,env->GetFieldID(fieldClass,"name","Ljava/lang/String;"));
    FREELOOK_GET(fieldDescField,env->GetFieldID(fieldClass,"desc","Ljava/lang/String;"));
    FREELOOK_GET(listSize,env->GetMethodID(listClass,"size","()I"));
    FREELOOK_GET(listGet,env->GetMethodID(listClass,"get","(I)Ljava/lang/Object;"));
    FREELOOK_GET(toArray,env->GetMethodID(insnsClass,"toArray",
        "()[Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;"));
    FREELOOK_GET(setInsn,env->GetMethodID(insnsClass,"set",
        "(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;"
        "Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;)V"));
    FREELOOK_GET(opcode,env->GetMethodID(abstractClass,"getOpcode","()I"));
    FREELOOK_GET(stringClass,env->FindClass("java/lang/String"));
    FREELOOK_GET(equals,env->GetMethodID(stringClass,"equals","(Ljava/lang/Object;)Z"));

    FREELOOK_GET(wantedUpdateName,env->NewStringUTF(updateName));
    FREELOOK_GET(wantedUpdateDesc,env->NewStringUTF(updateDescriptor));
    FREELOOK_GET(wantedOrientName,env->NewStringUTF(orientName));
    FREELOOK_GET(wantedOrientDesc,env->NewStringUTF(orientDescriptor));
    FREELOOK_GET(wantedOwner,env->NewStringUTF(entityOwner));
    FREELOOK_GET(wantedSetAngles,env->NewStringUTF(setAnglesName));
    FREELOOK_GET(setAnglesDesc,env->NewStringUTF("(FF)V"));
    FREELOOK_GET(floatDesc,env->NewStringUTF("F"));
    FREELOOK_GET(wantedYaw,env->NewStringUTF(yawField));
    FREELOOK_GET(wantedPitch,env->NewStringUTF(pitchField));
    FREELOOK_GET(wantedPreviousYaw,env->NewStringUTF(previousYawField));
    FREELOOK_GET(wantedPreviousPitch,env->NewStringUTF(previousPitchField));
    FREELOOK_GET(bridgeName,env->NewStringUTF("mcoverlay/NativeFreeLookBridge_v39"));
    FREELOOK_GET(getterDesc,env->NewStringUTF("(Ljava/lang/Object;)F"));
    FREELOOK_GET(yawGetter,env->NewStringUTF("cameraYaw"));
    FREELOOK_GET(pitchGetter,env->NewStringUTF("cameraPitch"));
    FREELOOK_GET(previousYawGetter,env->NewStringUTF("previousCameraYaw"));
    FREELOOK_GET(previousPitchGetter,env->NewStringUTF("previousCameraPitch"));

    stage("parse_class");
    FREELOOK_GET(input,env->NewByteArray(static_cast<jsize>(bytes.size())));
    FREELOOK_DO(env->SetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),
        reinterpret_cast<const jbyte*>(bytes.data())));
    FREELOOK_GET(reader,env->NewObject(readerClass,readerCtor,input));
    FREELOOK_GET(node,env->NewObject(nodeClass,nodeCtor));
    FREELOOK_DO(env->CallVoidMethod(reader,acceptReader,node,0));
    FREELOOK_GET(methods,env->GetObjectField(node,methodsField));
    const jint count=env->CallIntMethod(methods,listSize);
    if(env->ExceptionCheck()||count<0||count>8192) return nullptr;

    const auto same=[&](jobject left,jobject right) noexcept {
        const bool result=env->CallBooleanMethod(left,equals,right)==JNI_TRUE;
        return !env->ExceptionCheck()&&result;
    };
    const auto makeCall=[&](jstring name,jstring descriptor) noexcept -> jobject {
        return env->NewObject(callClass,callCtor,184,bridgeName,name,descriptor,JNI_FALSE);
    };
    bool updateFound=false;
    bool orientFound=false;
    int rotateCount=0;
    int fieldCounts[4]{};
    stage("scan_methods");
    for(jint methodIndex=0;methodIndex<count;++methodIndex) {
        FREELOOK_GET(method,env->CallObjectMethod(methods,listGet,methodIndex));
        FREELOOK_GET(name,env->GetObjectField(method,methodNameField));
        FREELOOK_GET(desc,env->GetObjectField(method,methodDescField));
        const bool update=same(name,wantedUpdateName)&&same(desc,wantedUpdateDesc);
        const bool orient=same(name,wantedOrientName)&&same(desc,wantedOrientDesc);
        if(report){report->updateFound|=update;report->orientFound|=orient;}
        if(!update&&!orient) continue;
        FREELOOK_GET(instructions,env->GetObjectField(method,insnsField));
        FREELOOK_GET(nodes,static_cast<jobjectArray>(
            env->CallObjectMethod(instructions,toArray)));
        const jsize size=env->GetArrayLength(nodes);
        for(jsize index=0;index<size;++index) {
            FREELOOK_GET(instruction,env->GetObjectArrayElement(nodes,index));
            const int op=env->CallIntMethod(instruction,opcode);
            if(env->ExceptionCheck()) return nullptr;
            if(update&&(op==182||op==183||op==184||op==185)&&
               env->IsInstanceOf(instruction,callClass)) {
                FREELOOK_GET(callOwner,env->GetObjectField(instruction,callOwnerField));
                FREELOOK_GET(callName,env->GetObjectField(instruction,callNameField));
                FREELOOK_GET(callDesc,env->GetObjectField(instruction,callDescField));
                if(report) {
                    const char* ownerText=env->GetStringUTFChars(
                        static_cast<jstring>(callOwner),nullptr);
                    const char* nameText=env->GetStringUTFChars(
                        static_cast<jstring>(callName),nullptr);
                    const char* descText=env->GetStringUTFChars(
                        static_cast<jstring>(callDesc),nullptr);
                    if(ownerText&&nameText&&descText) {
                        const std::size_t used=std::strlen(report->callSites);
                        if(used<sizeof(report->callSites)-1U)
                            std::snprintf(report->callSites+used,
                                sizeof(report->callSites)-used,"%s%d:%s.%s%s",
                                used?" | ":"",op,ownerText,nameText,descText);
                    }
                    if(ownerText)env->ReleaseStringUTFChars(
                        static_cast<jstring>(callOwner),ownerText);
                    if(nameText)env->ReleaseStringUTFChars(
                        static_cast<jstring>(callName),nameText);
                    if(descText)env->ReleaseStringUTFChars(
                        static_cast<jstring>(callDesc),descText);
                    if(env->ExceptionCheck())env->ExceptionClear();
                }
                // The invokevirtual owner is normally EntityPlayerSP even
                // though setAngles and the four fields are declared by Entity.
                // Matching inside the exact mapped renderer method by name and
                // descriptor is therefore the stable cross-client boundary.
                if(same(callName,wantedSetAngles)&&same(callDesc,setAnglesDesc)) {
                    ++rotateCount;
                    if(report)report->rotateCount=rotateCount;
                }
            } else if(orient&&op==180&&env->IsInstanceOf(instruction,fieldClass)) {
                FREELOOK_GET(owner,env->GetObjectField(instruction,fieldOwnerField));
                FREELOOK_GET(fieldName,env->GetObjectField(instruction,fieldNameField));
                FREELOOK_GET(fieldDesc,env->GetObjectField(instruction,fieldDescField));
                jstring getter=nullptr;
                int fieldIndex=-1;
                if(same(fieldName,wantedYaw)) { getter=yawGetter; fieldIndex=0; }
                else if(same(fieldName,wantedPitch)) { getter=pitchGetter; fieldIndex=1; }
                else if(same(fieldName,wantedPreviousYaw)) {
                    getter=previousYawGetter; fieldIndex=2;
                } else if(same(fieldName,wantedPreviousPitch)) {
                    getter=previousPitchGetter; fieldIndex=3;
                }
                if(getter&&(!same(owner,wantedOwner)||!same(fieldDesc,floatDesc))) {
                    if(report&&report->observed[0]=='\0') {
                        const char* ownerText=env->GetStringUTFChars(
                            static_cast<jstring>(owner),nullptr);
                        const char* nameText=env->GetStringUTFChars(
                            static_cast<jstring>(fieldName),nullptr);
                        const char* descText=env->GetStringUTFChars(
                            static_cast<jstring>(fieldDesc),nullptr);
                        if(ownerText&&nameText&&descText)
                            std::snprintf(report->observed,sizeof(report->observed),
                                "mapped field seen with owner=%s name=%s desc=%s expectedOwner=%s",
                                ownerText,nameText,descText,entityOwner);
                        if(ownerText)env->ReleaseStringUTFChars(
                            static_cast<jstring>(owner),ownerText);
                        if(nameText)env->ReleaseStringUTFChars(
                            static_cast<jstring>(fieldName),nameText);
                        if(descText)env->ReleaseStringUTFChars(
                            static_cast<jstring>(fieldDesc),descText);
                        if(env->ExceptionCheck())env->ExceptionClear();
                    }
                    continue;
                }
                if(getter) {
                    FREELOOK_GET(replacement,makeCall(getter,getterDesc));
                    FREELOOK_DO(env->CallVoidMethod(instructions,setInsn,instruction,replacement));
                    ++fieldCounts[fieldIndex];
                    if(report)report->fieldCounts[fieldIndex]=fieldCounts[fieldIndex];
                }
            }
        }
        updateFound|=update;
        orientFound|=orient;
    }
    stage("validate_counts");
    if(!updateFound||!orientFound||fieldCounts[0]<1||
       fieldCounts[1]<1||fieldCounts[2]<1||fieldCounts[3]<1) return nullptr;
    stage("write_class");
    FREELOOK_GET(writer,env->NewObject(writerClass,writerCtor,1));
    FREELOOK_DO(env->CallVoidMethod(node,acceptNode,writer));
    FREELOOK_GET(output,static_cast<jbyteArray>(env->CallObjectMethod(writer,toBytes)));
    stage("success");
    return output;
#undef FREELOOK_GET
#undef FREELOOK_DO
}

struct FreeLookTerrainWeaveReport final {
    bool methodFound=false;
    int yawReads=0;
    int pitchReads=0;
    char stage[48]{"entry"};
    char observed[384]{};
};

// Terrain visibility must use the same visual camera angles as orientCamera.
// This keeps the player's logical/network rotation untouched while allowing
// vanilla's chunk culler to prepare the area behind the player during FreeLook.
inline jbyteArray weaveFreeLookTerrainMethod(
    JNIEnv* env,std::span<const unsigned char> bytes,
    const char* methodName,const char* methodDescriptor,
    const char* entityOwner,const char* yawField,const char* pitchField,
    FreeLookTerrainWeaveReport* report=nullptr) noexcept {
    if(report)*report={};
    const auto stage=[&](const char* value) noexcept {
        if(report)std::snprintf(report->stage,sizeof(report->stage),"%s",value);
    };
#define FREELOOK_TERRAIN_GET(var, expression) auto var=(expression); if(env->ExceptionCheck() || !var) return nullptr
#define FREELOOK_TERRAIN_DO(expression) expression; if(env->ExceptionCheck()) return nullptr
    stage("resolve_asm");
    FREELOOK_TERRAIN_GET(readerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassReader"));
    FREELOOK_TERRAIN_GET(nodeClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/ClassNode"));
    FREELOOK_TERRAIN_GET(writerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassWriter"));
    FREELOOK_TERRAIN_GET(methodClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodNode"));
    FREELOOK_TERRAIN_GET(listClass,env->FindClass("java/util/List"));
    FREELOOK_TERRAIN_GET(insnsClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnList"));
    FREELOOK_TERRAIN_GET(callClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodInsnNode"));
    FREELOOK_TERRAIN_GET(fieldClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/FieldInsnNode"));
    FREELOOK_TERRAIN_GET(abstractClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/AbstractInsnNode"));
    FREELOOK_TERRAIN_GET(readerCtor,env->GetMethodID(readerClass,"<init>","([B)V"));
    FREELOOK_TERRAIN_GET(nodeCtor,env->GetMethodID(nodeClass,"<init>","()V"));
    FREELOOK_TERRAIN_GET(writerCtor,env->GetMethodID(writerClass,"<init>","(I)V"));
    FREELOOK_TERRAIN_GET(callCtor,env->GetMethodID(callClass,"<init>",
        "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V"));
    FREELOOK_TERRAIN_GET(acceptReader,env->GetMethodID(readerClass,"accept",
        "(Ljdk/internal/org/objectweb/asm/ClassVisitor;I)V"));
    FREELOOK_TERRAIN_GET(acceptNode,env->GetMethodID(nodeClass,"accept",
        "(Ljdk/internal/org/objectweb/asm/ClassVisitor;)V"));
    FREELOOK_TERRAIN_GET(toBytes,env->GetMethodID(writerClass,"toByteArray","()[B"));
    FREELOOK_TERRAIN_GET(methodsField,env->GetFieldID(nodeClass,"methods","Ljava/util/List;"));
    FREELOOK_TERRAIN_GET(methodNameField,env->GetFieldID(methodClass,"name","Ljava/lang/String;"));
    FREELOOK_TERRAIN_GET(methodDescField,env->GetFieldID(methodClass,"desc","Ljava/lang/String;"));
    FREELOOK_TERRAIN_GET(insnsField,env->GetFieldID(methodClass,"instructions",
        "Ljdk/internal/org/objectweb/asm/tree/InsnList;"));
    FREELOOK_TERRAIN_GET(fieldOwnerField,env->GetFieldID(fieldClass,"owner","Ljava/lang/String;"));
    FREELOOK_TERRAIN_GET(fieldNameField,env->GetFieldID(fieldClass,"name","Ljava/lang/String;"));
    FREELOOK_TERRAIN_GET(fieldDescField,env->GetFieldID(fieldClass,"desc","Ljava/lang/String;"));
    FREELOOK_TERRAIN_GET(listSize,env->GetMethodID(listClass,"size","()I"));
    FREELOOK_TERRAIN_GET(listGet,env->GetMethodID(listClass,"get","(I)Ljava/lang/Object;"));
    FREELOOK_TERRAIN_GET(toArray,env->GetMethodID(insnsClass,"toArray",
        "()[Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;"));
    FREELOOK_TERRAIN_GET(setInsn,env->GetMethodID(insnsClass,"set",
        "(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;"
        "Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;)V"));
    FREELOOK_TERRAIN_GET(opcode,env->GetMethodID(abstractClass,"getOpcode","()I"));
    FREELOOK_TERRAIN_GET(stringClass,env->FindClass("java/lang/String"));
    FREELOOK_TERRAIN_GET(equals,env->GetMethodID(stringClass,"equals","(Ljava/lang/Object;)Z"));
    FREELOOK_TERRAIN_GET(wantedMethod,env->NewStringUTF(methodName));
    FREELOOK_TERRAIN_GET(wantedMethodDesc,env->NewStringUTF(methodDescriptor));
    FREELOOK_TERRAIN_GET(wantedOwner,env->NewStringUTF(entityOwner));
    FREELOOK_TERRAIN_GET(wantedYaw,env->NewStringUTF(yawField));
    FREELOOK_TERRAIN_GET(wantedPitch,env->NewStringUTF(pitchField));
    FREELOOK_TERRAIN_GET(floatDesc,env->NewStringUTF("F"));
    FREELOOK_TERRAIN_GET(bridgeName,env->NewStringUTF("mcoverlay/NativeFreeLookBridge_v39"));
    FREELOOK_TERRAIN_GET(getterDesc,env->NewStringUTF("(Ljava/lang/Object;)F"));
    FREELOOK_TERRAIN_GET(yawGetter,env->NewStringUTF("cameraYaw"));
    FREELOOK_TERRAIN_GET(pitchGetter,env->NewStringUTF("cameraPitch"));

    stage("parse_class");
    FREELOOK_TERRAIN_GET(input,env->NewByteArray(static_cast<jsize>(bytes.size())));
    FREELOOK_TERRAIN_DO(env->SetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),
        reinterpret_cast<const jbyte*>(bytes.data())));
    FREELOOK_TERRAIN_GET(reader,env->NewObject(readerClass,readerCtor,input));
    FREELOOK_TERRAIN_GET(node,env->NewObject(nodeClass,nodeCtor));
    FREELOOK_TERRAIN_DO(env->CallVoidMethod(reader,acceptReader,node,0));
    FREELOOK_TERRAIN_GET(methods,env->GetObjectField(node,methodsField));
    const jint count=env->CallIntMethod(methods,listSize);
    if(env->ExceptionCheck()||count<0||count>8192)return nullptr;
    const auto same=[&](jobject left,jobject right) noexcept {
        const bool result=env->CallBooleanMethod(left,equals,right)==JNI_TRUE;
        return !env->ExceptionCheck()&&result;
    };
    const auto makeCall=[&](jstring name) noexcept -> jobject {
        return env->NewObject(callClass,callCtor,184,bridgeName,name,getterDesc,JNI_FALSE);
    };
    int yawReads=0;
    int pitchReads=0;
    bool methodFound=false;
    stage("scan_method");
    for(jint methodIndex=0;methodIndex<count;++methodIndex) {
        FREELOOK_TERRAIN_GET(method,env->CallObjectMethod(methods,listGet,methodIndex));
        FREELOOK_TERRAIN_GET(name,env->GetObjectField(method,methodNameField));
        FREELOOK_TERRAIN_GET(desc,env->GetObjectField(method,methodDescField));
        if(!same(name,wantedMethod)||!same(desc,wantedMethodDesc))continue;
        methodFound=true;
        if(report)report->methodFound=true;
        FREELOOK_TERRAIN_GET(instructions,env->GetObjectField(method,insnsField));
        FREELOOK_TERRAIN_GET(nodes,static_cast<jobjectArray>(
            env->CallObjectMethod(instructions,toArray)));
        const jsize size=env->GetArrayLength(nodes);
        for(jsize index=0;index<size;++index) {
            FREELOOK_TERRAIN_GET(instruction,env->GetObjectArrayElement(nodes,index));
            const int op=env->CallIntMethod(instruction,opcode);
            if(env->ExceptionCheck())return nullptr;
            if(op!=180||!env->IsInstanceOf(instruction,fieldClass))continue;
            FREELOOK_TERRAIN_GET(owner,env->GetObjectField(instruction,fieldOwnerField));
            FREELOOK_TERRAIN_GET(fieldName,env->GetObjectField(instruction,fieldNameField));
            FREELOOK_TERRAIN_GET(fieldDesc,env->GetObjectField(instruction,fieldDescField));
            const bool yaw=same(fieldName,wantedYaw);
            const bool pitch=same(fieldName,wantedPitch);
            if(!yaw&&!pitch)continue;
            if(!same(owner,wantedOwner)||!same(fieldDesc,floatDesc)) {
                if(report&&report->observed[0]=='\0') {
                    const char* ownerText=env->GetStringUTFChars(
                        static_cast<jstring>(owner),nullptr);
                    const char* nameText=env->GetStringUTFChars(
                        static_cast<jstring>(fieldName),nullptr);
                    const char* descText=env->GetStringUTFChars(
                        static_cast<jstring>(fieldDesc),nullptr);
                    if(ownerText&&nameText&&descText)
                        std::snprintf(report->observed,sizeof(report->observed),
                            "owner=%s name=%s desc=%s expectedOwner=%s",
                            ownerText,nameText,descText,entityOwner);
                    if(ownerText)env->ReleaseStringUTFChars(
                        static_cast<jstring>(owner),ownerText);
                    if(nameText)env->ReleaseStringUTFChars(
                        static_cast<jstring>(fieldName),nameText);
                    if(descText)env->ReleaseStringUTFChars(
                        static_cast<jstring>(fieldDesc),descText);
                    if(env->ExceptionCheck())env->ExceptionClear();
                }
                continue;
            }
            FREELOOK_TERRAIN_GET(replacement,makeCall(yaw?yawGetter:pitchGetter));
            FREELOOK_TERRAIN_DO(env->CallVoidMethod(instructions,setInsn,
                instruction,replacement));
            if(yaw)++yawReads;else ++pitchReads;
            if(report){report->yawReads=yawReads;report->pitchReads=pitchReads;}
        }
    }
    stage("validate");
    if(!methodFound||yawReads<1||pitchReads<1)return nullptr;
    stage("write_class");
    FREELOOK_TERRAIN_GET(writer,env->NewObject(writerClass,writerCtor,1));
    FREELOOK_TERRAIN_DO(env->CallVoidMethod(node,acceptNode,writer));
    FREELOOK_TERRAIN_GET(output,static_cast<jbyteArray>(
        env->CallObjectMethod(writer,toBytes)));
    stage("success");
    return output;
#undef FREELOOK_TERRAIN_GET
#undef FREELOOK_TERRAIN_DO
}

struct FreeLookInputWeaveReport final {
    bool methodFound=false;
    int replacedMethods=0;
    char stage[48]{"entry"};
};

// Mouse/rotation-input hook: replace Entity.setAngles itself with the same
// stack-compatible native bridge call. The native side implements the exact
// vanilla field update while inactive and consumes the deltas while FreeLook
// owns the local camera. Replacing the method body adds no branch and avoids
// relying on Lunar's renderer call graph.
inline jbyteArray weaveFreeLookInputMethod(
    JNIEnv* env,std::span<const unsigned char> bytes,
    const char* setAnglesName,
    FreeLookInputWeaveReport* report=nullptr) noexcept {
    if(report)*report={};
    const auto stage=[&](const char* value) noexcept {
        if(report)std::snprintf(report->stage,sizeof(report->stage),"%s",value);
    };
#define FREELOOK_INPUT_GET(var, expression) auto var=(expression); if(env->ExceptionCheck() || !var) return nullptr
#define FREELOOK_INPUT_DO(expression) expression; if(env->ExceptionCheck()) return nullptr
    stage("resolve_asm");
    FREELOOK_INPUT_GET(readerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassReader"));
    FREELOOK_INPUT_GET(nodeClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/ClassNode"));
    FREELOOK_INPUT_GET(writerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassWriter"));
    FREELOOK_INPUT_GET(methodClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodNode"));
    FREELOOK_INPUT_GET(listClass,env->FindClass("java/util/List"));
    FREELOOK_INPUT_GET(insnsClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnList"));
    FREELOOK_INPUT_GET(varInsnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/VarInsnNode"));
    FREELOOK_INPUT_GET(callClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodInsnNode"));
    FREELOOK_INPUT_GET(insnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnNode"));
    FREELOOK_INPUT_GET(readerCtor,env->GetMethodID(readerClass,"<init>","([B)V"));
    FREELOOK_INPUT_GET(nodeCtor,env->GetMethodID(nodeClass,"<init>","()V"));
    FREELOOK_INPUT_GET(writerCtor,env->GetMethodID(writerClass,"<init>","(I)V"));
    FREELOOK_INPUT_GET(varInsnCtor,env->GetMethodID(varInsnClass,"<init>","(II)V"));
    FREELOOK_INPUT_GET(callCtor,env->GetMethodID(callClass,"<init>",
        "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V"));
    FREELOOK_INPUT_GET(insnCtor,env->GetMethodID(insnClass,"<init>","(I)V"));
    FREELOOK_INPUT_GET(acceptReader,env->GetMethodID(readerClass,"accept",
        "(Ljdk/internal/org/objectweb/asm/ClassVisitor;I)V"));
    FREELOOK_INPUT_GET(acceptNode,env->GetMethodID(nodeClass,"accept",
        "(Ljdk/internal/org/objectweb/asm/ClassVisitor;)V"));
    FREELOOK_INPUT_GET(toBytes,env->GetMethodID(writerClass,"toByteArray","()[B"));
    FREELOOK_INPUT_GET(methodsField,env->GetFieldID(nodeClass,"methods","Ljava/util/List;"));
    FREELOOK_INPUT_GET(methodNameField,env->GetFieldID(methodClass,"name","Ljava/lang/String;"));
    FREELOOK_INPUT_GET(methodDescField,env->GetFieldID(methodClass,"desc","Ljava/lang/String;"));
    FREELOOK_INPUT_GET(insnsField,env->GetFieldID(methodClass,"instructions",
        "Ljdk/internal/org/objectweb/asm/tree/InsnList;"));
    FREELOOK_INPUT_GET(tryCatchField,env->GetFieldID(methodClass,"tryCatchBlocks",
        "Ljava/util/List;"));
    FREELOOK_INPUT_GET(listSize,env->GetMethodID(listClass,"size","()I"));
    FREELOOK_INPUT_GET(listGet,env->GetMethodID(listClass,"get","(I)Ljava/lang/Object;"));
    FREELOOK_INPUT_GET(listClear,env->GetMethodID(listClass,"clear","()V"));
    FREELOOK_INPUT_GET(insnsClear,env->GetMethodID(insnsClass,"clear","()V"));
    FREELOOK_INPUT_GET(insnsAdd,env->GetMethodID(insnsClass,"add",
        "(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;)V"));
    FREELOOK_INPUT_GET(stringClass,env->FindClass("java/lang/String"));
    FREELOOK_INPUT_GET(equals,env->GetMethodID(stringClass,"equals","(Ljava/lang/Object;)Z"));
    FREELOOK_INPUT_GET(wantedName,env->NewStringUTF(setAnglesName));
    FREELOOK_INPUT_GET(wantedDesc,env->NewStringUTF("(FF)V"));
    FREELOOK_INPUT_GET(bridgeName,env->NewStringUTF("mcoverlay/NativeFreeLookBridge_v39"));
    FREELOOK_INPUT_GET(rotateName,env->NewStringUTF("rotateCamera"));
    FREELOOK_INPUT_GET(rotateDesc,env->NewStringUTF("(Ljava/lang/Object;FF)V"));

    stage("parse_class");
    FREELOOK_INPUT_GET(input,env->NewByteArray(static_cast<jsize>(bytes.size())));
    FREELOOK_INPUT_DO(env->SetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),
        reinterpret_cast<const jbyte*>(bytes.data())));
    FREELOOK_INPUT_GET(reader,env->NewObject(readerClass,readerCtor,input));
    FREELOOK_INPUT_GET(node,env->NewObject(nodeClass,nodeCtor));
    // Skip debug labels: the replacement body deliberately has no relationship
    // to the original local-variable table.
    FREELOOK_INPUT_DO(env->CallVoidMethod(reader,acceptReader,node,2));
    FREELOOK_INPUT_GET(methods,env->GetObjectField(node,methodsField));
    const jint count=env->CallIntMethod(methods,listSize);
    if(env->ExceptionCheck()||count<0||count>8192) return nullptr;
    const auto same=[&](jobject left,jobject right) noexcept {
        const bool result=env->CallBooleanMethod(left,equals,right)==JNI_TRUE;
        return !env->ExceptionCheck()&&result;
    };
    stage("replace_method");
    int replaced=0;
    for(jint methodIndex=0;methodIndex<count;++methodIndex) {
        FREELOOK_INPUT_GET(method,env->CallObjectMethod(methods,listGet,methodIndex));
        FREELOOK_INPUT_GET(name,env->GetObjectField(method,methodNameField));
        FREELOOK_INPUT_GET(desc,env->GetObjectField(method,methodDescField));
        if(!same(name,wantedName)||!same(desc,wantedDesc)) continue;
        if(report)report->methodFound=true;
        FREELOOK_INPUT_GET(instructions,env->GetObjectField(method,insnsField));
        FREELOOK_INPUT_GET(tryCatches,env->GetObjectField(method,tryCatchField));
        FREELOOK_INPUT_DO(env->CallVoidMethod(instructions,insnsClear));
        FREELOOK_INPUT_DO(env->CallVoidMethod(tryCatches,listClear));
        FREELOOK_INPUT_GET(loadThis,env->NewObject(varInsnClass,varInsnCtor,25,0));
        FREELOOK_INPUT_GET(loadYaw,env->NewObject(varInsnClass,varInsnCtor,23,1));
        FREELOOK_INPUT_GET(loadPitch,env->NewObject(varInsnClass,varInsnCtor,23,2));
        FREELOOK_INPUT_GET(call,env->NewObject(callClass,callCtor,184,bridgeName,
            rotateName,rotateDesc,JNI_FALSE));
        FREELOOK_INPUT_GET(ret,env->NewObject(insnClass,insnCtor,177));
        FREELOOK_INPUT_DO(env->CallVoidMethod(instructions,insnsAdd,loadThis));
        FREELOOK_INPUT_DO(env->CallVoidMethod(instructions,insnsAdd,loadYaw));
        FREELOOK_INPUT_DO(env->CallVoidMethod(instructions,insnsAdd,loadPitch));
        FREELOOK_INPUT_DO(env->CallVoidMethod(instructions,insnsAdd,call));
        FREELOOK_INPUT_DO(env->CallVoidMethod(instructions,insnsAdd,ret));
        ++replaced;
        if(report)report->replacedMethods=replaced;
    }
    stage("validate");
    if(replaced!=1) return nullptr;
    stage("write_class");
    FREELOOK_INPUT_GET(writer,env->NewObject(writerClass,writerCtor,1));
    FREELOOK_INPUT_DO(env->CallVoidMethod(node,acceptNode,writer));
    FREELOOK_INPUT_GET(output,static_cast<jbyteArray>(env->CallObjectMethod(writer,toBytes)));
    stage("success");
    return output;
#undef FREELOOK_INPUT_GET
#undef FREELOOK_INPUT_DO
}

} // namespace mcoverlay
