// FOV discovery: decompile ULocalPlayer::GetViewPoint (the render-path
// GetPlayerViewPoint call site we inject at) plus everything it calls, so we can
// see where FMinimalViewInfo.FOV comes from and whether the write happens before
// or after GetPlayerViewPoint. RVAs are relative to the decrypted-dump image.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class fov extends GhidraScript {

    static final long[] SEED_RVAS = {
        0x010cfe60L,   // ULocalPlayer::GetViewPoint (call site 0x010cfeef is +0x8f in)
    };

    public void run() throws Exception {
        long BASE = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();
        AddressFactory fact = currentProgram.getAddressFactory();

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        String out = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\fov.txt";
        PrintWriter f = new PrintWriter(out);

        Set<Long> done = new HashSet<>();
        ArrayDeque<Long> queue = new ArrayDeque<>();
        for (long r : SEED_RVAS) queue.add(r);
        int depth = 0;

        while (!queue.isEmpty() && depth < 40) {
            long rva = queue.poll();
            if (!done.add(rva)) continue;
            depth++;

            Address a = fact.getDefaultAddressSpace().getAddress(BASE + rva);
            Function fn = fm.getFunctionContaining(a);
            f.printf("==================== fn rva 0x%08x ====================%n", rva);
            if (fn == null) { f.printf("  no function%n%n"); continue; }
            f.printf("  %s size=0x%x%n", fn.getName(), fn.getBody().getNumAddresses());

            // Call targets one level down, so GetFOVAngle / GetCameraCachePOV
            // come along without a second run.
            if (rva == SEED_RVAS[0]) {
                for (Instruction ins = getInstructionAt(fn.getEntryPoint());
                     ins != null && fn.getBody().contains(ins.getAddress());
                     ins = ins.getNext()) {
                    if (!ins.getFlowType().isCall()) continue;
                    for (Address t : ins.getFlows()) {
                        Function callee = fm.getFunctionContaining(t);
                        if (callee != null)
                            queue.add(callee.getEntryPoint().getOffset() - BASE);
                    }
                }
            }

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
