// Diagnostic: is my LEA scanner working, and how are .rdata strings reached?
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;

public class diag extends GhidraScript {
    long BASE;
    Memory mem; AddressFactory fact;
    Address addr(long v) { return fact.getDefaultAddressSpace().getAddress(v); }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        mem = currentProgram.getMemory(); fact = currentProgram.getAddressFactory();
        String outPath = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\diag.txt";
        PrintWriter f = new PrintWriter(outPath);

        f.println("## memory blocks");
        for (MemoryBlock b : mem.getBlocks())
            f.printf("  %-12s rva 0x%08x size 0x%08x  r=%b w=%b x=%b init=%b%n",
                b.getName(), b.getStart().getOffset()-BASE, b.getSize(),
                b.isRead(), b.isWrite(), b.isExecute(), b.isInitialized());

        Set<Integer> modrm = new HashSet<>(Arrays.asList(0x05,0x0D,0x15,0x1D,0x25,0x2D,0x35,0x3D));
        long totalLea = 0, leaToRdata = 0;
        long FMT = BASE + 0x01f4a070L;             // the "GetPlayerViewPoint FAILED" string
        List<Long> leaToFmt = new ArrayList<>();
        long rdataLo = BASE + 0x01e00000L, rdataHi = BASE + 0x02e00000L;
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isExecute() || !blk.isInitialized()) continue;
            long start = blk.getStart().getOffset();
            byte[] buf = new byte[(int) blk.getSize()];
            mem.getBytes(blk.getStart(), buf);
            for (int i = 0; i + 7 <= buf.length; i++) {
                int b0 = buf[i]&0xFF; if (b0!=0x48 && b0!=0x4C) continue;
                if ((buf[i+1]&0xFF)!=0x8D) continue;
                if (!modrm.contains(buf[i+2]&0xFF)) continue;
                int disp = (buf[i+3]&0xFF)|((buf[i+4]&0xFF)<<8)|((buf[i+5]&0xFF)<<16)|((buf[i+6]&0xFF)<<24);
                long tgt = start + i + 7 + disp;
                totalLea++;
                if (tgt>=rdataLo && tgt<rdataHi) leaToRdata++;
                if (tgt==FMT) leaToFmt.add(start+i);
            }
        }
        f.printf("%ntotal rip-rel LEA in exec mem: %d%n", totalLea);
        f.printf("LEA targeting .rdata-ish [0x01e00000..0x02e00000): %d%n", leaToRdata);
        f.printf("LEA targeting 0x01f4a070 (fmt str): %d%n", leaToFmt.size());
        for (Long a : leaToFmt) f.printf("   @ rva 0x%08x%n", a-BASE);

        // pointer-table slots that hold the absolute address of the fmt string
        f.println("\n## 8-byte slots in initialized memory equal to &fmtstr (pointer-to-string):");
        long want = FMT;
        int ptrHits = 0;
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isInitialized()) continue;
            long start = blk.getStart().getOffset();
            byte[] buf = new byte[(int) blk.getSize()];
            mem.getBytes(blk.getStart(), buf);
            for (int i = 0; i + 8 <= buf.length; i += 1) {
                long v = 0; for (int k=0;k<8;k++) v |= ((long)(buf[i+k]&0xFF))<<(8*k);
                if (v == want) { f.printf("   ptr@ rva 0x%08x (blk %s)%n", start+i-BASE, blk.getName()); if (++ptrHits>=12) break; }
            }
            if (ptrHits>=12) break;
        }
        if (ptrHits==0) f.println("   (none)");
        f.close();
        println("Wrote " + outPath + " totalLea=" + totalLea + " leaToFmt=" + leaToFmt.size() + " ptrHits=" + ptrHits);
    }
}
