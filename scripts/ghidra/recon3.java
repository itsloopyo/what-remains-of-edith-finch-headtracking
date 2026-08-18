// Find code anchors into the camera/view subsystem of a stripped UE4 binary.
// The FName pool strings (GetPlayerViewPoint etc.) have no code xrefs, so
// instead scan every DEFINED string for camera/view keywords and .cpp assert
// paths that DO have xrefs - each such xref lands inside camera code we can
// work outward from. Project must be fully analyzed (refs built).
import java.io.PrintWriter;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class recon3 extends GhidraScript {
    long BASE;
    long rva(Address a) { return a.getOffset() - BASE; }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager refs = currentProgram.getReferenceManager();
        Listing listing = currentProgram.getListing();

        String outPath = "C:\\Data\\repos\\itsloopyo\\what-remains-of-edith-finch-headtracking\\.lab\\ghidra\\recon3.txt";
        PrintWriter f = new PrintWriter(outPath);
        f.printf("FinchGame.exe recon3 (camera-anchor strings WITH xrefs). base 0x%x%n%n", BASE);

        // Substrings that, when found in a string that code references, point at
        // camera/view subsystem code. .cpp paths from check/ensure macros are
        // the gold standard (they sit inside the exact function that asserts).
        String[] keys = {
            "PlayerCameraManager", "CameraManager.cpp", "PlayerController.cpp",
            "Camera.cpp", "SceneView", "CalcSceneView", "CalcCamera",
            "ProcessViewRotation", "ViewRotation", "GetViewPoint",
            "ViewTarget", "FieldOfView", "FMinimalViewInfo", "PlayerCameraManager.cpp",
            "GameplayCamera", "CameraComponent", "CameraActor", "ViewportClient"
        };
        int printed = 0;
        DataIterator di = listing.getDefinedData(true);
        while (di.hasNext()) {
            Data d = di.next();
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String sv = (String) v;
            boolean ok = false;
            for (String k : keys) if (sv.contains(k)) { ok = true; break; }
            if (!ok) continue;
            // only strings that code references
            ReferenceIterator rit = refs.getReferencesTo(d.getAddress());
            List<Reference> rl = new ArrayList<>();
            while (rit.hasNext()) rl.add(rit.next());
            if (rl.isEmpty()) continue;
            String disp = sv.length() > 70 ? sv.substring(0, 70) : sv;
            f.printf("str 0x%08x %-72s (%d xref)%n", rva(d.getAddress()), "\"" + disp + "\"", rl.size());
            int shown = 0;
            for (Reference r : rl) {
                if (shown++ >= 8) break;
                Function fn = fm.getFunctionContaining(r.getFromAddress());
                f.printf("    <- 0x%08x  fn=%s%n", rva(r.getFromAddress()),
                    fn != null ? String.format("0x%08x", rva(fn.getEntryPoint())) : "(none)");
            }
            printed++;
        }
        if (printed == 0) f.println("(no camera-keyword strings with xrefs found)");
        f.printf("%n%d anchor strings printed%n", printed);
        f.close();
        println("Wrote " + outPath + " (" + printed + " anchors)");
    }
}
