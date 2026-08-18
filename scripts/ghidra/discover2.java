// Targeted GPV discovery via the surviving error format string
// "GetPlayerViewPoint FAILED for WCO Name = %s, PlayerController = %s" and the
// K2_GetPlayerViewPoint reflection name. For each anchor string we print EVERY
// reference (including refs with no containing function), and for each
// referencing function we dump all CALL targets with prologues - the real
// APlayerController::GetPlayerViewPoint is among the calls made by the function
// that logs the failure / by the exec thunk.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class discover2 extends GhidraScript {
    long BASE;
    Memory mem; Listing listing; FunctionManager fm; ReferenceManager refs; AddressFactory fact;

    Address addr(long v) { return fact.getDefaultAddressSpace().getAddress(v); }
    long rva(Address a) { return a.getOffset() - BASE; }

    String prologue(Address a, int n) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) { int b=0; try { b = mem.getByte(a.add(i)) & 0xFF; } catch (Exception e) {} sb.append(String.format("%02x ", b)); }
        return sb.toString().trim();
    }

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
                if (ta != null && fm.getFunctionAt(ta) != null) outs.add(rva(ta));
            }
        }
        return outs;
    }

    List<Address> findStrings(String needle) {
        List<Address> hits = new ArrayList<>();
        DataIterator di = listing.getDefinedData(true);
        String nl = needle.toLowerCase();
        while (di.hasNext()) {
            Data d = di.next(); Object v = d.getValue();
            if (v instanceof String && ((String) v).toLowerCase().contains(nl)) hits.add(d.getAddress());
        }
        return hits;
    }

    void dumpAnchor(PrintWriter f, String needle, boolean dumpCalls) {
        f.printf("%n## anchor: \"%s\"%n", needle);
        for (Address sa : findStrings(needle)) {
            Data d = listing.getDataAt(sa);
            String sv = d != null && d.getValue() instanceof String ? (String) d.getValue() : "";
            f.printf("  string @0x%08x  %s%n", rva(sa), sv.length() > 56 ? sv.substring(0, 56) : sv);
            int nref = 0;
            for (Reference r : refs.getReferencesTo(sa)) {
                nref++;
                Function fn = fm.getFunctionContaining(r.getFromAddress());
                f.printf("    <- ref from 0x%08x  fn=%s%n", rva(r.getFromAddress()),
                    fn != null ? String.format("0x%08x", rva(fn.getEntryPoint())) : "(no fn)");
                if (dumpCalls && fn != null) {
                    LinkedHashSet<Long> seen = new LinkedHashSet<>(callTargets(fn));
                    for (Long t : seen)
                        f.printf("         call -> 0x%08x  prologue: %s%n", t, prologue(addr(BASE + t), 14));
                }
            }
            if (nref == 0) f.println("    (no references)");
        }
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        mem = currentProgram.getMemory(); listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager(); refs = currentProgram.getReferenceManager();
        fact = currentProgram.getAddressFactory();

        String outPath = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\discover2.txt";
        PrintWriter f = new PrintWriter(outPath);
        f.printf("FinchGame.exe GPV discovery v2. image base 0x%x%n", BASE);

        dumpAnchor(f, "GetPlayerViewPoint FAILED", true);
        dumpAnchor(f, "K2_GetPlayerViewPoint", true);
        dumpAnchor(f, "GetActorEyesViewPoint", true);
        f.close();
        println("Wrote " + outPath);
    }
}
