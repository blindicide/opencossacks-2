// Запускает автоанализ с заданным набором отключённых анализаторов и замеряет время.
// Каждые 30 с дописывает в <timeline> активный анализатор и накопленные времена Ghidra.
// Если появляется файл <cancel>, анализ корректно прерывается, а проект сохраняется.
// Использование: analyzeHeadless re/project engine -process engine.exe -noanalysis
//   -postScript RunAnalysis.java <timeline> <cancel> "Анализатор 1;Анализатор 2"
import ghidra.app.script.GhidraScript;
import ghidra.app.plugin.core.analysis.AutoAnalysisManager;
import ghidra.framework.options.Options;
import ghidra.program.model.listing.Program;
import ghidra.util.task.TaskMonitorAdapter;
import java.io.*;
import java.time.*;
import java.util.*;

public class RunAnalysis extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File timeline = new File(args[0]);
        File cancelFlag = new File(args[1]);
        Set<String> disable = new HashSet<>();
        if (args.length > 2 && !args[2].isBlank())
            for (String s : args[2].split(";")) disable.add(s.trim());

        Options opts = currentProgram.getOptions(Program.ANALYSIS_PROPERTIES);
        for (String name : opts.getOptionNames()) {
            if (name.contains(".")) continue; // только флаги включения анализаторов
            Object v = opts.getObject(name, null);
            if (!(v instanceof Boolean)) continue;
            if (disable.contains(name)) opts.setBoolean(name, false);
            println(String.format("анализатор %-50s %s", name, opts.getBoolean(name, false) ? "вкл" : "выкл"));
        }

        AutoAnalysisManager mgr = AutoAnalysisManager.getAnalysisManager(currentProgram);
        TaskMonitorAdapter analysisMonitor = new TaskMonitorAdapter(true);
        Thread worker = Thread.currentThread();
        Instant start = Instant.now();

        // Наблюдатель: замеры и проверка флага отмены, без вмешательства в ход анализа.
        Thread watcher = new Thread(() -> {
            try (PrintWriter w = new PrintWriter(new FileWriter(timeline, true), true)) {
                while (!Thread.currentThread().isInterrupted()) {
                    String active = "?";
                    for (StackTraceElement e : worker.getStackTrace()) {
                        String c = e.getClassName();
                        if (c.endsWith("Analyzer") || c.endsWith("Cmd")) { active = c.substring(c.lastIndexOf('.') + 1); break; }
                    }
                    long sec = Duration.between(start, Instant.now()).getSeconds();
                    w.printf("%d\t%s%n", sec, active);
                    if (cancelFlag.exists() && !analysisMonitor.isCancelled()) {
                        w.printf("%d\tОТМЕНА по флагу%n", sec);
                        analysisMonitor.cancel();
                    }
                    Thread.sleep(30_000);
                }
            } catch (InterruptedException | IOException e) {
                // завершение наблюдателя
            }
        }, "analysis-watcher");
        watcher.setDaemon(true);
        watcher.start();

        mgr.reAnalyzeAll(null);
        mgr.startAnalysis(analysisMonitor);
        watcher.interrupt();

        long total = Duration.between(start, Instant.now()).getSeconds();
        println("анализ " + (analysisMonitor.isCancelled() ? "ПРЕРВАН" : "завершён") + " за " + total + " с");
        println("времена анализаторов (Ghidra):\n" + mgr.getTaskTimesString());
        try (PrintWriter w = new PrintWriter(new FileWriter(timeline, true))) {
            w.printf("%d\tИТОГ%n%s%n", total, mgr.getTaskTimesString());
        }
    }
}
