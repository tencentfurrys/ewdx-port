// Cross-reference HSP3 runtime calls in EchidnaWarsDX.exe
// @category Analysis
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;

public class HSP3CrossRef extends GhidraScript {
    @Override
    public void run() throws Exception {
        println("=== HSP3 Cross-Reference Analysis ===");
        
        // Find all imports from hmm.dll, hspda.dll, hspogg.dll
        println("\n--- External References ---");
        ExternalManager extMgr = currentProgram.getExternalManager();
        for (ExternalLocation ext : extMgr.getExternalLocations()) {
            println("  " + ext.getLibraryName() + " -> " + ext.getLabel());
        }
        
        // Find references to HSP3 API functions
        String[] hspFuncs = {"_DDINIT", "_DGINIT", "_DIINIT", "_DSINIT", "_DMINIT",
                            "_HMMINIT", "_HMMSLEEP", "_HMMHITCHECK",
                            "_DDLOADFNAME", "_DGLOADFNAME", "_DSLOADFNAME",
                            "_DDDRAWTEXT", "_DGDRAWTEXT", "_DDSCREEN", "_DGSCREEN",
                            "_DGBUFFER", "_DDBUFFER", "_DDREDRAW", "_DGREDRAW",
                            "_DDEND", "_DGEND", "_DSEND", "_DMEND", "_DIEND",
                            "_DSPLAY", "_DSSTOP", "_DMPLAY", "_DMSTOP",
                            "_DIGETKEYSTATE", "_DIGETJOYSTATE",
                            "_HMMGETFPS", "_HMMBITCHECK"};
        
        println("\n--- HSP3 API Call References ---");
        for (String func : hspFuncs) {
            // Search for the function in imports
            for (ExternalLocation ext : extMgr.getExternalLocations()) {
                if (ext.getLabel().contains(func)) {
                    println("  Found import: " + func);
                    // Find all references to this import
                    ReferenceManager refMgr = currentProgram.getReferenceManager();
                    for (Reference ref : refMgr.getReferencesTo(ext.getThunkReference().getToAddress())) {
                        println("    Called from: " + ref.getFromAddress());
                    }
                }
            }
        }
        
        println("\n=== Analysis Complete ===");
    }
}
