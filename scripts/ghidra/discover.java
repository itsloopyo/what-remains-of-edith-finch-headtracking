// FinchGame.exe (UE4, Shipping) RVA discovery for
// APlayerController::GetPlayerViewPoint. Java port of LNEE's discover.py
// (Ghidra 12 dropped Jython). Shipping strips the checkf ASCII strings, so GPV
// cannot be found by its own string. What survives is the reflection metadata:
// the FName "GetPlayerViewPoint" used when the engine registers the UFunction.
// Chain:
//   1. "GetPlayerViewPoint" string (ASCII and/or UTF-16 defined data)
//   2. -> code that references it = Z_Construct_UFunction_..._GetPlayerViewPoint
//   3. that registrar stores a pointer to execGetPlayerViewPoint (the BP VM thunk)
//   4. the exec thunk's internal CALL -> the real C++ GetPlayerViewPoint (hook target)
// Every link is dumped so the target can be confirmed by hand.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class discover extends GhidraScript {
    long BASE;
    Memory mem;
    Listing listing;
    FunctionManager fm;
    ReferenceManager refs;
    AddressFactory fact;

    Address addr(long v) { return fact.getDefaultAddressSpace().getAddress(v); }
    long rva(Address a) { return a.getOffset() - BASE; }

    String prologue(Address a, int n) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            int b = 0;
            try { b = mem.getByte(a.add(i)) & 0xFF; } catch (Exception e) {}
            sb.append(String.format("%02x ", b));
        }
        return sb.toString().trim();
    }

    List<Address> findStrings(String needle) {
        List<Address> hits = new ArrayList<>();
        DataIterator di = listing.getDefinedData(true);
        String nl = needle.toLowerCase();
        while (di.hasNext()) {
            Data d = di.next();
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            if (((String) v).toLowerCase().contains(nl)) hits.add(d.getAddress());
        }
        return hits;
    }

    // function entries referenced as data/operand inside fn (catches the exec
    // thunk pointer the registrar stores).
    List<Long> codePtrsInFn(Function fn) {
        TreeSet<Long> out = new TreeSet<>();
        InstructionIterator it = listing.getInstructions(fn.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            for (Reference r : ins.getReferencesFrom()) {
                Address ta = r.getToAddress();
                if (ta == null) continue;
                if (fm.getFunctionAt(ta) != null) out.add(rva(ta));
            }
        }
        return new ArrayList<>(out);
    }

    // RVAs of CALL destinations inside fn that land on a known function entry.
    List<Long> callTargets(Function fn) {
        List<Long> outs = new ArrayList<>();
        if (fn == null) return outs;
        InstructionIterator it = listing.getInstructions(fn.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            String m = ins.getMnemonicString();
            if (m == null || !m.toLowerCase().startsWith("call")) continue;
            for (Reference r : ins.getReferencesFrom()) {
                Address ta = r.getToAddress();
                if (ta == null) continue;
                if (fm.getFunctionAt(ta) != null) outs.add(rva(ta));
            }
        }
        return outs;
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        mem = currentProgram.getMemory();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        refs = currentProgram.getReferenceManager();
        fact = currentProgram.getAddressFactory();

        String outPath = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\discover.txt";
        PrintWriter f = new PrintWriter(outPath);
        f.printf("FinchGame.exe GetPlayerViewPoint discovery. image base 0x%x%n", BASE);
        for (int i = 0; i < 78; i++) f.print("="); f.println();

        f.println("\n## anchor: GetPlayerViewPoint (reflection name + registrar chain)");
        Map<Long, Integer> cand = new HashMap<>();
        for (Address sa : findStrings("GetPlayerViewPoint")) {
            Object val = listing.getDataAt(sa) != null ? listing.getDataAt(sa).getValue() : "";
            f.printf("  string @0x%08x %s%n", rva(sa),
                val.toString().length() > 50 ? val.toString().substring(0, 50) : val.toString());
            for (Reference r : refs.getReferencesTo(sa)) {
                Function fn = fm.getFunctionContaining(r.getFromAddress());
                if (fn == null) continue;
                f.printf("    referenced by fn 0x%08x (%s) @ref 0x%08x%n",
                    rva(fn.getEntryPoint()), fn.getName(), rva(r.getFromAddress()));
                for (Long p : codePtrsInFn(fn)) {
                    Function thunk = fm.getFunctionAt(addr(BASE + p));
                    List<Long> tgts = callTargets(thunk);
                    StringBuilder ts = new StringBuilder();
                    for (int i = 0; i < Math.min(tgts.size(), 8); i++) ts.append(String.format("0x%08x ", tgts.get(i)));
                    f.printf("      code-ptr 0x%08x (%s) -> internal calls: %s%n",
                        p, thunk != null ? thunk.getName() : "?", ts.toString().trim());
                    for (Long t : tgts) cand.merge(t, 1, Integer::sum);
                }
            }
        }

        f.println("\n  GPV real-function candidates (call targets inside exec thunks):");
        List<Map.Entry<Long, Integer>> sorted = new ArrayList<>(cand.entrySet());
        sorted.sort((a, b) -> b.getValue() - a.getValue());
        for (int i = 0; i < Math.min(sorted.size(), 12); i++) {
            long c = sorted.get(i).getKey();
            f.printf("    0x%08x  (seen %dx)  prologue: %s%n", c, sorted.get(i).getValue(),
                prologue(addr(BASE + c), 16));
        }

        for (String label : new String[]{"PlayerCameraManager", "GetActorEyesViewPoint",
                "bShowMouseCursor", "GetCameraViewPoint"}) {
            f.printf("%n## anchor: %s%n", label);
            int n = 0;
            for (Address sa : findStrings(label)) {
                if (n++ >= 6) break;
                Object val = listing.getDataAt(sa) != null ? listing.getDataAt(sa).getValue() : "";
                f.printf("  string @0x%08x %s%n", rva(sa),
                    val.toString().length() > 50 ? val.toString().substring(0, 50) : val.toString());
                int m = 0;
                for (Reference r : refs.getReferencesTo(sa)) {
                    if (m++ >= 6) break;
                    Function fn = fm.getFunctionContaining(r.getFromAddress());
                    if (fn != null) f.printf("    ref by fn 0x%08x (%s)%n", rva(fn.getEntryPoint()), fn.getName());
                }
            }
        }
        f.close();
        println("Wrote " + outPath);
    }
}
