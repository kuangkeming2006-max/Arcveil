#pragma once

#include <jni.h>
#include <span>

namespace mcoverlay {

inline jbyteArray weaveInteractionMethods(
    JNIEnv* env,std::span<const unsigned char> bytes,
    const char* clickName,const char* clickDescriptor,
    const char* heldName,const char* heldDescriptor) noexcept {
#define INTERACT_GET(var, expression) auto var=(expression); if(env->ExceptionCheck() || !var) return nullptr
#define INTERACT_DO(expression) expression; if(env->ExceptionCheck()) return nullptr
    INTERACT_GET(readerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassReader"));
    INTERACT_GET(nodeClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/ClassNode"));
    INTERACT_GET(writerClass,env->FindClass("jdk/internal/org/objectweb/asm/ClassWriter"));
    INTERACT_GET(methodClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodNode"));
    INTERACT_GET(listClass,env->FindClass("java/util/List"));
    INTERACT_GET(insnsClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnList"));
    INTERACT_GET(insnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/InsnNode"));
    INTERACT_GET(varInsnClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/VarInsnNode"));
    INTERACT_GET(callClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/MethodInsnNode"));
    INTERACT_GET(labelClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/LabelNode"));
    INTERACT_GET(frameClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/FrameNode"));
    INTERACT_GET(jumpClass,env->FindClass("jdk/internal/org/objectweb/asm/tree/JumpInsnNode"));
    INTERACT_GET(readerCtor,env->GetMethodID(readerClass,"<init>","([B)V"));
    INTERACT_GET(nodeCtor,env->GetMethodID(nodeClass,"<init>","()V"));
    INTERACT_GET(writerCtor,env->GetMethodID(writerClass,"<init>","(I)V"));
    INTERACT_GET(acceptReader,env->GetMethodID(readerClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;I)V"));
    INTERACT_GET(acceptNode,env->GetMethodID(nodeClass,"accept","(Ljdk/internal/org/objectweb/asm/ClassVisitor;)V"));
    INTERACT_GET(toBytes,env->GetMethodID(writerClass,"toByteArray","()[B"));
    INTERACT_GET(methodsField,env->GetFieldID(nodeClass,"methods","Ljava/util/List;"));
    INTERACT_GET(nameField,env->GetFieldID(methodClass,"name","Ljava/lang/String;"));
    INTERACT_GET(descField,env->GetFieldID(methodClass,"desc","Ljava/lang/String;"));
    INTERACT_GET(insnsField,env->GetFieldID(methodClass,"instructions","Ljdk/internal/org/objectweb/asm/tree/InsnList;"));
    INTERACT_GET(listSize,env->GetMethodID(listClass,"size","()I"));
    INTERACT_GET(listGet,env->GetMethodID(listClass,"get","(I)Ljava/lang/Object;"));
    INTERACT_GET(insnsCtor,env->GetMethodID(insnsClass,"<init>","()V"));
    INTERACT_GET(insnCtor,env->GetMethodID(insnClass,"<init>","(I)V"));
    INTERACT_GET(varInsnCtor,env->GetMethodID(varInsnClass,"<init>","(II)V"));
    INTERACT_GET(callCtor,env->GetMethodID(callClass,"<init>","(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V"));
    INTERACT_GET(labelCtor,env->GetMethodID(labelClass,"<init>","()V"));
    INTERACT_GET(frameCtor,env->GetMethodID(frameClass,"<init>","(II[Ljava/lang/Object;I[Ljava/lang/Object;)V"));
    INTERACT_GET(jumpCtor,env->GetMethodID(jumpClass,"<init>","(ILjdk/internal/org/objectweb/asm/tree/LabelNode;)V"));
    INTERACT_GET(add,env->GetMethodID(insnsClass,"add","(Ljdk/internal/org/objectweb/asm/tree/AbstractInsnNode;)V"));
    INTERACT_GET(insert,env->GetMethodID(insnsClass,"insert","(Ljdk/internal/org/objectweb/asm/tree/InsnList;)V"));
    INTERACT_GET(stringClass,env->FindClass("java/lang/String"));
    INTERACT_GET(equals,env->GetMethodID(stringClass,"equals","(Ljava/lang/Object;)Z"));
    const bool wantClick=clickName&&*clickName&&clickDescriptor&&*clickDescriptor;
    const bool wantHeld=heldName&&*heldName&&heldDescriptor&&*heldDescriptor;
    if(!wantClick&&!wantHeld) return nullptr;
    INTERACT_GET(clickWanted,env->NewStringUTF(wantClick?clickName:""));
    INTERACT_GET(clickDescWanted,env->NewStringUTF(wantClick?clickDescriptor:""));
    INTERACT_GET(heldWanted,env->NewStringUTF(wantHeld?heldName:""));
    INTERACT_GET(heldDescWanted,env->NewStringUTF(wantHeld?heldDescriptor:""));
    INTERACT_GET(bridgeName,env->NewStringUTF("mcoverlay/NativeLogicalBridge_v32"));
    INTERACT_GET(consumeClick,env->NewStringUTF("consumeClick"));
    INTERACT_GET(consumeClickDesc,env->NewStringUTF("(Ljava/lang/Object;)Z"));
    INTERACT_GET(consumeHeld,env->NewStringUTF("consumeHeld"));
    INTERACT_GET(consumeHeldDesc,env->NewStringUTF("(Ljava/lang/Object;Z)Z"));
    INTERACT_GET(input,env->NewByteArray(static_cast<jsize>(bytes.size())));
    INTERACT_DO(env->SetByteArrayRegion(input,0,static_cast<jsize>(bytes.size()),
        reinterpret_cast<const jbyte*>(bytes.data())));
    INTERACT_GET(reader,env->NewObject(readerClass,readerCtor,input));
    INTERACT_GET(node,env->NewObject(nodeClass,nodeCtor));
    INTERACT_DO(env->CallVoidMethod(reader,acceptReader,node,0));
    INTERACT_GET(methods,env->GetObjectField(node,methodsField));
    const jint count=env->CallIntMethod(methods,listSize);
    if(env->ExceptionCheck()||count<0||count>8192) return nullptr;
    int transformed=0;
    for(jint index=0;index<count;++index) {
        INTERACT_GET(method,env->CallObjectMethod(methods,listGet,index));
        INTERACT_GET(methodName,env->GetObjectField(method,nameField));
        INTERACT_GET(methodDesc,env->GetObjectField(method,descField));
        const bool clickNameMatches=
            env->CallBooleanMethod(methodName,equals,clickWanted)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        const bool clickDescriptorMatches=
            env->CallBooleanMethod(methodDesc,equals,clickDescWanted)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        const bool heldNameMatches=
            env->CallBooleanMethod(methodName,equals,heldWanted)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        const bool heldDescriptorMatches=
            env->CallBooleanMethod(methodDesc,equals,heldDescWanted)==JNI_TRUE;
        if(env->ExceptionCheck()) return nullptr;
        const bool isClick=wantClick&&clickNameMatches&&clickDescriptorMatches;
        const bool isHeld=wantHeld&&heldNameMatches&&heldDescriptorMatches;
        if(!isClick&&!isHeld) continue;
        INTERACT_GET(instructions,env->GetObjectField(method,insnsField));
        INTERACT_GET(entry,env->NewObject(insnsClass,insnsCtor));
        INTERACT_GET(subject,env->NewObject(varInsnClass,varInsnCtor,25,0));
        INTERACT_DO(env->CallVoidMethod(entry,add,subject));
        if(isHeld) {
            INTERACT_GET(value,env->NewObject(varInsnClass,varInsnCtor,21,1));
            INTERACT_DO(env->CallVoidMethod(entry,add,value));
        }
        INTERACT_GET(call,env->NewObject(callClass,callCtor,184,bridgeName,
            isHeld?consumeHeld:consumeClick,isHeld?consumeHeldDesc:consumeClickDesc,JNI_FALSE));
        INTERACT_DO(env->CallVoidMethod(entry,add,call));
        INTERACT_GET(continuation,env->NewObject(labelClass,labelCtor));
        INTERACT_GET(jump,env->NewObject(jumpClass,jumpCtor,153,continuation));
        INTERACT_DO(env->CallVoidMethod(entry,add,jump));
        INTERACT_GET(ret,env->NewObject(insnClass,insnCtor,177));
        INTERACT_DO(env->CallVoidMethod(entry,add,ret));
        INTERACT_DO(env->CallVoidMethod(entry,add,continuation));
        // The injected IFEQ introduces a new branch target.  Preserve the
        // original class frames and provide the one new SAME frame explicitly
        // instead of asking ClassWriter(COMPUTE_FRAMES) to resolve Lunar's
        // transformed class hierarchy through the system loader.
        INTERACT_GET(frame,env->NewObject(frameClass,frameCtor,3,0,nullptr,0,nullptr));
        INTERACT_DO(env->CallVoidMethod(entry,add,frame));
        INTERACT_DO(env->CallVoidMethod(instructions,insert,entry));
        ++transformed;
    }
    const int expectedTransforms=(wantClick?1:0)+(wantHeld?1:0);
    if(transformed!=expectedTransforms) return nullptr;
    // COMPUTE_MAXS only. Existing StackMapTable frames are retained and the
    // injected branch target above owns an explicit F_SAME frame.
    INTERACT_GET(writer,env->NewObject(writerClass,writerCtor,1));
    INTERACT_DO(env->CallVoidMethod(node,acceptNode,writer));
    INTERACT_GET(output,static_cast<jbyteArray>(env->CallObjectMethod(writer,toBytes)));
    return output;
#undef INTERACT_GET
#undef INTERACT_DO
}

} // namespace mcoverlay
