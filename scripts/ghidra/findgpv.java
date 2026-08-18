// Pin APlayerController::GetPlayerViewPoint. It is the vtable[0x618] override
// that, in its PlayerCameraManager-null fallback, calls the base
// AController::GetPlayerViewPoint (FUN @ 0x00ff2410, which jmps vtable[0x510] =
// GetActorEyesViewPoint). So: collect every distinct vtable-slot-0x618 target,
// and report the ones whose body CALLs 0x00ff2410 - that is the camera override.
// Decompile the matches.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class findgpv extends GhidraScript {
    long BASE, TEXT_LO, TEXT_HI;
    Memory mem;
    boolean isText(long p) { return p >= TEXT_LO && p < TEXT_HI; }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        AddressFactory fact = currentProgram.getAddressFactory();
        Listing listing = currentProgram.getListing();
        TEXT_LO = BASE + 0x1000; TEXT_HI = BASE + 0x1f1c000;
        long BASE_GPV = BASE + 0x00ff2410L;   // AController::GetPlayerViewPoint

        // 1. collect distinct slot-0x618 targets
        Set<Long> targets = new HashSet<>();
        for (MemoryBlock blk : mem.getBlocks()) {
            if (blk.isExecute() || !blk.isInitialized()) continue;
            long start = blk.getStart().getOffset();
            byte[] buf = new byte[(int) blk.getSize()];
            mem.getBytes(blk.getStart(), buf);
            int n = buf.length / 8;
            long prev = -1; int run = 0; long runStart = 0;
            long[] p = new long[n];
            for (int i=0;i<n;i++){ long v=0; for(int k=0;k<8;k++) v|=((long)(buf[i*8+k]&0xFF))<<(8*k); p[i]=v; }
            int i=0;
            while (i<n) {
                if (!isText(p[i])) { i++; continue; }
                int j=i; while(j<n && isText(p[j])) j++;
                if (j-i >= 196) targets.add(p[i+195]);
                i=j;
            }
        }

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        String outPath = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\gpv.txt";
        PrintWriter f = new PrintWriter(outPath);
        f.printf("GPV pin. base 0x%x  baseAControllerGPV=0x00ff2410  %d distinct slot618 targets%n%n",
            BASE, targets.size());

        for (long t : targets) {
            Address a = fact.getDefaultAddressSpace().getAddress(t);
            Function fn = fm.getFunctionAt(a);
            if (fn == null) { disassemble(a); fn = createFunction(a, null); }
            if (fn == null) continue;
            // scan for a direct CALL to BASE_GPV
            boolean callsBase = false;
            InstructionIterator it = listing.getInstructions(fn.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                String m = ins.getMnemonicString();
                if (m == null || !(m.toLowerCase().startsWith("call") || m.toLowerCase().startsWith("jmp"))) continue;
                for (Reference r : ins.getReferencesFrom())
                    if (r.getToAddress() != null && r.getToAddress().getOffset() == BASE_GPV) callsBase = true;
            }
            if (!callsBase) continue;
            f.printf("==== slot618 candidate @rva 0x%08x CALLS base GPV -> APlayerController::GetPlayerViewPoint ====%n", t - BASE);
            DecompileResults res = di.decompileFunction(fn, 60, monitor);
            if (res != null && res.getDecompiledFunction() != null) f.println(res.getDecompiledFunction().getC());
        }
        f.close();
        di.dispose();
        println("Wrote " + outPath);
    }
}
