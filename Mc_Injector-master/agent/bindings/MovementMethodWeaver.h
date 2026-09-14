#pragma once

#include <jni.h>
#include <span>

namespace mcoverlay {

inline jbyteArray weaveMovementMethod(JNIEnv* env,
                                     std::span<const unsigned char> bytes,
                                     const char* name,
                                     const char* descriptor,
                                     const char* sprintName,
                                     const char* sprintDescriptor) noexcept {
#define MOVE_GET(var, expression) auto var=(expression); if(env->ExceptionCheck() || !var) return nullptr
#define MOVE_DO(expression) expression; if(env->ExceptionCheck()) return nullptr
    MOVE_GET(readerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassReader"));
    MOVE_GET(nodeClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/ClassNode"));
    MOVE_GET(writerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassWriter"));
    MOVE_GET(methodClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodNode"));
    MOVE_GET(listClass,env->FindClass("java/util/List"));
    MOVE_GET(insnsClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnList"));
    MOVE_GET(insnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnNode"));
    MOVE_GET(varInsnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/VarInsnNode"));
    MOVE_GET(callClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodInsnNode"));
    MOVE_GET(abstractClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/AbstractInsnNode"));
    MOVE_GET(readerCtor,env->GetMethodID(readerClass,"<init>","([B)V"));
    MOVE_GET(nodeCtor,env->GetMethodID(nodeClass,"<init>","()V"));
    MOVE_GET(writerCtor,env->GetMethodID(writerClass,"<init>","(I)V"));
    MOVE_GET(acceptReader,env->GetMethodID(readerClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;I)V"));
    MOVE_GET(acceptNode,env->GetMethodID(nodeClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;)V"));
    MOVE_GET(toBytes,env->GetMethodID(writerClass,"toByteArray","()[B"));
    MOVE_GET(methodsField,env->GetFieldID(nodeClass,"methods","Ljava/util/List;"));
    MOVE_GET(nameField,env->GetFieldID(methodClass,"name","Ljava/lang/String;"));
    MOVE_GET(descField,env->GetFieldID(methodClass,"desc","Ljava/lang/String;"));
    MOVE_GET(insnsField,env->GetFieldID(methodClass,"instructions","Ljdk/internal/org/objectweb/asm/tree/InsnList;"));
    MOVE_GET(listSize,env->GetMethodID(listClass,"size","()I"));
    MOVE_GET(listGet,env->GetMethodID(listClass,"get","(I)Ljava/lang/Object;"));
    MOVE_GET(insnsCtor,env->GetMethodID(insnsClass,"<init>","()V"));
    MOVE_GET(insnCtor,env->GetMethodID(insnClass,"<init>","(I)V"));
    MOVE_GET(varInsnCtor,env->GetMethodID(varInsnClass,"<init>","(II)V"));
    MOVE_GET(callCtor,env->GetMethodID(callClass,"<init>","(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V"));
    MOVE_GET(add,env->GetMethodID(insnsClass,"add","(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;)V"));
    MOVE_GET(insert,env->GetMethodID(insnsClass,"insert","(Ljdk/internal/org/objectweb/asm/tree/InsnList;)V"));
    MOVE_GET(before,env->GetMethodID(insnsClass,"insertBefore","(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;Ljdk/internal/org/objectweb/asm/tree/InsnList;)V"));
    MOVE_GET(toArray,env->GetMethodID(insnsClass,"toArray","()[Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;"));
    MOVE_GET(opcode,env->GetMethodID(abstractClass,"getOpcode","()I"));
    MOVE_GET(stringClass,env->FindClass("java/lang/String"));
    MOVE_GET(equals,env->GetMethodID(stringClass,"equals","(Ljava/lang/Object;)Z"));
    MOVE_GET(wantedName,env->NewStringUTF(name));
    MOVE_GET(wantedDesc,env->NewStringUTF(descriptor));
    MOVE_GET(wantedSprintName,env->NewStringUTF(sprintName));
    MOVE_GET(wantedSprintDesc,env->NewStringUTF(sprintDescriptor));
    MOVE_GET(bridgeName,env->NewStringUTF("mcoverlay/NativeLogicalBridge_v32"));
    MOVE_GET(beginName,env->NewStringUTF("beginMovement"));
    MOVE_GET(beginDesc,env->NewStringUTF("(Ljava/lang/Object;FF)F"));
    MOVE_GET(forwardName,env->NewStringUTF("mappedForward"));
    MOVE_GET(forwardDesc,env->NewStringUTF("(Ljava/lang/Object;)F"));
    MOVE_GET(endName,env->NewStringUTF("endMovement"));
    MOVE_GET(endDesc,env->NewStringUTF("(Ljava/lang/Object;)V"));
    MOVE_GET(sprintBridgeName,env->NewStringUTF("arbitrateSprint"));
    MOVE_GET(sprintBridgeDesc,env->NewStringUTF("(Ljava/lang/Object;Z)Z"));
    MOVE_GET(input,env->NewByteArray(static_cast<jsize>(bytes.size())));
    MOVE_DO(env->SetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),
        reinterpret_cast<const jbyte*>(bytes.data())));
    MOVE_GET(reader,env->NewObject(readerClass,readerCtor,input));
    MOVE_GET(node,env->NewObject(nodeClass,nodeCtor));
    MOVE_DO(env->CallVoidMethod(reader,acceptReader,node,0));
    MOVE_GET(methods,env->GetObjectField(node,methodsField));
    const jint count=env->CallIntMethod(methods,listSize);
    if(env->ExceptionCheck()||count<0||count>8192) return nullptr;
    const auto appendVar=[&](jobject list,int op,int index) noexcept -> bool {
        jobject value=env->NewObject(varInsnClass,varInsnCtor,op,index);
        if(!value||env->ExceptionCheck()) return false;
        env->CallVoidMethod(list,add,value);
        if(env->ExceptionCheck()) return false;
        env->DeleteLocalRef(value);
        return true;
    };
    const auto appendCall=[&](jobject list,jstring method,jstring signature) noexcept -> bool {
        jobject value=env->NewObject(callClass,callCtor,184,bridgeName,method,signature,JNI_FALSE);
        if(!value||env->ExceptionCheck()) return false;
        env->CallVoidMethod(list,add,value);
        if(env->ExceptionCheck()) return false;
        env->DeleteLocalRef(value);
        return true;
    };
    bool movementFound=false;
    bool sprintFound=false;
    for(jint index=0;index<count;++index) {
        MOVE_GET(method,env->CallObjectMethod(methods,listGet,index));
        MOVE_GET(methodName,env->GetObjectField(method,nameField));
        MOVE_GET(methodDesc,env->GetObjectField(method,descField));
        const bool sameName=env->CallBooleanMethod(methodName,equals,wantedName)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        const bool sameDesc=env->CallBooleanMethod(methodDesc,equals,wantedDesc)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        const bool sameSprintName=
            env->CallBooleanMethod(methodName,equals,wantedSprintName)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        const bool sameSprintDesc=
            env->CallBooleanMethod(methodDesc,equals,wantedSprintDesc)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        if(sameName&&sameDesc&&!movementFound) {
            MOVE_GET(instructions,env->GetObjectField(method,insnsField));
            MOVE_GET(nodes,static_cast<jobjectArray>(env->CallObjectMethod(instructions,toArray)));
            const jsize size=env->GetArrayLength(nodes);
            for(jsize i=0;i<size;++i) {
                MOVE_GET(instruction,env->GetObjectArrayElement(nodes,i));
                const int op=env->CallIntMethod(instruction,opcode);
                if(env->ExceptionCheck()) return nullptr;
                if(op==177||op==191) {
                    MOVE_GET(exit,env->NewObject(insnsClass,insnsCtor));
                    if(!appendVar(exit,25,0)||!appendCall(exit,endName,endDesc)) return nullptr;
                    MOVE_DO(env->CallVoidMethod(instructions,before,instruction,exit));
                    env->DeleteLocalRef(exit);
                }
                env->DeleteLocalRef(instruction);
            }
            MOVE_GET(entry,env->NewObject(insnsClass,insnsCtor));
            if(!appendVar(entry,25,0)||!appendVar(entry,23,1)||!appendVar(entry,23,2)||
               !appendCall(entry,beginName,beginDesc)||!appendVar(entry,56,1)||
               !appendVar(entry,25,0)||!appendCall(entry,forwardName,forwardDesc)||
               !appendVar(entry,56,2)) return nullptr;
            MOVE_DO(env->CallVoidMethod(instructions,insert,entry));
            movementFound=true;
        }
        if(sameSprintName&&sameSprintDesc&&!sprintFound) {
            MOVE_GET(instructions,env->GetObjectField(method,insnsField));
            MOVE_GET(entry,env->NewObject(insnsClass,insnsCtor));
            // Treat every caller's `true` as a sprint request.  The bridge may
            // veto it while SilentCombat owns the held input, but it never
            // manufactures a true state and becomes a no-op immediately on
            // release.
            if(!appendVar(entry,25,0)||!appendVar(entry,21,1)||
               !appendCall(entry,sprintBridgeName,sprintBridgeDesc)||
               !appendVar(entry,54,1)) return nullptr;
            MOVE_DO(env->CallVoidMethod(instructions,insert,entry));
            sprintFound=true;
        }
        if(movementFound&&sprintFound) break;
    }
    if(!movementFound||!sprintFound) return nullptr;
    MOVE_GET(writer,env->NewObject(writerClass,writerCtor,1));
    MOVE_DO(env->CallVoidMethod(node,acceptNode,writer));
    MOVE_GET(output,static_cast<jbyteArray>(env->CallObjectMethod(writer,toBytes)));
    return output;
#undef MOVE_GET
#undef MOVE_DO
}

} // namespace mcoverlay
