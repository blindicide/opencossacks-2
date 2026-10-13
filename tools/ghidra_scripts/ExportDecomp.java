// Выгружает декомпилированный C-код всех функций и индекс функций (адрес, размер, имя).
// Использование: analyzeHeadless ... -postScript ExportDecomp.java <outdir>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        File out = new File(getScriptArgs()[0]);
        out.mkdirs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        try (PrintWriter idx = new PrintWriter(new File(out, "functions.tsv"));
             PrintWriter c = new PrintWriter(new File(out, "engine.c"))) {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            int n = 0;
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                idx.printf("%s\t%d\t%s%n", f.getEntryPoint(), f.getBody().getNumAddresses(), f.getName());
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                c.printf("// ==== %s @ %s%n", f.getName(), f.getEntryPoint());
                c.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed: " + r.getErrorMessage());
                if (++n % 2000 == 0) println("decompiled " + n);
            }
            println("total functions: " + n);
        }
    }
}
