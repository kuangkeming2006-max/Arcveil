#pragma once

#include <jni.h>
#include <span>
#include <array>
#include <string>

namespace mcoverlay {

// Void entry observer: preserve original arguments, return values and control flow.
// COMPUTE_MAXS suffices for the straight-line prefix; preserve stack-map frames.
inline jbyteArray weaveDiagnosticMethods(
    JNIEnv* env,std::span<const unsigned char> bytes,
    const std::array<std::string,4>& names,
    const std::array<std::string,4>& descriptors) noexcept {
#define DIAG_GET(var, expression) auto var=(expression); if(env->ExceptionCheck() || !var) return nullptr
#define DIAG_DO(expression) expression; if(env->ExceptionCheck()) return nullptr
    DIAG_GET(readerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassReader"));
    DIAG_GET(nodeClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/ClassNode"));
    DIAG_GET(writerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassWriter"));
    DIAG_GET(methodClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodNode"));
    DIAG_GET(listClass,env->FindClass("java/util/List"));
    DIAG_GET(insnsClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnList"));
    DIAG_GET(insnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnNode"));
    DIAG_GET(varInsnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/VarInsnNode"));
    DIAG_GET(callClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodInsnNode"));
    DIAG_GET(readerCtor,env->GetMethodID(readerClass,"<init>","([B)V"));
    DIAG_GET(nodeCtor,env->GetMethodID(nodeClass,"<init>","()V"));
    DIAG_GET(writerCtor,env->GetMethodID(writerClass,"<init>","(I)V"));
    DIAG_GET(acceptReader,env->GetMethodID(readerClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;I)V"));
    DIAG_GET(acceptNode,env->GetMethodID(nodeClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;)V"));
    DIAG_GET(toBytes,env->GetMethodID(writerClass,"toByteArray","()[B"));
    DIAG_GET(methodsField,env->GetFieldID(nodeClass,"methods","Ljava/util/List;"));
    DIAG_GET(nameField,env->GetFieldID(methodClass,"name","Ljava/lang/String;"));
    DIAG_GET(descField,env->GetFieldID(methodClass,"desc","Ljava/lang/String;"));
    DIAG_GET(insnsField,env->GetFieldID(methodClass,"instructions","Ljdk/internal/org/objectweb/asm/tree/InsnList;"));
    DIAG_GET(listSize,env->GetMethodID(listClass,"size","()I"));
    DIAG_GET(listGet,env->GetMethodID(listClass,"get","(I)Ljava/lang/Object;"));
    DIAG_GET(insnsCtor,env->GetMethodID(insnsClass,"<init>","()V"));
    DIAG_GET(insnCtor,env->GetMethodID(insnClass,"<init>","(I)V"));
    DIAG_GET(varInsnCtor,env->GetMethodID(varInsnClass,"<init>","(II)V"));
    DIAG_GET(callCtor,env->GetMethodID(callClass,"<init>","(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V"));
    DIAG_GET(add,env->GetMethodID(insnsClass,"add","(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;)V"));
    DIAG_GET(insert,env->GetMethodID(insnsClass,"insert","(Ljdk/internal/org/objectweb/asm/tree/InsnList;)V"));
    DIAG_GET(stringClass,env->FindClass("java/lang/String"));
    DIAG_GET(equals,env->GetMethodID(stringClass,"equals","(Ljava/lang/Object;)Z"));
    std::array<jstring,4> wantedNames{},wantedDescs{};
    int expected=0;
    for(std::size_t i=0;i<names.size();++i) if(!names[i].empty()) {
        wantedNames[i]=env->NewStringUTF(names[i].c_str());
        if(env->ExceptionCheck() || !wantedNames[i]) return nullptr;
        wantedDescs[i]=env->NewStringUTF(descriptors[i].c_str());
        if(env->ExceptionCheck() || !wantedDescs[i]) return nullptr;
        ++expected;
    }
    DIAG_GET(bridgeName,env->NewStringUTF("mcoverlay/NativeDiagnosticBridge_v33"));
    DIAG_GET(observeName,env->NewStringUTF("observe"));
    DIAG_GET(observeDesc,env->NewStringUTF("(ILjava/lang/Object;)V"));
    DIAG_GET(input,env->NewByteArray(static_cast<jsize>(bytes.size())));
    DIAG_DO(env->SetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),
        reinterpret_cast<const jbyte*>(bytes.data())));
    DIAG_GET(reader,env->NewObject(readerClass,readerCtor,input));
    DIAG_GET(node,env->NewObject(nodeClass,nodeCtor));
    DIAG_DO(env->CallVoidMethod(reader,acceptReader,node,0));
    DIAG_GET(methods,env->GetObjectField(node,methodsField));
    const jint count=env->CallIntMethod(methods,listSize);
    if(env->ExceptionCheck()||count<0||count>8192) return nullptr;
    int transformed=0;
    for(jint index=0;index<count;++index) {
        DIAG_GET(method,env->CallObjectMethod(methods,listGet,index));
        DIAG_GET(methodName,env->GetObjectField(method,nameField));
        DIAG_GET(methodDesc,env->GetObjectField(method,descField));
        int kind=-1;
        for(int i=0;i<4;++i) {
            if(!wantedNames[i]) continue;
            const bool sameName=env->CallBooleanMethod(methodName,equals,wantedNames[i])==JNI_TRUE;
            if(env->ExceptionCheck()) return nullptr;
            const bool sameDesc=env->CallBooleanMethod(methodDesc,equals,wantedDescs[i])==JNI_TRUE;
            if(env->ExceptionCheck()) return nullptr;
            if(sameName && sameDesc) {kind=i;break;}
        }
        if(kind<0) {
            env->DeleteLocalRef(methodName);env->DeleteLocalRef(methodDesc);env->DeleteLocalRef(method);
            continue;
        }
        DIAG_GET(instructions,env->GetObjectField(method,insnsField));
        DIAG_GET(entry,env->NewObject(insnsClass,insnsCtor));
        DIAG_GET(event,env->NewObject(insnClass,insnCtor,3+kind)); // ICONST_0..3
        DIAG_DO(env->CallVoidMethod(entry,add,event));
        jobject argument=kind==3 ? env->NewObject(insnClass,insnCtor,1) // ACONST_NULL
            : env->NewObject(varInsnClass,varInsnCtor,25,kind==0 ? 2 : 1); // ALOAD target/position
        if(env->ExceptionCheck() || !argument) return nullptr;
        DIAG_DO(env->CallVoidMethod(entry,add,argument));
        DIAG_GET(call,env->NewObject(callClass,callCtor,184,bridgeName,observeName,observeDesc,JNI_FALSE));
        DIAG_DO(env->CallVoidMethod(entry,add,call));
        DIAG_DO(env->CallVoidMethod(instructions,insert,entry));
        ++transformed;
    }
    if(expected==0 || transformed!=expected) return nullptr;
    DIAG_GET(writer,env->NewObject(writerClass,writerCtor,1));
    DIAG_DO(env->CallVoidMethod(node,acceptNode,writer));
    DIAG_GET(output,static_cast<jbyteArray>(env->CallObjectMethod(writer,toBytes)));
    return output;
#undef DIAG_GET
#undef DIAG_DO
}

} // namespace mcoverlay
