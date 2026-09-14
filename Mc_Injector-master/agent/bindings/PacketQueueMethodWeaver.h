#pragma once
#include <jni.h>
#include <span>

namespace mcoverlay {
// Rewrites exactly one NetHandlerPlayClient::addToSendQueue(Packet) method:
//     packet = NativePacketBridge_v33.serialize(packet);
// The CHECKCAST keeps the verifier's local-variable type equal to Packet on
// Java 8, 17 and 21. The original method body and its stack frames remain
// otherwise untouched.
inline jbyteArray weavePacketQueueMethod(JNIEnv* env,
                                         std::span<const unsigned char> bytes,
                                         const char* name,
                                         const char* descriptor,
                                         const char* packetInternalName) noexcept {
#define PACKET_GET(var, expression) auto var=(expression); if(env->ExceptionCheck() || !var) return nullptr
#define PACKET_DO(expression) expression; if(env->ExceptionCheck()) return nullptr
    PACKET_GET(readerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassReader"));
    PACKET_GET(nodeClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/ClassNode"));
    PACKET_GET(writerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassWriter"));
    PACKET_GET(methodClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodNode"));
    PACKET_GET(listClass,env->FindClass("java/util/List"));
    PACKET_GET(insnsClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnList"));
    PACKET_GET(varInsnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/VarInsnNode"));
    PACKET_GET(typeInsnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/TypeInsnNode"));
    PACKET_GET(callClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodInsnNode"));
    PACKET_GET(readerCtor,env->GetMethodID(readerClass,"<init>","([B)V"));
    PACKET_GET(nodeCtor,env->GetMethodID(nodeClass,"<init>","()V"));
    PACKET_GET(writerCtor,env->GetMethodID(writerClass,"<init>","(I)V"));
    PACKET_GET(acceptReader,env->GetMethodID(readerClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;I)V"));
    PACKET_GET(acceptNode,env->GetMethodID(nodeClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;)V"));
    PACKET_GET(toBytes,env->GetMethodID(writerClass,"toByteArray","()[B"));
    PACKET_GET(methodsField,env->GetFieldID(nodeClass,"methods","Ljava/util/List;"));
    PACKET_GET(nameField,env->GetFieldID(methodClass,"name","Ljava/lang/String;"));
    PACKET_GET(descField,env->GetFieldID(methodClass,"desc","Ljava/lang/String;"));
    PACKET_GET(insnsField,env->GetFieldID(methodClass,"instructions","Ljdk/internal/org/objectweb/asm/tree/InsnList;"));
    PACKET_GET(listSize,env->GetMethodID(listClass,"size","()I"));
    PACKET_GET(listGet,env->GetMethodID(listClass,"get","(I)Ljava/lang/Object;"));
    PACKET_GET(insnsCtor,env->GetMethodID(insnsClass,"<init>","()V"));
    PACKET_GET(varInsnCtor,env->GetMethodID(varInsnClass,"<init>","(II)V"));
    PACKET_GET(typeInsnCtor,env->GetMethodID(typeInsnClass,"<init>","(ILjava/lang/String;)V"));
    PACKET_GET(callCtor,env->GetMethodID(callClass,"<init>","(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V"));
    PACKET_GET(add,env->GetMethodID(insnsClass,"add","(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;)V"));
    PACKET_GET(insert,env->GetMethodID(insnsClass,"insert","(Ljdk/internal/org/objectweb/asm/tree/InsnList;)V"));
    PACKET_GET(stringClass,env->FindClass("java/lang/String"));
    PACKET_GET(equals,env->GetMethodID(stringClass,"equals","(Ljava/lang/Object;)Z"));
    PACKET_GET(wantedName,env->NewStringUTF(name));
    PACKET_GET(wantedDesc,env->NewStringUTF(descriptor));
    PACKET_GET(bridgeName,env->NewStringUTF("mcoverlay/NativePacketBridge_v33"));
    PACKET_GET(transformName,env->NewStringUTF("serialize"));
    PACKET_GET(transformDesc,env->NewStringUTF("(Ljava/lang/Object;)Ljava/lang/Object;"));
    PACKET_GET(packetName,env->NewStringUTF(packetInternalName));
    PACKET_GET(input,env->NewByteArray(static_cast<jsize>(bytes.size())));
    PACKET_DO(env->SetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),reinterpret_cast<const jbyte*>(bytes.data())));
    PACKET_GET(reader,env->NewObject(readerClass,readerCtor,input));
    PACKET_GET(node,env->NewObject(nodeClass,nodeCtor));
    PACKET_DO(env->CallVoidMethod(reader,acceptReader,node,0));
    PACKET_GET(methods,env->GetObjectField(node,methodsField));
    const jint count=env->CallIntMethod(methods,listSize);
    if(env->ExceptionCheck() || count<0 || count>8192) return nullptr;
    bool found=false;
    for(jint i=0;i<count;++i) {
        PACKET_GET(method,env->CallObjectMethod(methods,listGet,i));
        PACKET_GET(methodName,env->GetObjectField(method,nameField));
        PACKET_GET(methodDesc,env->GetObjectField(method,descField));
        const bool sameName=env->CallBooleanMethod(methodName,equals,wantedName);
        if(env->ExceptionCheck()) return nullptr;
        const bool sameDesc=env->CallBooleanMethod(methodDesc,equals,wantedDesc);
        if(env->ExceptionCheck()) return nullptr;
        if(!sameName || !sameDesc) continue;
        PACKET_GET(sequence,env->NewObject(insnsClass,insnsCtor));
        PACKET_GET(load,env->NewObject(varInsnClass,varInsnCtor,25,1)); // ALOAD 1
        PACKET_GET(call,env->NewObject(callClass,callCtor,184,bridgeName,transformName,transformDesc,JNI_FALSE));
        PACKET_GET(cast,env->NewObject(typeInsnClass,typeInsnCtor,192,packetName)); // CHECKCAST Packet
        PACKET_GET(store,env->NewObject(varInsnClass,varInsnCtor,58,1)); // ASTORE 1
        PACKET_DO(env->CallVoidMethod(sequence,add,load));
        PACKET_DO(env->CallVoidMethod(sequence,add,call));
        PACKET_DO(env->CallVoidMethod(sequence,add,cast));
        PACKET_DO(env->CallVoidMethod(sequence,add,store));
        PACKET_GET(instructions,env->GetObjectField(method,insnsField));
        PACKET_DO(env->CallVoidMethod(instructions,insert,sequence));
        found=true;
        break;
    }
    if(!found) return nullptr;
    PACKET_GET(writer,env->NewObject(writerClass,writerCtor,1)); // COMPUTE_MAXS
    PACKET_DO(env->CallVoidMethod(node,acceptNode,writer));
    PACKET_GET(output,static_cast<jbyteArray>(env->CallObjectMethod(writer,toBytes)));
    return output;
#undef PACKET_GET
#undef PACKET_DO
}
}
