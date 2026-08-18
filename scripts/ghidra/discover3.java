// GPV discovery by MANUAL rip-relative LEA scan (Ghidra's reference DB on this
// binary did not build code->.rdata refs, so getReferencesTo is empty). We:
//   1. Collect anchor string addresses (ASCII defined data + raw UTF-16 search).
//   2. Scan all executable memory for x64 `LEA r64,[rip+disp32]` (48/4C 8D
//      modrm(rm=101) disp32) whose computed target == an anchor string.
//   3. For each hit, report the instruction RVA, the containing function, and
//      that function's CALL targets (with prologues). The real
//      APlayerController::GetPlayerViewPoint is called by the function that
//      logs "GetPlayerViewPoint FAILED ..." and/or by the K2 exec thunk.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;

public class discover3 extends GhidraScript {
    long BASE;
    Memory mem; Listing listing; FunctionManager fm; AddressFactory fact;

    Address addr(long v) { return fact.getDefaultAddressSpace().getAddress(v); }
    long rva(Address a) { return a.getOffset() - BASE; }
    String prologue(Address a, int n) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) { int b=0; try { b = mem.getByte(a.add(i)) & 0xFF; } catch (Exception e) {} sb.append(String.format("%02x ", b)); }
        return sb.toString().trim();
    }

    // raw memory search for all occurrences of bytes
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

    List<Long> callTargets(Function fn) {
        List<Long> outs = new ArrayList<>();
        if (fn == null) return outs;
        InstructionIterator it = listing.getInstructions(fn.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            String m = ins.getMnemonicString();
            if (m == null || !m.toLowerCase().startsWith("call")) continue;
            for (ghidra.program.model.symbol.Reference r : ins.getReferencesFrom()) {
                Address ta = r.getToAddress();
                if (ta != null && fm.getFunctionAt(ta) != null) outs.add(rva(ta));
            }
        }
        return outs;
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        mem = currentProgram.getMemory(); listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager(); fact = currentProgram.getAddressFactory();

        String outPath = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\discover3.txt";
        PrintWriter f = new PrintWriter(outPath);
        f.printf("FinchGame.exe GPV discovery v3 (manual LEA scan). base 0x%x%n", BASE);

        // 1. anchor target addresses
        Set<Long> targets = new HashSet<>();           // absolute addresses
        Map<Long,String> label = new HashMap<>();
        String[] anchors = {"GetPlayerViewPoint FAILED", "K2_GetPlayerViewPoint",
                            "GetPlayerViewPoint", "GetActorEyesViewPoint"};
        // ASCII defined-data hits
        DataIterator di = listing.getDefinedData(true);
        while (di.hasNext()) {
            Data d = di.next(); Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String s = (String) v;
            for (String a : anchors) if (s.contains(a)) {
                long off = d.getAddress().getOffset();
                targets.add(off); label.put(off, "ascii:" + (s.length()>30?s.substring(0,30):s));
                break;
            }
        }
        // raw ASCII + UTF-16 search for the two key names
        for (String a : new String[]{"GetPlayerViewPoint FAILED", "K2_GetPlayerViewPoint", "GetPlayerViewPoint"}) {
            for (Address h : searchBytes(a.getBytes("US-ASCII"), 8)) { targets.add(h.getOffset()); label.putIfAbsent(h.getOffset(), "ascii-raw:"+a); }
            // UTF-16LE
            byte[] w = new byte[a.length()*2];
            for (int i=0;i<a.length();i++){ w[i*2]=(byte)a.charAt(i); w[i*2+1]=0; }
            for (Address h : searchBytes(w, 8)) { targets.add(h.getOffset()); label.putIfAbsent(h.getOffset(), "utf16:"+a); }
        }
        f.printf("anchor target addresses: %d%n", targets.size());
        for (Long t : new TreeSet<>(targets)) f.printf("  0x%08x  %s%n", t-BASE, label.get(t));

        // 2. scan executable memory for rip-relative LEA to any target
        f.println("\n## LEA-to-anchor hits (the referencing code):");
        Set<Integer> modrm = new HashSet<>(Arrays.asList(0x05,0x0D,0x15,0x1D,0x25,0x2D,0x35,0x3D));
        int hits = 0;
        for (MemoryBlock blk : mem.getBlocks()) {
            if (!blk.isExecute() || !blk.isInitialized()) continue;
            long start = blk.getStart().getOffset();
            long size = blk.getSize();
            byte[] buf = new byte[(int)Math.min(size, Integer.MAX_VALUE)];
            mem.getBytes(blk.getStart(), buf);
            for (int i = 0; i + 7 <= buf.length; i++) {
                int b0 = buf[i] & 0xFF;
                if (b0 != 0x48 && b0 != 0x4C) continue;
                if ((buf[i+1] & 0xFF) != 0x8D) continue;
                if (!modrm.contains(buf[i+2] & 0xFF)) continue;
                long disp = ((long)(buf[i+3]&0xFF)) | ((long)(buf[i+4]&0xFF)<<8) | ((long)(buf[i+5]&0xFF)<<16) | ((long)(buf[i+6]&0xFF)<<24);
                disp = (int) disp; // sign-extend
                long instr = start + i;
                long tgt = instr + 7 + disp;
                if (!targets.contains(tgt)) continue;
                hits++;
                Address ia = addr(instr);
                Function fn = fm.getFunctionContaining(ia);
                f.printf("LEA @0x%08x -> 0x%08x (%s)  fn=%s%n", rva(ia), tgt-BASE, label.get(tgt),
                    fn != null ? String.format("0x%08x", rva(fn.getEntryPoint())) : "(no fn)");
                if (fn != null) {
                    LinkedHashSet<Long> seen = new LinkedHashSet<>(callTargets(fn));
                    for (Long t : seen) f.printf("     call -> 0x%08x  prologue: %s%n", t, prologue(addr(BASE+t), 14));
                }
            }
        }
        f.printf("%n%d LEA hits total%n", hits);
        f.close();
        println("Wrote " + outPath + " (" + hits + " LEA hits)");
    }
}
