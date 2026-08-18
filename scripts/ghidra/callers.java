// Identify the GetPlayerViewPoint call sites captured at runtime (inject mode 0
// logs each caller's return RVA). For every RVA supplied below: report the
// containing function, its size, who calls THAT function, and decompile it, so
// the render-path caller (ULocalPlayer::GetViewPoint, reached from
// CalcSceneView) can be told apart from the audio-listener / trace callers.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class callers extends GhidraScript {

    static final long[] RET_RVAS = {
        0x0110d073L, 0x010d37b7L, 0x0104396aL,
        0x00183be3L, 0x01242b70L, 0x010cfeefL,
    };

    public void run() throws Exception {
        long BASE = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();
        AddressFactory fact = currentProgram.getAddressFactory();
        ReferenceManager rm = currentProgram.getReferenceManager();

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        String out = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\callers.txt";
        PrintWriter f = new PrintWriter(out);
        f.printf("caller identification. image base 0x%x%n%n", BASE);

        for (long rva : RET_RVAS) {
            Address a = fact.getDefaultAddressSpace().getAddress(BASE + rva);
            Function fn = fm.getFunctionContaining(a);
            f.printf("==================== ret RVA 0x%08x ====================%n", rva);
            if (fn == null) {
                f.printf("  no containing function; disassembling%n");
                disassemble(a);
                fn = fm.getFunctionContaining(a);
            }
            if (fn == null) { f.printf("  STILL no function%n%n"); continue; }

            long fnRva = fn.getEntryPoint().getOffset() - BASE;
            f.printf("  containing fn %s @ rva 0x%08x  size=0x%x  (call site is +0x%x into it)%n",
                fn.getName(), fnRva, fn.getBody().getNumAddresses(),
                (BASE + rva) - fn.getEntryPoint().getOffset());

            // Who calls this function? Names the layer above (CalcSceneView,
            // the audio listener update, the interaction trace, ...).
            Set<Long> ups = new TreeSet<>();
            for (Reference r : rm.getReferencesTo(fn.getEntryPoint())) {
                if (!r.getReferenceType().isCall()) continue;
                Function up = fm.getFunctionContaining(r.getFromAddress());
                if (up != null) ups.add(up.getEntryPoint().getOffset() - BASE);
            }
            f.printf("  direct callers (%d): ", ups.size());
            for (long u : ups) f.printf("0x%08x ", u);
            f.printf("%n");
            if (ups.isEmpty())
                f.printf("  (none - virtual dispatch target, or refs not built)%n");

            DecompileResults dr = di.decompileFunction(fn, 90, monitor);
            if (dr != null && dr.decompileCompleted()) {
                f.printf("%n%s%n", dr.getDecompiledFunction().getC());
            } else {
                f.printf("  decompile failed: %s%n%n",
                    dr == null ? "null" : dr.getErrorMessage());
            }
        }
        f.close();
        println("wrote " + out);
    }
}
