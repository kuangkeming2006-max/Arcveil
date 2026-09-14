#pragma once

#include <jni.h>
#include <span>

namespace mcoverlay {

// Adds reversible native boundaries around EntityLivingBase::jump().  The
// original method body remains the sole source of vertical motion and sprint
// impulse; native code only lends it the committed logical yaw/sprint state.
inline jbyteArray weaveJumpMethod(JNIEnv* env,
                                 std::span<const unsigned char> bytes,
                                 const char* name,
                                 const char* descriptor) noexcept {
#define JUMP_GET(var, expression) auto var=(expression); if(env->ExceptionCheck() || !var) return nullptr
#define JUMP_DO(expression) expression; if(env->ExceptionCheck()) return nullptr
    JUMP_GET(readerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassReader"));
    JUMP_GET(nodeClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/ClassNode"));
    JUMP_GET(writerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassWriter"));
    JUMP_GET(methodClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodNode"));
    JUMP_GET(listClass,env->FindClass("java/util/List"));
    JUMP_GET(insnsClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnList"));
    JUMP_GET(varInsnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/VarInsnNode"));
    JUMP_GET(callClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodInsnNode"));
    JUMP_GET(abstractClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/AbstractInsnNode"));
    JUMP_GET(readerCtor,env->GetMethodID(readerClass,"<init>","([B)V"));
    JUMP_GET(nodeCtor,env->GetMethodID(nodeClass,"<init>","()V"));
    JUMP_GET(writerCtor,env->GetMethodID(writerClass,"<init>","(I)V"));
    JUMP_GET(acceptReader,env->GetMethodID(readerClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;I)V"));
    JUMP_GET(acceptNode,env->GetMethodID(nodeClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;)V"));
    JUMP_GET(toBytes,env->GetMethodID(writerClass,"toByteArray","()[B"));
    JUMP_GET(methodsField,env->GetFieldID(nodeClass,"methods","Ljava/util/List;"));
    JUMP_GET(nameField,env->GetFieldID(methodClass,"name","Ljava/lang/String;"));
    JUMP_GET(descField,env->GetFieldID(methodClass,"desc","Ljava/lang/String;"));
    JUMP_GET(insnsField,env->GetFieldID(methodClass,"instructions","Ljdk/internal/org/objectweb/asm/tree/InsnList;"));
    JUMP_GET(listSize,env->GetMethodID(listClass,"size","()I"));
    JUMP_GET(listGet,env->GetMethodID(listClass,"get","(I)Ljava/lang/Object;"));
    JUMP_GET(insnsCtor,env->GetMethodID(insnsClass,"<init>","()V"));
    JUMP_GET(varInsnCtor,env->GetMethodID(varInsnClass,"<init>","(II)V"));
    JUMP_GET(callCtor,env->GetMethodID(callClass,"<init>","(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V"));
    JUMP_GET(add,env->GetMethodID(insnsClass,"add","(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;)V"));
    JUMP_GET(insert,env->GetMethodID(insnsClass,"insert","(Ljdk/internal/org/objectweb/asm/tree/InsnList;)V"));
    JUMP_GET(before,env->GetMethodID(insnsClass,"insertBefore","(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;Ljdk/internal/org/objectweb/asm/tree/InsnList;)V"));
    JUMP_GET(toArray,env->GetMethodID(insnsClass,"toArray","()[Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;"));
    JUMP_GET(opcode,env->GetMethodID(abstractClass,"getOpcode","()I"));
    JUMP_GET(stringClass,env->FindClass("java/lang/String"));
    JUMP_GET(equals,env->GetMethodID(stringClass,"equals","(Ljava/lang/Object;)Z"));
    JUMP_GET(wantedName,env->NewStringUTF(name));
    JUMP_GET(wantedDesc,env->NewStringUTF(descriptor));
    JUMP_GET(bridgeName,env->NewStringUTF("mcoverlay/NativeLogicalBridge_v32"));
    JUMP_GET(beginName,env->NewStringUTF("beginJump"));
    JUMP_GET(endName,env->NewStringUTF("endJump"));
    JUMP_GET(boundaryDesc,env->NewStringUTF("(Ljava/lang/Object;)V"));
    JUMP_GET(input,env->NewByteArray(static_cast<jsize>(bytes.size())));
    JUMP_DO(env->SetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),
        reinterpret_cast<const jbyte*>(bytes.data())));
    JUMP_GET(reader,env->NewObject(readerClass,readerCtor,input));
    JUMP_GET(node,env->NewObject(nodeClass,nodeCtor));
    JUMP_DO(env->CallVoidMethod(reader,acceptReader,node,0));
    JUMP_GET(methods,env->GetObjectField(node,methodsField));
    const jint count=env->CallIntMethod(methods,listSize);
    if(env->ExceptionCheck()||count<0||count>8192) return nullptr;
    const auto appendBoundary=[&](jobject list,jstring method) noexcept -> bool {
        jobject load=env->NewObject(varInsnClass,varInsnCtor,25,0);
        if(!load||env->ExceptionCheck()) return false;
        env->CallVoidMethod(list,add,load);
        env->DeleteLocalRef(load);
        if(env->ExceptionCheck()) return false;
        jobject call=env->NewObject(callClass,callCtor,184,bridgeName,method,
                                    boundaryDesc,JNI_FALSE);
        if(!call||env->ExceptionCheck()) return false;
        env->CallVoidMethod(list,add,call);
        env->DeleteLocalRef(call);
        return env->ExceptionCheck()==JNI_FALSE;
    };
    bool found=false;
    for(jint index=0;index<count;++index) {
        JUMP_GET(method,env->CallObjectMethod(methods,listGet,index));
        JUMP_GET(methodName,env->GetObjectField(method,nameField));
        JUMP_GET(methodDesc,env->GetObjectField(method,descField));
        const bool sameName=env->CallBooleanMethod(methodName,equals,wantedName)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        const bool sameDesc=env->CallBooleanMethod(methodDesc,equals,wantedDesc)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        if(!sameName||!sameDesc) continue;
        JUMP_GET(instructions,env->GetObjectField(method,insnsField));
        JUMP_GET(nodes,static_cast<jobjectArray>(env->CallObjectMethod(instructions,toArray)));
        const jsize size=env->GetArrayLength(nodes);
        for(jsize i=0;i<size;++i) {
            JUMP_GET(instruction,env->GetObjectArrayElement(nodes,i));
            const int op=env->CallIntMethod(instruction,opcode);
            if(env->ExceptionCheck()) return nullptr;
            if(op==177||op==191) {
                JUMP_GET(exit,env->NewObject(insnsClass,insnsCtor));
                if(!appendBoundary(exit,endName)) return nullptr;
                JUMP_DO(env->CallVoidMethod(instructions,before,instruction,exit));
                env->DeleteLocalRef(exit);
            }
            env->DeleteLocalRef(instruction);
        }
        JUMP_GET(entry,env->NewObject(insnsClass,insnsCtor));
        if(!appendBoundary(entry,beginName)) return nullptr;
        JUMP_DO(env->CallVoidMethod(instructions,insert,entry));
        found=true;
        break;
    }
    if(!found) return nullptr;
    JUMP_GET(writer,env->NewObject(writerClass,writerCtor,1));
    JUMP_DO(env->CallVoidMethod(node,acceptNode,writer));
    JUMP_GET(output,static_cast<jbyteArray>(env->CallObjectMethod(writer,toBytes)));
    return output;
#undef JUMP_GET
#undef JUMP_DO
}

} // namespace mcoverlay
