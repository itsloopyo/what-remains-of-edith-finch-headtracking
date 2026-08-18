// Find the concrete APlayerController::GetPlayerViewPoint. We proved GPV is the
// virtual at vtable byte-offset 0x618 (K2_GetPlayerViewPoint tail-calls
// this->vtable[0x618]; the BP-lib GetPlayerViewPoint calls pc->vtable[0x618]).
// Scan all read-only data for vtables (runs of >=196 pointers into .text) and
// read slot 195 (offset 0x618) of each. The GPV implementation is the slot-0x618
// target shared by the controller-shaped vtables; tally and report the winners.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.listing.*;

public class findvtables extends GhidraScript {
    long BASE, TEXT_LO, TEXT_HI;
    Memory mem;

    boolean isText(long p) { return p >= TEXT_LO && p < TEXT_HI; }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        AddressFactory fact = currentProgram.getAddressFactory();
        TEXT_LO = BASE + 0x1000; TEXT_HI = BASE + 0x1f1c000;

        String outPath = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\vtables.txt";
        PrintWriter f = new PrintWriter(outPath);
        f.printf("vtable scan. base 0x%x  slot 0x618 (idx 195) = GetPlayerViewPoint%n%n", BASE);

        Map<Long,Integer> slot618 = new HashMap<>();   // target RVA -> count
        int vtblCount = 0;
        for (MemoryBlock blk : mem.getBlocks()) {
            if (blk.isExecute() || !blk.isInitialized()) continue;   // data blocks only
            if (blk.isWrite() && !blk.getName().equals(".data")) continue;
            long start = blk.getStart().getOffset();
            byte[] buf = new byte[(int) blk.getSize()];
            mem.getBytes(blk.getStart(), buf);
            int n = buf.length / 8;
            long[] p = new long[n];
            for (int i = 0; i < n; i++) {
                long v = 0; for (int k=0;k<8;k++) v |= ((long)(buf[i*8+k]&0xFF))<<(8*k);
                p[i] = v;
            }
            int i = 0;
            while (i < n) {
                if (!isText(p[i])) { i++; continue; }
                int j = i;
                while (j < n && isText(p[j])) j++;
                int runLen = j - i;
                if (runLen >= 196) {
                    vtblCount++;
                    long vtblAddr = start + (long)i*8;
                    long gpv = p[i + 195];
                    slot618.merge(gpv - BASE, 1, Integer::sum);
                    if ((gpv - BASE) == 0x01112680L)
                        f.printf("GPV-VTABLE @rva 0x%08x len %d%n", vtblAddr - BASE, runLen);
                }
                i = j;
            }
        }
        f.printf("%n%d candidate vtables (len>=196)%n", vtblCount);
        f.println("\n## most common slot-0x618 targets (GPV candidates):");
        List<Map.Entry<Long,Integer>> s = new ArrayList<>(slot618.entrySet());
        s.sort((a,b)->b.getValue()-a.getValue());
        for (int k=0;k<s.size();k++) {
            long rva = s.get(k).getKey();
            Address a = fact.getDefaultAddressSpace().getAddress(BASE+rva);
            StringBuilder pro = new StringBuilder();
            for (int b=0;b<14;b++){ int bb=0; try{bb=mem.getByte(a.add(b))&0xFF;}catch(Exception e){} pro.append(String.format("%02x ",bb)); }
            f.printf("  0x%08x  (in %d vtables)  prologue: %s%n", rva, s.get(k).getValue(), pro.toString().trim());
        }
        f.close();
        println("Wrote " + outPath + " (" + vtblCount + " vtables)");
    }
}
