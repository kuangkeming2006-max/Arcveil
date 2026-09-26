import java.lang.instrument.*;
import java.security.ProtectionDomain;
public class MappingCaptureFixture {
    public static void premain(String options, Instrumentation instrumentation) {
        instrumentation.addTransformer(new ClassFileTransformer() {
            public byte[] transform(ClassLoader loader, String name, Class<?> type,
                                    ProtectionDomain domain, byte[] bytes) {
                if (!name.equals("MappingCaptureSubject")) return null;
                byte[] modified = bytes.clone();
                byte[] before = "mapping-original".getBytes(java.nio.charset.StandardCharsets.UTF_8);
                byte[] after = "mapping-modified".getBytes(java.nio.charset.StandardCharsets.UTF_8);
                for (int i=0;i<=modified.length-before.length;i++) {
                    boolean equal=true; for(int j=0;j<before.length;j++) equal &= modified[i+j]==before[j];
                    if(equal)System.arraycopy(after,0,modified,i,after.length);
                }
                return modified;
            }
        });
    }
    public static void main(String[] args) throws Exception {
        System.out.println(MappingCaptureSubject.value());
        System.out.flush();
        Thread.sleep(120000);
    }
}
class MappingCaptureSubject {
    private int state;
    public static String value() { return "mapping-original"; }
    public int compute(int x) { state += x * 3; return state + 17; }
}
