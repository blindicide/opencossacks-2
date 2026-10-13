// Выгружает декомпилированный C-код всех функций и индекс функций (адрес, размер, имя).
// Декомпиляция идёт в несколько потоков; результат пишется в порядке адресов.
// Использование: analyzeHeadless ... -postScript ExportDecomp.java <outdir> [потоков]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.util.task.TaskMonitor;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args[0]);
        int threads = args.length > 1 ? Integer.parseInt(args[1]) : Runtime.getRuntime().availableProcessors();
        out.mkdirs();

        List<Function> funcs = new ArrayList<>();
        currentProgram.getFunctionManager().getFunctions(true).forEach(funcs::add);
        int total = funcs.size();
        println("функций: " + total + ", потоков: " + threads);

        try (PrintWriter idx = new PrintWriter(new File(out, "functions.tsv"))) {
            for (Function f : funcs)
                idx.printf("%s\t%d\t%s%n", f.getEntryPoint(), f.getBody().getNumAddresses(), f.getName());
        }

        // У каждого потока свой экземпляр декомпилятора: DecompInterface не потокобезопасен.
        String[] results = new String[total];
        AtomicInteger next = new AtomicInteger(), done = new AtomicInteger();
        ExecutorService pool = Executors.newFixedThreadPool(threads);
        for (int t = 0; t < threads; t++) {
            pool.submit(() -> {
                DecompInterface di = new DecompInterface();
                di.openProgram(currentProgram);
                try {
                    int i;
                    while ((i = next.getAndIncrement()) < total && !monitor.isCancelled()) {
                        Function f = funcs.get(i);
                        DecompileResults r = di.decompileFunction(f, 60, TaskMonitor.DUMMY);
                        results[i] = String.format("// ==== %s @ %s%n%s", f.getName(), f.getEntryPoint(),
                            r.decompileCompleted() ? r.getDecompiledFunction().getC()
                                                   : "// декомпиляция не удалась: " + r.getErrorMessage() + "\n");
                        int d = done.incrementAndGet();
                        if (d % 1000 == 0) println("декомпилировано " + d + " / " + total);
                    }
                } finally {
                    di.dispose();
                }
                return null;
            });
        }
        pool.shutdown();
        pool.awaitTermination(7, TimeUnit.DAYS);

        try (PrintWriter c = new PrintWriter(new File(out, "engine.c"))) {
            for (String s : results) if (s != null) c.print(s);
        }
        println("готово: " + done.get() + " / " + total);
    }
}
