#pragma once

#include <jni.h>
#include <span>

namespace mcoverlay {

// Veto confirmed-source knockback before vanilla changes any velocity.
inline jbyteArray weaveImpulseMethod(JNIEnv* env,
    std::span<const unsigned char> bytes,const char* methodName,
    const char* methodDescriptor,const char* targetInternalName,const bool velocity=false) noexcept {
#define ATTACK_GET(var, expression) auto var=(expression); if(env->ExceptionCheck() || !var) return nullptr
#define ATTACK_DO(expression) expression; if(env->ExceptionCheck()) return nullptr
    ATTACK_GET(readerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassReader"));
    ATTACK_GET(nodeClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/ClassNode"));
    ATTACK_GET(writerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassWriter"));
    ATTACK_GET(methodClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodNode"));
    ATTACK_GET(listClass,env->FindClass("java/util/List"));
    ATTACK_GET(insnsClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnList"));
    ATTACK_GET(insnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnNode"));
    ATTACK_GET(varInsnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/VarInsnNode"));
    ATTACK_GET(typeInsnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/TypeInsnNode"));
    ATTACK_GET(callClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodInsnNode"));
    ATTACK_GET(labelClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/LabelNode"));
    ATTACK_GET(frameClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/FrameNode"));
    ATTACK_GET(jumpClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/JumpInsnNode"));
    ATTACK_GET(objectClass,env->FindClass("java/lang/Object"));
    ATTACK_GET(readerCtor,env->GetMethodID(readerClass,"<init>","([B)V"));
    ATTACK_GET(nodeCtor,env->GetMethodID(nodeClass,"<init>","()V"));
    ATTACK_GET(writerCtor,env->GetMethodID(writerClass,"<init>","(I)V"));
    ATTACK_GET(acceptReader,env->GetMethodID(readerClass,"accept",
        "(Ljdk/internal/org/objectweb/asm/ClassVisitor;I)V"));
    ATTACK_GET(acceptNode,env->GetMethodID(nodeClass,"accept",
        "(Ljdk/internal/org/objectweb/asm/ClassVisitor;)V"));
    ATTACK_GET(toBytes,env->GetMethodID(writerClass,"toByteArray","()[B"));
    ATTACK_GET(methodsField,env->GetFieldID(nodeClass,"methods","Ljava/util/List;"));
    ATTACK_GET(nameField,env->GetFieldID(methodClass,"name","Ljava/lang/String;"));
    ATTACK_GET(descField,env->GetFieldID(methodClass,"desc","Ljava/lang/String;"));
    ATTACK_GET(insnsField,env->GetFieldID(methodClass,"instructions",
        "Ljdk/internal/org/objectweb/asm/tree/InsnList;"));
    ATTACK_GET(listSize,env->GetMethodID(listClass,"size","()I"));
    ATTACK_GET(listGet,env->GetMethodID(listClass,"get","(I)Ljava/lang/Object;"));
    ATTACK_GET(insnsCtor,env->GetMethodID(insnsClass,"<init>","()V"));
    ATTACK_GET(insnCtor,env->GetMethodID(insnClass,"<init>","(I)V"));
    ATTACK_GET(varInsnCtor,env->GetMethodID(varInsnClass,"<init>","(II)V"));
    ATTACK_GET(typeInsnCtor,env->GetMethodID(typeInsnClass,"<init>",
        "(ILjava/lang/String;)V"));
    ATTACK_GET(callCtor,env->GetMethodID(callClass,"<init>",
        "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V"));
    ATTACK_GET(labelCtor,env->GetMethodID(labelClass,"<init>","()V"));
    ATTACK_GET(frameCtor,env->GetMethodID(frameClass,"<init>",
        "(II[Ljava/lang/Object;I[Ljava/lang/Object;)V"));
    ATTACK_GET(jumpCtor,env->GetMethodID(jumpClass,"<init>",
        "(ILjdk/internal/org/objectweb/asm/tree/LabelNode;)V"));
    ATTACK_GET(add,env->GetMethodID(insnsClass,"add",
        "(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;)V"));
    ATTACK_GET(insert,env->GetMethodID(insnsClass,"insert",
        "(Ljdk/internal/org/objectweb/asm/tree/InsnList;)V"));
    ATTACK_GET(stringClass,env->FindClass("java/lang/String"));
    ATTACK_GET(equals,env->GetMethodID(stringClass,"equals","(Ljava/lang/Object;)Z"));

    ATTACK_GET(wantedName,env->NewStringUTF(methodName));
    ATTACK_GET(wantedDescriptor,env->NewStringUTF(methodDescriptor));
    ATTACK_GET(targetType,env->NewStringUTF(targetInternalName));
    ATTACK_GET(bridgeName,env->NewStringUTF("mcoverlay/NativeImpulseBridge_v50"));
    ATTACK_GET(bridgeMethod,env->NewStringUTF(velocity?"cancelVelocity":"cancelImpulse"));
    ATTACK_GET(bridgeDescriptor,env->NewStringUTF(
        "(Ljava/lang/Object;Ljava/lang/Object;)Z"));
    ATTACK_GET(input,env->NewByteArray(static_cast<jsize>(bytes.size())));
    ATTACK_DO(env->SetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),
        reinterpret_cast<const jbyte*>(bytes.data())));
    ATTACK_GET(reader,env->NewObject(readerClass,readerCtor,input));
    ATTACK_GET(node,env->NewObject(nodeClass,nodeCtor));
    ATTACK_DO(env->CallVoidMethod(reader,acceptReader,node,0));
    ATTACK_GET(methods,env->GetObjectField(node,methodsField));
    const jint count=env->CallIntMethod(methods,listSize);
    if(env->ExceptionCheck()||count<0||count>8192) return nullptr;
    int transformed=0;
    for(jint index=0;index<count;++index) {
        ATTACK_GET(method,env->CallObjectMethod(methods,listGet,index));
        ATTACK_GET(name,env->GetObjectField(method,nameField));
        ATTACK_GET(desc,env->GetObjectField(method,descField));
        const bool nameMatches=env->CallBooleanMethod(name,equals,wantedName)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        const bool descriptorMatches=
            env->CallBooleanMethod(desc,equals,wantedDescriptor)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        if(!nameMatches||!descriptorMatches) continue;
        ATTACK_GET(instructions,env->GetObjectField(method,insnsField));
        ATTACK_GET(entry,env->NewObject(insnsClass,insnsCtor));
        ATTACK_GET(self,env->NewObject(varInsnClass,varInsnCtor,25,0));
        ATTACK_GET(attacker,env->NewObject(varInsnClass,varInsnCtor,25,1));
        ATTACK_DO(env->CallVoidMethod(entry,add,self));
        ATTACK_DO(env->CallVoidMethod(entry,add,attacker));
        ATTACK_GET(arbitrate,env->NewObject(callClass,callCtor,184,bridgeName,
            bridgeMethod,bridgeDescriptor,JNI_FALSE));
        ATTACK_DO(env->CallVoidMethod(entry,add,arbitrate));
        ATTACK_GET(continueLabel,env->NewObject(labelClass,labelCtor));
        ATTACK_GET(pass,env->NewObject(jumpClass,jumpCtor,153,continueLabel));
        ATTACK_DO(env->CallVoidMethod(entry,add,pass));
        ATTACK_GET(ret,env->NewObject(insnClass,insnCtor,177));
        ATTACK_DO(env->CallVoidMethod(entry,add,ret));
        ATTACK_DO(env->CallVoidMethod(entry,add,continueLabel));
        ATTACK_GET(frame,env->NewObject(frameClass,frameCtor,3,0,nullptr,0,nullptr));
        ATTACK_DO(env->CallVoidMethod(entry,add,frame));
        ATTACK_DO(env->CallVoidMethod(instructions,insert,entry));
        ++transformed;
    }
    if(transformed!=1) return nullptr;
    ATTACK_GET(writer,env->NewObject(writerClass,writerCtor,1));
    ATTACK_DO(env->CallVoidMethod(node,acceptNode,writer));
    ATTACK_GET(output,static_cast<jbyteArray>(env->CallObjectMethod(writer,toBytes)));
    return output;
#undef ATTACK_GET
#undef ATTACK_DO
}

} // namespace mcoverlay
