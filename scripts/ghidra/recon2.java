// Stripped-binary recon for FinchGame.exe (no RTTI, huge image). Instead of
// RTTI vftables it locates anchor strings by raw memory search and reports the
// functions that reference them, plus basic program stats. Safe to run with
// -noanalysis against a partially-analyzed project to see what state it is in.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class recon2 extends GhidraScript {
    long BASE;

    long rva(Address a) { return a.getOffset() - BASE; }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        Memory mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refs = currentProgram.getReferenceManager();
        Listing listing = currentProgram.getListing();

        String outPath = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\recon2.txt";
        PrintWriter f = new PrintWriter(outPath);
        f.printf("FinchGame.exe recon2 (string-anchor). image base 0x%x%n", BASE);

        // --- program stats ---
        int nFuncs = fm.getFunctionCount();
        int nStrings = 0;
        DataIterator di = listing.getDefinedData(true);
        while (di.hasNext()) { Object v = di.next().getValue(); if (v instanceof String) nStrings++; }
        f.printf("stats: functions=%d definedStrings=%d%n%n", nFuncs, nStrings);

        // --- anchor strings ---
        String[] anchors = {
            "GetPlayerViewPoint", "PlayerCameraManager", "GetActorEyesViewPoint",
            "GetControlRotation", "CalcCamera", "FMinimalViewInfo", "ViewTarget"
        };
        for (String s : anchors) {
            f.printf("## anchor \"%s\"%n", s);
            byte[] pat = s.getBytes("US-ASCII");
            Address start = mem.getMinAddress();
            int found = 0;
            while (start != null && found < 8) {
                Address hit = mem.findBytes(start, pat, null, true, monitor);
                if (hit == null) break;
                found++;
                f.printf("   @ 0x%08x (rva)  ", rva(hit));
                // references to this address
                ReferenceIterator rit = refs.getReferencesTo(hit);
                List<Reference> rl = new ArrayList<>();
                while (rit.hasNext()) rl.add(rit.next());
                f.printf("%d xref(s)%n", rl.size());
                int shown = 0;
                for (Reference r : rl) {
                    if (shown++ >= 6) break;
                    Function fn = fm.getFunctionContaining(r.getFromAddress());
                    f.printf("       <- from 0x%08x  fn=%s%n", rva(r.getFromAddress()),
                        fn != null ? String.format("0x%08x", rva(fn.getEntryPoint())) : "(none)");
                }
                start = hit.add(1);
            }
            if (found == 0) f.println("   (not found in memory)");
            f.println();
        }
        f.close();
        println("Wrote " + outPath);
    }
}
