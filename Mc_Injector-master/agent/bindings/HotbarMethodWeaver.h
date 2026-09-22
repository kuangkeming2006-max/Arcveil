#pragma once
#include <jni.h>
#include <span>

namespace mcoverlay {
// Preserve vanilla pressTime decrement, then filter each boolean return.
// Stack frames/branches stay unchanged: [pressed] -> [pressed, this] -> [result].
inline jbyteArray weaveHotbarMethod(JNIEnv* env,
                                         std::span<const unsigned char> bytes,
                                         const char* name,
                                         const char* descriptor,
                                         const bool itemUse=false) noexcept {
#define HOTBAR_GET(var, expression) auto var=(expression); if(env->ExceptionCheck() || !var) return nullptr
#define HOTBAR_DO(expression) expression; if(env->ExceptionCheck()) return nullptr
    HOTBAR_GET(readerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassReader"));
    HOTBAR_GET(nodeClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/ClassNode"));
    HOTBAR_GET(writerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassWriter"));
    HOTBAR_GET(methodClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodNode"));
    HOTBAR_GET(listClass,env->FindClass("java/util/List"));
    HOTBAR_GET(insnsClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnList"));
    HOTBAR_GET(varInsnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/VarInsnNode"));
    HOTBAR_GET(insnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnNode"));
    HOTBAR_GET(insnCtor,env->GetMethodID(insnClass,"<init>","(I)V"));
    HOTBAR_GET(callClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodInsnNode"));
    HOTBAR_GET(readerCtor,env->GetMethodID(readerClass,"<init>","([B)V"));
    HOTBAR_GET(nodeCtor,env->GetMethodID(nodeClass,"<init>","()V"));
    HOTBAR_GET(writerCtor,env->GetMethodID(writerClass,"<init>","(I)V"));
    HOTBAR_GET(acceptReader,env->GetMethodID(readerClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;I)V"));
    HOTBAR_GET(acceptNode,env->GetMethodID(nodeClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;)V"));
    HOTBAR_GET(toBytes,env->GetMethodID(writerClass,"toByteArray","()[B"));
    HOTBAR_GET(methodsField,env->GetFieldID(nodeClass,"methods","Ljava/util/List;"));
    HOTBAR_GET(nameField,env->GetFieldID(methodClass,"name","Ljava/lang/String;"));
    HOTBAR_GET(descField,env->GetFieldID(methodClass,"desc","Ljava/lang/String;"));
    HOTBAR_GET(insnsField,env->GetFieldID(methodClass,"instructions","Ljdk/internal/org/objectweb/asm/tree/InsnList;"));
    HOTBAR_GET(listSize,env->GetMethodID(listClass,"size","()I"));
    HOTBAR_GET(listGet,env->GetMethodID(listClass,"get","(I)Ljava/lang/Object;"));
    HOTBAR_GET(insnsCtor,env->GetMethodID(insnsClass,"<init>","()V"));
    HOTBAR_GET(varInsnCtor,env->GetMethodID(varInsnClass,"<init>","(II)V"));
    HOTBAR_GET(callCtor,env->GetMethodID(callClass,"<init>","(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V"));
    HOTBAR_GET(add,env->GetMethodID(insnsClass,"add","(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;)V"));
    HOTBAR_GET(insert,env->GetMethodID(insnsClass,"insertBefore","(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;Ljdk/internal/org/objectweb/asm/tree/InsnList;)V"));
    HOTBAR_GET(insertHead,env->GetMethodID(insnsClass,"insert","(Ljdk/internal/org/objectweb/asm/tree/InsnList;)V"));
    HOTBAR_GET(abstractClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/AbstractInsnNode"));
    HOTBAR_GET(toArray,env->GetMethodID(insnsClass,"toArray","()[Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;"));
    HOTBAR_GET(opcode,env->GetMethodID(abstractClass,"getOpcode","()I"));
    HOTBAR_GET(stringClass,env->FindClass("java/lang/String"));
    HOTBAR_GET(equals,env->GetMethodID(stringClass,"equals","(Ljava/lang/Object;)Z"));
    HOTBAR_GET(wantedName,env->NewStringUTF(name));
    HOTBAR_GET(wantedDesc,env->NewStringUTF(descriptor));
    HOTBAR_GET(bridgeName,env->NewStringUTF("mcoverlay/NativeHotbarBridge_v50"));
    HOTBAR_GET(transformName,env->NewStringUTF(itemUse?"useEvent":"filterPress"));
    HOTBAR_GET(transformDesc,env->NewStringUTF("(ZLjava/lang/Object;)Z"));
    HOTBAR_GET(input,env->NewByteArray(static_cast<jsize>(bytes.size())));
    HOTBAR_DO(env->SetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),reinterpret_cast<const jbyte*>(bytes.data())));
    HOTBAR_GET(reader,env->NewObject(readerClass,readerCtor,input));
    HOTBAR_GET(node,env->NewObject(nodeClass,nodeCtor));
    HOTBAR_DO(env->CallVoidMethod(reader,acceptReader,node,0));
    HOTBAR_GET(methods,env->GetObjectField(node,methodsField));
    const jint count=env->CallIntMethod(methods,listSize);
    if(env->ExceptionCheck() || count<0 || count>8192) return nullptr;
    bool found=false;
    for(jint i=0;i<count;++i) {
        HOTBAR_GET(method,env->CallObjectMethod(methods,listGet,i));
        HOTBAR_GET(methodName,env->GetObjectField(method,nameField));
        HOTBAR_GET(methodDesc,env->GetObjectField(method,descField));
        const bool sameName=env->CallBooleanMethod(methodName,equals,wantedName);
        if(env->ExceptionCheck()) return nullptr;
        const bool sameDesc=env->CallBooleanMethod(methodDesc,equals,wantedDesc);
        if(env->ExceptionCheck()) return nullptr;
        if(!sameName || !sameDesc) continue;
        HOTBAR_GET(instructions,env->GetObjectField(method,insnsField));
        if(itemUse) {
            HOTBAR_GET(head,env->NewObject(insnsClass,insnsCtor));
            HOTBAR_GET(enter,env->NewObject(insnClass,insnCtor,4));
            HOTBAR_GET(self,env->NewObject(varInsnClass,varInsnCtor,25,0));
            HOTBAR_GET(call,env->NewObject(callClass,callCtor,184,bridgeName,transformName,transformDesc,JNI_FALSE));
            HOTBAR_GET(pop,env->NewObject(insnClass,insnCtor,87));
            HOTBAR_DO(env->CallVoidMethod(head,add,enter));
            HOTBAR_DO(env->CallVoidMethod(head,add,self));
            HOTBAR_DO(env->CallVoidMethod(head,add,call));
            HOTBAR_DO(env->CallVoidMethod(head,add,pop));
            HOTBAR_DO(env->CallVoidMethod(instructions,insertHead,head));
        }
        HOTBAR_GET(array,static_cast<jobjectArray>(env->CallObjectMethod(instructions,toArray)));
        const jsize length=env->GetArrayLength(array);
        for(jsize index=0;index<length;++index) {
            if(env->PushLocalFrame(16)<0) return nullptr;
            struct Frame {JNIEnv* env;~Frame(){env->PopLocalFrame(nullptr);}} frame{env};
            HOTBAR_GET(instruction,env->GetObjectArrayElement(array,index));
            const jint op=env->CallIntMethod(instruction,opcode);
            if(env->ExceptionCheck()) return nullptr;
            if(op==(itemUse?177:172)) {
                HOTBAR_GET(sequence,env->NewObject(insnsClass,insnsCtor));
                if(itemUse) {
                    HOTBAR_GET(exit,env->NewObject(insnClass,insnCtor,3));
                    HOTBAR_DO(env->CallVoidMethod(sequence,add,exit));
                }
                HOTBAR_GET(load,env->NewObject(varInsnClass,varInsnCtor,25,0));
                HOTBAR_GET(call,env->NewObject(callClass,callCtor,184,bridgeName,transformName,transformDesc,JNI_FALSE));
                HOTBAR_DO(env->CallVoidMethod(sequence,add,load));
                HOTBAR_DO(env->CallVoidMethod(sequence,add,call));
                if(itemUse) {
                    HOTBAR_GET(pop,env->NewObject(insnClass,insnCtor,87));
                    HOTBAR_DO(env->CallVoidMethod(sequence,add,pop));
                }
                HOTBAR_DO(env->CallVoidMethod(instructions,insert,instruction,sequence));
                found=true;
            }
        }
        break;
    }
    if(!found) return nullptr;
    HOTBAR_GET(writer,env->NewObject(writerClass,writerCtor,1)); // COMPUTE_MAXS
    HOTBAR_DO(env->CallVoidMethod(node,acceptNode,writer));
    HOTBAR_GET(output,static_cast<jbyteArray>(env->CallObjectMethod(writer,toBytes)));
    return output;
#undef HOTBAR_GET
#undef HOTBAR_DO
}
}
