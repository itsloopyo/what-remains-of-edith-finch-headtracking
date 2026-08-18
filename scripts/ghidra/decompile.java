// Targeted decompiler: decompiles a fixed set of candidate RVAs (and the call
// targets one level down) so we can read GetPlayerViewPoint's body. Decompiling
// only these clean functions avoids the packed-function runaway. RVAs are
// relative to the decrypted-dump image base.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class decompile extends GhidraScript {
    long BASE;
    long rva(Address a) { return a.getOffset() - BASE; }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();
        AddressFactory fact = currentProgram.getAddressFactory();

        long[] absAddrs = {
            0x7ff654cee6a0L,   // 0x0111e6a0 (43 vtables)
            0x7ff654bc2410L,   // 0x00ff2410 (3) - AController base GPV (-> GetActorEyes)
            0x7ff654db2ef0L,   // 0x011e2ef0 (3)
            0x7ff6542818f0L,   // 0x006b18f0 (3)
            0x7ff653cf2be0L,   // 0x00122be0 (3)
            0x7ff653cf2c10L,   // 0x00122c10 (4)
        };

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        String outPath = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\decompile.txt";
        PrintWriter f = new PrintWriter(outPath);

        Set<Long> done = new HashSet<>();
        ArrayDeque<Long> queue = new ArrayDeque<>();
        for (long r : absAddrs) queue.add(r);

        while (!queue.isEmpty()) {
            long abs = queue.poll();
            if (!done.add(abs)) continue;
            long r = abs - BASE;
            Address a = fact.getDefaultAddressSpace().getAddress(abs);
            Function fn = fm.getFunctionAt(a);
            if (fn == null) {
                disassemble(a);
                fn = createFunction(a, null);
            }
            f.printf("%n================ fn @ rva 0x%08x (%s) ================%n", r,
                fn != null ? fn.getName() : "NO FUNCTION (create failed)");
            if (fn == null) continue;
            DecompileResults res = di.decompileFunction(fn, 60, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                f.println(res.getDecompiledFunction().getC());
            } else {
                f.println("  (decompile failed)");
            }
        }
        f.close();
        di.dispose();
        println("Wrote " + outPath);
    }
}
