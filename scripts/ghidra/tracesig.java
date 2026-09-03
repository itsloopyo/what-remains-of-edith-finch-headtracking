// Decompile the UE4 Blueprint trace statics the lean clamp calls, plus the exec
// thunk that wraps one of them. The thunk fetches parameters in declaration
// order, so reading it alongside the static is what pins the argument list
// without guessing at a remembered UE4 signature.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class tracesig extends GhidraScript {

    static final long[] FN_RVAS = {
        0x010c10f0L,   // UKismetSystemLibrary::SphereTraceSingle (static)
        0x010b6c90L,   // UKismetSystemLibrary::LineTraceSingle (static)
        0x016a8230L,   // SphereTraceSingle_NEW exec thunk (parameter order)
    };

    public void run() throws Exception {
        long BASE = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();
        AddressFactory fact = currentProgram.getAddressFactory();
        ReferenceManager rm = currentProgram.getReferenceManager();

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        String out = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\tracesig.txt";
        PrintWriter f = new PrintWriter(out);

        for (long rva : FN_RVAS) {
            Address a = fact.getDefaultAddressSpace().getAddress(BASE + rva);
            Function fn = fm.getFunctionContaining(a);
            f.printf("==================== fn rva 0x%08x ====================%n", rva);
            if (fn == null) { f.printf("  no function%n%n"); continue; }
            f.printf("  %s size=0x%x%n", fn.getName(), fn.getBody().getNumAddresses());

            Set<Long> ups = new TreeSet<>();
            for (Reference r : rm.getReferencesTo(fn.getEntryPoint())) {
                if (!r.getReferenceType().isCall()) continue;
                Function up = fm.getFunctionContaining(r.getFromAddress());
                if (up != null) ups.add(up.getEntryPoint().getOffset() - BASE);
            }
            f.printf("  direct callers (%d): ", ups.size());
            for (long u : ups) f.printf("0x%08x ", u);
            f.printf("%n");

            DecompileResults dr = di.decompileFunction(fn, 120, monitor);
            if (dr != null && dr.decompileCompleted())
                f.printf("%n%s%n", dr.getDecompiledFunction().getC());
            else
                f.printf("  decompile failed%n%n");
        }
        f.close();
        println("wrote " + out);
    }
}
