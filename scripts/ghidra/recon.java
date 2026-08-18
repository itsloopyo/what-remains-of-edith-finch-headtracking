// UE4 (Ethan Carter Redux) camera-hook recon, Java GhidraScript (Ghidra 12
// dropped Jython, so the .py sibling needs PyGhidra; this one always runs).
// Dumps:
//   1. RTTI vftable symbols for *Controller / *CameraManager / *LocalPlayer.
//   2. Camera / view / projection defined strings + their referrers.
//   3. APlayerController vs AController vtable slot comparison (overrides =
//      GetPlayerViewPoint candidates).
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class recon extends GhidraScript {
    long BASE;
    Memory mem;
    Listing listing;
    FunctionManager fm;
    SymbolTable st;
    ReferenceManager refs;
    AddressFactory fact;

    Address addr(long v) { return fact.getDefaultAddressSpace().getAddress(v); }
    long rva(Address a) { return a.getOffset() - BASE; }

    long readPtr(Address a) {
        try { return mem.getLong(a); } catch (Exception e) { return 0; }
    }

    // (idx, fnRVA) slots; stop on null/out-of-image pointer.
    List<long[]> vtableSlots(Address start, int n) {
        List<long[]> out = new ArrayList<>();
        for (int i = 0; i < n; i++) {
            long p = readPtr(start.add((long) i * 8));
            if (p == 0) break;
            if (p < BASE || p > BASE + 0x4000000L) break;
            out.add(new long[]{i, p - BASE});
        }
        return out;
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        mem = currentProgram.getMemory();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        st = currentProgram.getSymbolTable();
        refs = currentProgram.getReferenceManager();
        fact = currentProgram.getAddressFactory();

        String outPath = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\recon.txt";
        PrintWriter f = new PrintWriter(outPath);
        f.printf("FinchGame.exe recon. image base 0x%x%n", BASE);
        for (int i = 0; i < 78; i++) f.print("=");
        f.println("\n");

        // 1. RTTI vftable symbols.
        f.println("## 1. RTTI vftable symbols (Controller / CameraManager / LocalPlayer / Pawn)");
        String[] want = {"PlayerController", "CameraManager", "LocalPlayer", "Controller", "Camera", "Pawn"};
        Map<String, Address> vtbls = new LinkedHashMap<>();
        SymbolIterator it = st.getAllSymbols(true);
        while (it.hasNext()) {
            Symbol s = it.next();
            String nm = s.getName(true);
            if (!nm.contains("vftable") && !nm.contains("vtable")) continue;
            boolean ok = false;
            for (String w : want) if (nm.contains(w)) { ok = true; break; }
            if (!ok) continue;
            f.printf("   RVA 0x%08x  %s%n", rva(s.getAddress()), nm.length() > 100 ? nm.substring(0, 100) : nm);
            vtbls.put(nm, s.getAddress());
        }
        if (vtbls.isEmpty())
            f.println("   (no RTTI vftable symbols found - RTTI may be stripped; rely on strings)");

        // 2. Camera/view/projection strings.
        f.println("\n## 2. Camera / view / projection strings + referrers");
        String[] keys = {"GetPlayerViewPoint", "PlayerCameraManager", "CameraCache", "FMinimalViewInfo",
                "GetProjectionData", "CalcSceneView", "FieldOfView", "GetCameraViewPoint",
                "ViewTarget", "GetActorEyesViewPoint", "CalcCamera"};
        int hits = 0;
        DataIterator di = listing.getDefinedData(true);
        while (di.hasNext() && hits < 60) {
            Data d = di.next();
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String sv = (String) v;
            boolean ok = false;
            for (String k : keys) if (sv.toLowerCase().contains(k.toLowerCase())) { ok = true; break; }
            if (!ok) continue;
            ReferenceIterator rit = refs.getReferencesTo(d.getAddress());
            List<Reference> rl = new ArrayList<>();
            while (rit.hasNext()) rl.add(rit.next());
            f.printf("   str 0x%08x %-46s (%d refs)%n", rva(d.getAddress()),
                    "\"" + (sv.length() > 42 ? sv.substring(0, 42) : sv) + "\"", rl.size());
            int shown = 0;
            for (Reference r : rl) {
                if (shown++ >= 5) break;
                Function fn = fm.getFunctionContaining(r.getFromAddress());
                f.printf("       <- fn 0x%08x%n", fn != null ? rva(fn.getEntryPoint()) : 0);
            }
            hits++;
        }
        if (hits == 0) f.println("   (none of the camera keys found as strings)");

        // 3. vtable slot comparison.
        f.println("\n## 3. vtable slot comparison (override = candidate GetPlayerViewPoint)");
        Address apc = null, ac = null;
        String apcNm = null, acNm = null;
        for (Map.Entry<String, Address> e : vtbls.entrySet()) {
            if (apc == null && e.getKey().contains("PlayerController")) { apc = e.getValue(); apcNm = e.getKey(); }
        }
        for (Map.Entry<String, Address> e : vtbls.entrySet()) {
            String k = e.getKey();
            if (k.contains("Controller") && !k.contains("PlayerController")) { ac = e.getValue(); acNm = k; break; }
        }
        if (apc != null) f.printf("   APlayerController vftable @ 0x%08x (%s)%n", rva(apc), apcNm);
        if (ac != null) f.printf("   AController       vftable @ 0x%08x (%s)%n", rva(ac), acNm);
        if (apc != null && ac != null) {
            List<long[]> apcS = vtableSlots(apc, 90);
            List<long[]> acS = vtableSlots(ac, 90);
            Map<Long, Long> acMap = new HashMap<>();
            for (long[] s : acS) acMap.put(s[0], s[1]);
            f.println("   idx  APlayerController     AController        override?");
            for (long[] s : apcS) {
                long acr = acMap.getOrDefault(s[0], 0L);
                String ovr = (acr != 0 && acr != s[1]) ? "  <-- OVERRIDE" : "";
                f.printf("   %3d  0x%08x          0x%08x%s%n", s[0], s[1], acr, ovr);
            }
        } else if (apc != null) {
            f.println("   (AController vftable not found; APlayerController slots only)");
            for (long[] s : vtableSlots(apc, 90)) f.printf("   %3d  0x%08x%n", s[0], s[1]);
        }

        f.close();
        println("Wrote " + outPath);
    }
}
