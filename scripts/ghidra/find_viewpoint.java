// Behavioral scan for UE4 GetCameraViewPoint / GetPlayerViewPoint in a
// stripped-RTTI build. Both have the shape:
//   void f(RCX=this, RDX=FVector* outLoc, R8=FRotator* outRot)
// copying 3 floats (Location) to [RDX..] and 3 floats (Rotation) to [R8..]
// from a member struct of `this`. We flag small functions that store
// float-width values through both the RDX and R8 registers.
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.lang.Register;

public class find_viewpoint extends GhidraScript {
    long BASE;
    FunctionManager fm;
    ReferenceManager refs;

    long rva(Address a) { return a.getOffset() - BASE; }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        fm = currentProgram.getFunctionManager();
        refs = currentProgram.getReferenceManager();

        String outPath = "C:\\Data\\repos\\itsloopyo\\the-vanishing-of-ethan-carter-redux-headtracking\\.lab\\ghidra\\viewpoint.txt";
        PrintWriter f = new PrintWriter(outPath);

        String[] FMOVE = {"MOVSS", "MOVSD", "MOVUPS", "MOVAPS", "MOVLPS", "MOVHPS", "MOVQ", "MOVLPD"};
        Set<String> fmove = new HashSet<>(Arrays.asList(FMOVE));

        List<long[]> cands = new ArrayList<>();   // {rva, score, sizeBytes, callCount}
        Map<Long, Integer> callCount = new HashMap<>();

        FunctionIterator it = fm.getFunctions(true);
        while (it.hasNext()) {
            Function fn = it.next();
            long size = fn.getBody().getNumAddresses();
            if (size < 6 || size > 260) continue;   // small leaf-ish copiers

            boolean storeRDX = false, storeR8 = false, readRCX = false;
            int fmoves = 0, calls = 0;
            InstructionIterator iit = currentProgram.getListing().getInstructions(fn.getBody(), true);
            int n = 0;
            while (iit.hasNext()) {
                Instruction ins = iit.next();
                if (++n > 80) break;
                String mn = ins.getMnemonicString().toUpperCase();
                if (mn.startsWith("CALL")) calls++;
                boolean isF = fmove.contains(mn);
                // Examine operands' register usage + dest memory base.
                int nops = ins.getNumOperands();
                for (int op = 0; op < nops; op++) {
                    Object[] objs = ins.getOpObjects(op);
                    boolean opHasRDX = false, opHasR8 = false, opHasRCX = false;
                    for (Object o : objs) {
                        if (o instanceof Register) {
                            String rn = ((Register) o).getName().toUpperCase();
                            if (rn.equals("RDX") || rn.equals("EDX")) opHasRDX = true;
                            if (rn.equals("R8") || rn.equals("R8D")) opHasR8 = true;
                            if (rn.equals("RCX") || rn.equals("ECX")) opHasRCX = true;
                        }
                    }
                    // op 0 is destination for moves; memory dest through RDX/R8.
                    boolean isDest = (op == 0);
                    boolean isMem = ins.getOperandType(op) != 0 &&
                            (ins.getDefaultOperandRepresentation(op).contains("["));
                    if (isF && isDest && isMem && opHasRDX) storeRDX = true;
                    if (isF && isDest && isMem && opHasR8) storeR8 = true;
                    if (isMem && opHasRCX) readRCX = true;
                }
                if (isF) fmoves++;
            }

            if (storeRDX && storeR8) {
                int score = (readRCX ? 2 : 0) + (calls <= 2 ? 1 : 0) + (fmoves >= 2 ? 1 : 0);
                cands.add(new long[]{rva(fn.getEntryPoint()), score, size, calls, fmoves, readRCX ? 1 : 0});
            }
        }

        // Count callers for each candidate (helps spot GetCameraViewPoint, which
        // GetPlayerViewPoint calls).
        cands.sort((a, b) -> Long.compare(b[1], a[1]));
        f.printf("Found %d (this, RDX*, R8*) float-copy candidates (score desc):%n", cands.size());
        f.println("   rva         score size  calls fmoves readRCX  callers");
        int shown = 0;
        for (long[] c : cands) {
            if (shown++ > 60) break;
            int nc = 0;
            ReferenceIterator rit = refs.getReferencesTo(
                    currentProgram.getImageBase().add(c[0]));
            while (rit.hasNext()) { Reference r = rit.next(); if (r.getReferenceType().isCall()) nc++; }
            f.printf("   0x%08x  %3d   %4d  %3d   %3d    %d       %d%n",
                    c[0], c[1], c[2], c[3], c[4], c[5], nc);
        }
        f.close();
        println("Wrote " + outPath + " (" + cands.size() + " candidates)");
    }
}
