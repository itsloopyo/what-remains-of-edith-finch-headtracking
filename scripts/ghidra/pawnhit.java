// Two open offsets for the lean clamp: which member of AController holds the
// Pawn (the trace has to ignore the player capsule the camera sits inside), and
// where FHitResult keeps the hit distance. The trace workers are decompiled
// alongside because they are what writes the hit result fields.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class pawnhit extends GhidraScript {

    static final long[] FN_RVAS = {
        0x00ff2410L,   // AController::GetPlayerViewPoint base - reads Pawn
        0x010c0db0L,   // sphere trace worker - writes FHitResult
        0x010b6980L,   // line trace worker - writes FHitResult
    };

    public void run() throws Exception {
        long BASE = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();
        AddressFactory fact = currentProgram.getAddressFactory();
        ReferenceManager rm = currentProgram.getReferenceManager();

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        String out = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\pawnhit.txt";
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
