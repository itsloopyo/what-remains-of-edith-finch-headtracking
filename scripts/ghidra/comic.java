// Find and decompile the game's own comic-panel code (Barbara's chapter,
// FinchGame/Private/Surprise/Comic*.cpp). The panel interior is drawn by
// something that never calls GetPlayerViewPoint, so the question this answers
// is how the panel view is produced at all: a second FSceneView, a view-target
// swap, a camera component the game drives itself, or none of those.
//
// Same manual rip-rel LEA scan as discover3.java - the reference DB on this
// binary does not carry code->.rdata refs, so the check()/ensure() source-path
// strings compiled into each method are the way back to the functions.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;

public class comic extends GhidraScript {
    long BASE;
    Memory mem; Listing listing; FunctionManager fm; AddressFactory fact;

    Address addr(long v) { return fact.getDefaultAddressSpace().getAddress(v); }
    long rva(Address a) { return a.getOffset() - BASE; }

    List<Address> searchBytes(byte[] pat, int max) {
        List<Address> hits = new ArrayList<>();
        Address start = mem.getMinAddress();
        while (start != null && hits.size() < max) {
            Address h = mem.findBytes(start, pat, null, true, monitor);
            if (h == null) break;
            hits.add(h);
            start = h.add(1);
        }
        return hits;
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        mem = currentProgram.getMemory(); listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager(); fact = currentProgram.getAddressFactory();

        String outPath = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\comic.txt";
        PrintWriter f = new PrintWriter(outPath);
        f.printf("Surprise/Comic code discovery. base 0x%x%n", BASE);

        String[] anchors = {
            "Surprise\\ComicPanel.cpp",
            "Surprise\\Comic.cpp",
            "Panel duration expired",
            "IsCurrentComicPanel",
            "SetIsActivePanel",
            "GetDegreesRotated",
        };

        Set<Long> targets = new HashSet<>();
        Map<Long,String> label = new HashMap<>();
        for (String a : anchors) {
            for (Address h : searchBytes(a.getBytes("US-ASCII"), 12)) {
                targets.add(h.getOffset());
                label.putIfAbsent(h.getOffset(), a);
            }
        }
        f.printf("anchor addresses: %d%n", targets.size());
        for (Long t : new TreeSet<>(targets)) f.printf("  0x%08x  %s%n", t-BASE, label.get(t));

        Set<Integer> modrm = new HashSet<>(Arrays.asList(0x05,0x0D,0x15,0x1D,0x25,0x2D,0x35,0x3D));
        LinkedHashMap<Long,String> fns = new LinkedHashMap<>();
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isExecute() || !blk.isInitialized()) continue;
            long start = blk.getStart().getOffset();
            byte[] buf = new byte[(int)Math.min(blk.getSize(), Integer.MAX_VALUE)];
            mem.getBytes(blk.getStart(), buf);
            for (int i = 0; i + 7 <= buf.length; i++) {
                int b0 = buf[i] & 0xFF;
                if (b0 != 0x48 && b0 != 0x4C) continue;
                if ((buf[i+1] & 0xFF) != 0x8D) continue;
                if (!modrm.contains(buf[i+2] & 0xFF)) continue;
                long disp = ((long)(buf[i+3]&0xFF)) | ((long)(buf[i+4]&0xFF)<<8)
                          | ((long)(buf[i+5]&0xFF)<<16) | ((long)(buf[i+6]&0xFF)<<24);
                disp = (int) disp;
                long instr = start + i;
                long tgt = instr + 7 + disp;
                if (!targets.contains(tgt)) continue;
                Function fn = fm.getFunctionContaining(addr(instr));
                if (fn == null) continue;
                fns.putIfAbsent(fn.getEntryPoint().getOffset(), label.get(tgt));
            }
        }

        f.printf("%n## %d referencing functions%n", fns.size());
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        int done = 0;
        for (Map.Entry<Long,String> e : fns.entrySet()) {
            Function fn = fm.getFunctionAt(addr(e.getKey()));
            if (fn == null) continue;
            f.printf("%n==================== fn 0x%08x  (via %s) size=0x%x ====================%n",
                e.getKey()-BASE, e.getValue(), fn.getBody().getNumAddresses());
            if (++done > 24) { f.println("  (decompile budget reached)"); continue; }
            DecompileResults dr = di.decompileFunction(fn, 90, monitor);
            if (dr != null && dr.decompileCompleted()) f.println(dr.getDecompiledFunction().getC());
            else f.println("  decompile failed");
        }
        f.close();
        println("Wrote " + outPath + " (" + fns.size() + " functions)");
    }
}
