import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import java.util.HashSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class DecompOne extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: DecompOne <sym-substring> [<sym-substring> ...]");
            return;
        }

        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        HashSet<String> followed = new HashSet<String>();
        Pattern funPat = Pattern.compile("FUN_([0-9a-f]{8})");

        for (String want : args) {
        println("=== searching for: " + want + " ===");

        FunctionIterator funcs = currentProgram.getFunctionManager().getFunctions(true);
        int done = 0;
        String lastC = null;
        while (funcs.hasNext()) {
            Function f = funcs.next();
            String name = f.getName();
            if (name.indexOf(want) < 0) {
                continue;
            }
            println("--- function: " + name + " @ " + f.getEntryPoint() + " ---");
            DecompileResults res = dec.decompileFunction(f, 60, monitor);
            if (res != null && res.decompileCompleted()) {
                lastC = res.getDecompiledFunction().getC();
                println(lastC);
            }
            else {
                println("*** decompile failed ***");
            }
            done++;
            if (done >= 6) {
                break;
            }
        }
        // auto-follow one level of FUN_xxxxxxxx callees from the last body
        if (lastC != null) {
            Matcher m = funPat.matcher(lastC);
            while (m.find()) {
                String addr = m.group(1);
                if (followed.contains(addr)) continue;
                followed.add(addr);
                FunctionIterator all = currentProgram.getFunctionManager().getFunctions(true);
                while (all.hasNext()) {
                    Function f2 = all.next();
                    if (f2.getEntryPoint().toString().endsWith(addr)) {
                        println("--- followed: " + f2.getName() + " @ " + f2.getEntryPoint() + " ---");
                        DecompileResults r2 = dec.decompileFunction(f2, 120, monitor);
                        if (r2 != null && r2.decompileCompleted()) {
                            println(r2.getDecompiledFunction().getC());
                        }
                        break;
                    }
                }
            }
        }
        if (done == 0) {
            // fallback: dump matching symbols
            SymbolTable st = currentProgram.getSymbolTable();
            for (Symbol s : st.getAllSymbols(false)) {
                if (s.getName().indexOf(want) >= 0) {
                    println("symbol: " + s.getName() + " @ " + s.getAddress() +
                            " type=" + s.getSymbolType());
                }
            }
        }
        } // end for each wanted substring
        dec.dispose();
    }
}
