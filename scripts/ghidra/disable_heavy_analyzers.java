// Pre-script (Java; Ghidra 12 dropped Jython): disable the decompiler-based
// analyzers before auto-analysis. On FinchGame.exe the "Decompiler Parameter
// ID" analyzer gets stuck in a runaway loop decompiling a packed/obfuscated
// function (140c859dd), spewing endless "non-existing memory" pcode warnings
// and never finishing (it ran >24h once). recon.java only needs RTTI vftable
// symbols + string xrefs, which do not depend on the decompiler, so switch
// every decompiler-dependent analyzer off to let analysis finish in minutes.
import ghidra.app.script.GhidraScript;
import ghidra.framework.options.Options;
import ghidra.program.model.listing.Program;

public class disable_heavy_analyzers extends GhidraScript {
    public void run() throws Exception {
        Options opts = currentProgram.getOptions(Program.ANALYSIS_PROPERTIES);
        String[] kill = {"Decompiler", "Switch", "Parameter ID", "Call Convention"};
        for (String name : opts.getOptionNames()) {
            if (name.contains(".")) continue;          // skip sub-options
            boolean hit = false;
            for (String k : kill) if (name.contains(k)) { hit = true; break; }
            if (!hit) continue;
            try {
                opts.setBoolean(name, false);
                println("disabled analyzer: " + name);
            } catch (Exception e) {
                println("could not disable " + name + ": " + e);
            }
        }
    }
}
